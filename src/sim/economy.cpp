#include "sim/economy.hpp"
#include "sim/components.hpp"
#include "sim/simulation.hpp"
#include "sim/systems.hpp"
#include "core/templates.hpp"

#include <algorithm>
#include <deque>
#include <format>
#include <random>

namespace
{
    // 진영에 나쁜 일이 생겼다는 이벤트 (청군 기준 색).
    void FactionTrouble(World& world, int faction, const std::string& text)
    {
        PushEvent(world, world.factions[static_cast<std::size_t>(faction)].name + ": " + text,
                  faction == PlayerFaction ? EventKind::Bad : EventKind::Good);
    }

    bool Roll(World& world, float chance)
    {
        return chance > 0.0f && std::uniform_real_distribution<float>(0.0f, 1.0f)(world.rng) < chance;
    }

    std::vector<entt::entity> FactionDivisions(const World& world, int faction)
    {
        std::vector<entt::entity> result;
        for (const auto [entity, division, owner] : world.registry.view<Division, Faction>().each())
        {
            if (owner.id == faction)
            {
                result.push_back(entity);
            }
        }
        return result;
    }

    entt::entity RandomDivision(World& world, const std::vector<entt::entity>& divisions)
    {
        if (divisions.empty())
        {
            return entt::null;
        }
        return divisions[std::uniform_int_distribution<std::size_t>(0, divisions.size() - 1)(world.rng)];
    }

    // 공장이 있는 헥스에서 아군 영토로만 이어지는 헥스 표시.
    std::vector<char> ReachFromFactories(const World& world, int faction)
    {
        const HexMap& map = world.map;
        std::vector<char> reach(map.hexes.size(), 0);
        std::deque<int> open;
        for (const FactoryState& factory : world.factions[static_cast<std::size_t>(faction)].factories)
        {
            if (factory.daysLeft <= 0.0f && map.hexes[static_cast<std::size_t>(factory.hex)].owner == faction && !reach[static_cast<std::size_t>(factory.hex)])
            {
                reach[static_cast<std::size_t>(factory.hex)] = 1;
                open.push_back(factory.hex);
            }
        }
        while (!open.empty())
        {
            const int current = open.front();
            open.pop_front();
            for (const HexCoord next : HexNeighbors(IndexToHex(map, current)))
            {
                if (!InBounds(map, next))
                {
                    continue;
                }
                const int index = HexIndex(map, next);
                if (!reach[static_cast<std::size_t>(index)] && map.hexes[static_cast<std::size_t>(index)].owner == faction)
                {
                    reach[static_cast<std::size_t>(index)] = 1;
                    open.push_back(index);
                }
            }
        }
        return reach;
    }

    bool HasStock(const ResourceAmounts& stock, const ResourceAmounts& need)
    {
        for (std::size_t r = 0; r < ResourceCount; ++r)
        {
            if (stock[r] + 0.001f < need[r])
            {
                return false;
            }
        }
        return true;
    }

    void Spend(ResourceAmounts& stock, const ResourceAmounts& cost)
    {
        for (std::size_t r = 0; r < ResourceCount; ++r)
        {
            stock[r] = std::max(0.0f, stock[r] - cost[r]);
        }
    }

