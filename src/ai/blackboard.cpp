#include "ai/ai.hpp"
#include "core/templates.hpp"
#include "sim/components.hpp"
#include "sim/economy.hpp"

#include <algorithm>
#include <limits>

// Blackboard: 총사령부 지휘정보망. 감지 결과만 쓰고 판단은 HTN·Utility가 한다.

namespace
{
    constexpr float RallyRadiusKm = 60.0f;

    float StrengthNear(const std::vector<std::pair<Vec2, float>>& units, Vec2 point, float radius)
    {
        float sum = 0.0f;
        for (const auto& [position, hp] : units)
        {
            if (Distance(position, point) <= radius)
            {
                sum += hp;
            }
        }
        return sum;
    }

    // 지을 공장 고르기: 편제에 필요한데 없는 공장 > 군수품 부족 > 가장 많이 쓰는 공장 증설.
    int ChooseFactory(const FactionState& state, const GameData& data)
    {
        std::vector<int> usage(data.factories.size(), 0);
        for (const TemplateDef& templ : state.templates)
        {
            for (const int type : GetTemplateNeeds(templ, data).factories)
            {
                ++usage[static_cast<std::size_t>(type)];
            }
        }

        int building = 0;
        for (std::size_t type = 0; type < data.factories.size(); ++type)
        {
            building += FactoriesUnderConstruction(state, static_cast<int>(type));
        }
        for (std::size_t type = 0; type < data.factories.size(); ++type)
        {
            const int t = static_cast<int>(type);
            if (usage[type] > 0 && ActiveFactories(state, t) == 0 && FactoriesUnderConstruction(state, t) == 0)
            {
                return t;
            }
        }
        if (state.munitions < 50.0f)
        {
            for (std::size_t type = 0; type < data.factories.size(); ++type)
            {
                const int t = static_cast<int>(type);
                if (data.factories[type].munitions > 0.0f && FactoriesUnderConstruction(state, t) == 0)
                {
                    return t;
                }
            }
        }
        if (building > 0)
        {
            return -1;   // 증설은 한 번에 하나씩
        }
        const auto most = std::max_element(usage.begin(), usage.end());
        return *most > 0 ? static_cast<int>(most - usage.begin()) : -1;
    }
}

void BuildBlackboard(const World& world, const GameData& data, int self, AiBlackboard& board)
{
    const GameConfig& c = data.config;
    const FactionState& state = world.factions[static_cast<std::size_t>(self)];

    std::vector<std::pair<Vec2, float>> own;
    std::vector<std::pair<Vec2, float>> enemy;
    for (const auto [entity, division, position, faction] : world.registry.view<Division, Position, Faction>().each())
    {
        (faction.id == self ? own : enemy).push_back({ position.value, division.hp });
    }

    board.ownStrength = 0.0f;
    board.enemyStrength = 0.0f;
    Vec2 sum;
    for (const auto& [position, hp] : own)
    {
        board.ownStrength += hp;
        sum = { sum.x + position.x, sum.y + position.y };
    }
    for (const auto& [position, hp] : enemy)
    {
        board.enemyStrength += hp;
    }
    if (!own.empty())
    {
        board.center = { sum.x / static_cast<float>(own.size()), sum.y / static_cast<float>(own.size()) };
    }
    board.timeLeftRatio = world.matchSeconds > 0.0f ? std::max(0.0f, 1.0f - world.time / world.matchSeconds) : 1.0f;

    // 경제
    board.canProduce = false;
    float cheapestManpower = std::numeric_limits<float>::max();
    for (const TemplateDef& templ : state.templates)
    {
        cheapestManpower = std::min(cheapestManpower, GetTemplateNeeds(templ, data).manpower);
        board.canProduce = board.canProduce || ProductionBlocker(state, data, templ).empty();
    }
    board.canProduce = board.canProduce && state.queue.size() < 2;
    board.needManpower = state.manpower < cheapestManpower;
    board.canConscript = state.draftPressure + c.conscriptPressure <= c.pressureSafe;
    const float oilBalance = state.mineRate[ToIndex(Resource::Oil)] - state.upkeep;
    board.canAffordUpkeep = oilBalance > 0.0f || state.stock[ToIndex(Resource::Oil)] > state.upkeep * 120.0f;
    board.factoryToBuild = ChooseFactory(state, data);
    board.canBuildFactory = board.factoryToBuild >= 0 && FactoryBlocker(world, data, self, board.factoryToBuild).empty();

    // 위협받는 아군 기지, 공격 후보 적 기지
    board.threats.clear();
    board.objectives.clear();
    const float radius = c.aiDefenseKm;
    for (const auto [entity, base, faction, position] : world.registry.view<Base, Faction, Position>().each())
    {
        const bool hq = base.type == BaseType::Hq;
        if (faction.id == self)
        {
            if (hq)
            {
                board.center = position.value;
            }
            const float enemyNear = StrengthNear(enemy, position.value, radius);
            if (enemyNear > 0.0f)
            {
                board.threats.push_back({ .baseHex = base.hex, .position = position.value, .enemyStrength = enemyNear,
                                          .ownStrength = StrengthNear(own, position.value, radius), .hq = hq });
            }
        }
        else
        {
            board.objectives.push_back({ .hex = base.hex, .position = position.value, .guardStrength = StrengthNear(enemy, position.value, radius), .hq = hq });
        }
    }

    // 지난 결정이 더는 유효하지 않으면 지운다 (목표 기지를 점령했거나 집결지를 잃음).
    if (std::none_of(board.objectives.begin(), board.objectives.end(), [&](const AiObjective& o) { return o.hex == board.attackHex; }))
    {
        board.attackHex = -1;
    }
    if (board.rallyHex >= 0 && world.map.hexes[static_cast<std::size_t>(board.rallyHex)].owner != self)
    {
        board.rallyHex = -1;
    }
    board.rallyGathered = board.rallyHex >= 0 ? StrengthNear(own, board.rallyPosition, RallyRadiusKm) : 0.0f;
}