    // 1초 단위 경제 처리.
    void EconomyTick(World& world, const GameData& data, int f)
    {
        const GameConfig& c = data.config;
        FactionState& state = world.factions[static_cast<std::size_t>(f)];

        // 광산 -> 공장
        const std::vector<char> reach = ReachFromFactories(world, f);
        state.mineRate.fill(0.0f);
        for (Mine& mine : world.mines)
        {
            const int owner = world.map.hexes[static_cast<std::size_t>(mine.hex)].owner;
            if (owner < 0)
            {
                mine.connected = false;
            }
            if (owner != f)
            {
                continue;
            }
            mine.connected = reach[static_cast<std::size_t>(mine.hex)] != 0;
            if (mine.connected)
            {
                float yield = std::max(0.0f, mine.rate * state.incomeMultiplier);
                if (mine.reserve > 0.0f)
                {
                    yield = std::min(yield, mine.reserve);
                    mine.reserve -= yield;
                    if (mine.reserve == 0.0f)
                    {
                        mine.rate = 0.0f;
                        PushEvent(world, std::string(ResourceNames[ToIndex(mine.resource)]) + " mine exhausted", EventKind::Info);
                    }
                }
                state.mineRate[ToIndex(mine.resource)] += yield;
            }
        }
        for (std::size_t r = 0; r < ResourceCount; ++r)
        {
            state.stock[r] += state.mineRate[r];
        }

        for (const MapCity& city : world.features.cities)
        {
            if (world.map.hexes[static_cast<std::size_t>(city.hex)].owner == f)
            {
                state.manpower += c.cityManpowerPer100k * (static_cast<float>(city.population) / 100000.0f);
            }
        }

        // 공장: 건설 진행, 군수품 생산
        for (FactoryState& factory : state.factories)
        {
            const FactoryDef& def = data.factories[static_cast<std::size_t>(factory.type)];
            if (factory.daysLeft > 0.0f)
            {
                factory.daysLeft -= c.daysPerSecond;
                if (factory.daysLeft <= 0.0f && f == PlayerFaction && !state.aiControlled)
                {
                    PushEvent(world, def.name + " completed", EventKind::Good);
                }
                continue;
            }
            if (def.munitions > 0.0f && HasStock(state.stock, def.input))
            {
                Spend(state.stock, def.input);
                state.munitions += def.munitions;
            }
        }

        // 유지비: 석유, 교전 중 군수품
        const std::vector<entt::entity> divisions = FactionDivisions(world, f);
        int engaged = 0;
        for (const entt::entity entity : divisions)
        {
            engaged += world.time - world.registry.get<Division>(entity).lastCombatTime < 1.0f ? 1 : 0;
        }
        state.munitions = std::max(0.0f, state.munitions - static_cast<float>(engaged) * c.combatMunitions);
        state.upkeep = UpkeepPerSecond(static_cast<int>(divisions.size()), c);
        float& oil = state.stock[ToIndex(Resource::Oil)];
        const bool shortage = oil < state.upkeep;
        oil = std::max(0.0f, oil - state.upkeep);
        if (shortage && !state.fuelShortage)
        {
            FactionTrouble(world, f, "fuel shortage");
        }
        state.fuelShortage = shortage;

        // 징집 압력: 높으면 탈영, 낮으면 군사질 회복
        state.draftPressure = std::max(0.0f, state.draftPressure - c.pressureDecay);
        const float over = state.draftPressure - c.pressureSafe;
        if (over > 0.0f && Roll(world, over * c.desertionChance))
        {
            state.manpower *= std::uniform_real_distribution<float>(0.9f, 0.95f)(world.rng);
            const entt::entity victim = RandomDivision(world, divisions);
            if (victim != entt::null)
            {
                Division& division = world.registry.get<Division>(victim);
                division.hp = std::max(1.0f, division.hp - division.maxHp * 0.1f);
            }
            FactionTrouble(world, f, "desertion");
        }
        if (state.draftPressure < c.pressureSafe * 0.6f)
        {
            state.quality = std::min(1.0f, state.quality + c.qualityRecover);
        }

        // 과도한 병력: 예측불가 인력·지휘통제 사고
        const int excess = static_cast<int>(divisions.size()) - c.overstretchDivisions;
        if (excess > 0 && Roll(world, static_cast<float>(excess) * c.commandChance))
        {
            const entt::entity lost = RandomDivision(world, divisions);
            if (Roll(world, 0.5f) && lost != entt::null && world.registry.all_of<MovePath>(lost))
            {
                world.registry.remove<MovePath>(lost);
                FactionTrouble(world, f, "command breakdown, " + world.registry.get<Division>(lost).name + " lost its orders");
            }
            else
            {
                state.manpower *= 0.95f;
                FactionTrouble(world, f, "manpower crisis");
            }
        }
    }

    void UpdateProduction(World& world, const GameData& data, int f, float dt)
    {
        const GameConfig& c = data.config;
        FactionState& state = world.factions[static_cast<std::size_t>(f)];

        float penalty = 1.0f;
        Vec2 spawn{};
        bool hasSpawn = false;
        bool spawnIsHq = false;
        for (const auto [entity, base, owner, position] : world.registry.view<Base, Faction, Position>().each())
        {
            const BaseDef& def = data.bases[ToIndex(base.type)];
            if (owner.id == f && !spawnIsHq)
            {
                spawn = position.value;
                hasSpawn = true;
                spawnIsHq = base.type == BaseType::Hq;
            }
            if (base.originalOwner == f && owner.id != f && base.type != BaseType::Hq)
            {
                penalty *= def.penalty;
            }
        }

        if (state.queue.empty())
        {
            state.productionSpeed = penalty;
            return;
        }

        // 필요한 공장 종류 중 가장 적은 쪽이 속도를 정한다. 공장을 잃으면 멈춘다.
        const TemplateDef& front = state.templates[static_cast<std::size_t>(state.queue.front())];
        float factorySpeed = 2.0f;
        for (const int type : GetTemplateNeeds(front, data).factories)
        {
            const int count = ActiveFactories(state, type);
            factorySpeed = std::min(factorySpeed, count == 0 ? 0.0f : 1.0f + 0.5f * static_cast<float>(count - 1));
        }
        state.productionSpeed = penalty * factorySpeed;

        state.queueProgress = std::min(1.0f, state.queueProgress + dt * state.productionSpeed / c.productionSeconds);
        if (state.queueProgress >= 1.0f && hasSpawn)
        {
            const TemplateDef templ = front;
            state.queue.erase(state.queue.begin());
            state.queueProgress = 0.0f;
            SpawnDivision(world, data, f, templ, spawn);
            if (f == PlayerFaction && !state.aiControlled)
            {
                PushEvent(world, templ.name + " is ready", EventKind::Info);
            }
        }
    }
}

void PlaceMines(World& world, const GameData& data)
{
    world.mines.clear();
    const HexMap& map = world.map;
    if (data.mineRules.empty())
    {
        return;
    }

    // 왼쪽 절반에 놓고 좌우 대칭으로 복사해 양쪽이 공평하게 한다.
    std::vector<char> used(map.hexes.size(), 0);
    std::vector<int> candidates;
    for (int row = 0; row < map.rows; ++row)
    {
        for (int col = 0; col < map.cols / 2; ++col)
        {
            const int left = HexIndex(map, { col, row });
            const int right = HexIndex(map, { map.cols - 1 - col, row });
            const bool land = map.hexes[static_cast<std::size_t>(left)].terrain != Terrain::Sea && map.hexes[static_cast<std::size_t>(right)].terrain != Terrain::Sea;
            if (land && world.baseAtHex[static_cast<std::size_t>(left)] == entt::null && world.baseAtHex[static_cast<std::size_t>(right)] == entt::null)
            {
                candidates.push_back(left);
            }
        }
    }
    if (candidates.empty())
    {
        return;
    }

    float totalWeight = 0.0f;
    for (const MineRuleDef& rule : data.mineRules)
    {
        totalWeight += rule.weight;
    }
    std::uniform_int_distribution<std::size_t> pickHex(0, candidates.size() - 1);
    std::uniform_real_distribution<float> pickWeight(0.0f, totalWeight);

    for (int n = 0; n < data.config.minesPerSide; ++n)
    {
        // 처음에는 자원마다 하나씩 보장, 그 뒤로는 가중치(희귀도)대로.
        std::size_t ruleIndex = static_cast<std::size_t>(n);
        if (ruleIndex >= data.mineRules.size())
        {
            float roll = pickWeight(world.rng);
            ruleIndex = 0;
            while (ruleIndex + 1 < data.mineRules.size() && roll > data.mineRules[ruleIndex].weight)
            {
                roll -= data.mineRules[ruleIndex].weight;
                ++ruleIndex;
            }
        }
        const MineRuleDef& rule = data.mineRules[ruleIndex];

        int hex = -1;
        for (int attempt = 0; attempt < 400 && hex < 0; ++attempt)
        {
            const int candidate = candidates[pickHex(world.rng)];
            const unsigned terrain = static_cast<unsigned>(map.hexes[static_cast<std::size_t>(candidate)].terrain);
            const bool fits = (rule.terrainMask & (1u << terrain)) != 0 || attempt >= 300;   // 맞는 지형이 없으면 아무 육지
            if (!used[static_cast<std::size_t>(candidate)] && fits)
            {
                hex = candidate;
            }
        }
        if (hex < 0)
        {
            continue;
        }
        const HexCoord coord = IndexToHex(map, hex);
        const int mirror = HexIndex(map, { map.cols - 1 - coord.col, coord.row });
        used[static_cast<std::size_t>(hex)] = 1;
        used[static_cast<std::size_t>(mirror)] = 1;
        world.mines.push_back({ .hex = hex, .resource = rule.resource, .rate = rule.rate });
        world.mines.push_back({ .hex = mirror, .resource = rule.resource, .rate = rule.rate });
    }
}

void SetupFactionEconomy(World& world, const GameData& data, int faction)
{
    const GameConfig& c = data.config;
    FactionState& state = world.factions[static_cast<std::size_t>(faction)];
    state.stock = data.startStock;
    state.manpower = c.startManpower;
    state.munitions = c.startMunitions;
    state.quality = 1.0f;
    state.draftPressure = 0.0f;
    state.factories.clear();

    // 시작 공장은 HQ, 군단 기지, 사단 기지 순으로 나눠 둔다.
    std::vector<std::pair<BaseType, int>> bases;
    for (const auto [entity, base, owner] : world.registry.view<Base, Faction>().each())
    {
        if (owner.id == faction)
        {
            bases.push_back({ base.type, base.hex });
        }
    }
    std::sort(bases.begin(), bases.end());
    if (bases.empty())
    {
        return;
    }
    for (std::size_t i = 0; i < data.startFactories.size(); ++i)
    {
        state.factories.push_back({ .type = data.startFactories[i], .hex = bases[i % bases.size()].second, .daysLeft = 0.0f });
    }
}

void UpdateEconomy(World& world, const GameData& data, float dt)
{
    for (int f = 0; f < FactionCount; ++f)
    {
        FactionState& state = world.factions[static_cast<std::size_t>(f)];
        state.economyTimer += dt;
        while (state.economyTimer >= 1.0f)
        {
            state.economyTimer -= 1.0f;
            EconomyTick(world, data, f);
        }
        UpdateProduction(world, data, f, dt);
    }
}

std::string ProductionBlocker(const FactionState& state, const GameData& data, const TemplateDef& templ)
{
    const TemplateNeeds needs = GetTemplateNeeds(templ, data);
    for (const int type : needs.factories)
    {
        if (ActiveFactories(state, type) == 0)
        {
            return "needs " + data.factories[static_cast<std::size_t>(type)].name;
        }
    }
    for (std::size_t r = 0; r < ResourceCount; ++r)
    {
        if (state.stock[r] + 0.001f < needs.materials[r])
        {
            return std::string("not enough ") + ResourceNames[r];
        }
    }
    if (state.manpower < needs.manpower)
    {
        return "not enough manpower";
    }
    return {};
}

std::string FactoryBlocker(const World& world, const GameData& data, int faction, int type)
{
    if (type < 0 || type >= static_cast<int>(data.factories.size()))
    {
        return "unknown factory";
    }
    const FactionState& state = world.factions[static_cast<std::size_t>(faction)];
    const ResourceAmounts& cost = data.factories[static_cast<std::size_t>(type)].cost;
    for (std::size_t r = 0; r < ResourceCount; ++r)
    {
        if (state.stock[r] + 0.001f < cost[r])
        {
            return std::string("not enough ") + ResourceNames[r];
        }
    }
    return {};
}

void Conscript(World& world, const GameData& data, int faction)
{
    const GameConfig& c = data.config;
    FactionState& state = world.factions[static_cast<std::size_t>(faction)];
    float gain = c.conscriptManpower;
    const float over = state.draftPressure - c.pressureSafe;
    if (over > 0.0f)
    {
        if (Roll(world, std::min(0.95f, over * c.refusalChance)))
        {
            gain *= 0.2f;
            FactionTrouble(world, faction, "draft refusal");
        }
        state.quality = std::max(c.qualityMin, state.quality - c.qualityLoss);
        FactionTrouble(world, faction, "military quality dropped");
    }
    state.manpower += gain;
    state.draftPressure += c.conscriptPressure;
}

void StartFactory(World& world, const GameData& data, int faction, int type)
{
    if (!FactoryBlocker(world, data, faction, type).empty())
    {
        return;
    }
    FactionState& state = world.factions[static_cast<std::size_t>(faction)];

    // 공장이 가장 적은 아군 기지에 짓는다 (동률이면 HQ 우선): 한 기지를 잃어도 피해를 나눈다.
    int bestHex = -1;
    std::pair<int, int> best = { 1 << 30, 0 };
    for (const auto [entity, base, owner] : world.registry.view<Base, Faction>().each())
    {
        if (owner.id != faction)
        {
            continue;
        }
        const int count = static_cast<int>(std::count_if(state.factories.begin(), state.factories.end(), [&](const FactoryState& f) { return f.hex == base.hex; }));
        const std::pair<int, int> key = { count, static_cast<int>(base.type) };
        if (key < best)
        {
            best = key;
            bestHex = base.hex;
        }
    }
    if (bestHex < 0)
    {
        return;
    }
    const FactoryDef& def = data.factories[static_cast<std::size_t>(type)];
    Spend(state.stock, def.cost);
    state.factories.push_back({ .type = type, .hex = bestHex, .daysLeft = def.buildDays });
}

void DestroyFactoriesAt(World& world, int hex)
{
    for (int f = 0; f < FactionCount; ++f)
    {
        std::vector<FactoryState>& factories = world.factions[static_cast<std::size_t>(f)].factories;
        const std::size_t removed = std::erase_if(factories, [&](const FactoryState& factory) { return factory.hex == hex; });
        if (removed > 0)
        {
            FactionTrouble(world, f, std::to_string(removed) + " factories destroyed");
        }
    }
}

int ActiveFactories(const FactionState& state, int type)
{
    return static_cast<int>(std::count_if(state.factories.begin(), state.factories.end(), [&](const FactoryState& f) { return f.type == type && f.daysLeft <= 0.0f; }));
}

int FactoriesUnderConstruction(const FactionState& state, int type)
{
    return static_cast<int>(std::count_if(state.factories.begin(), state.factories.end(), [&](const FactoryState& f) { return f.type == type && f.daysLeft > 0.0f; }));
}

float UpkeepPerSecond(int divisions, const GameConfig& config)
{
    const float n = static_cast<float>(std::max(0, divisions));
    return n * config.upkeepOil * (1.0f + config.upkeepGrowth * n);
}

float FactionAttackMultiplier(const FactionState& state, const GameConfig& config)
{
    return state.quality
        * (state.munitions <= 0.0f ? config.noMunitionsAttack : 1.0f)
        * (state.fuelShortage ? config.noFuelAttack : 1.0f);
}

float FactionSpeedMultiplier(const FactionState& state, const GameConfig& config)
{
    return state.fuelShortage ? config.noFuelSpeed : 1.0f;
}

std::string GameDateText(float seconds, const GameConfig& config)
{
    const int days = static_cast<int>(seconds * config.daysPerSecond);
    const int year = config.startYear + days / 365;
    const int month = std::min(12, (days % 365) * 12 / 365 + 1);
    return std::format("{}-{:02}", year, month);
}
