#include "ai/deeplearn/rl_features.hpp"

#include "ai/ai.hpp"
#include "sim/components.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr float DistanceScaleKm = 200.0f;

    double FiniteOrZero(double value)
    {
        return std::isfinite(value) ? value : 0.0;
    }

    double Nonnegative(double value)
    {
        return std::max(0.0, FiniteOrZero(value));
    }

    float UnitRatio(double value, double scale)
    {
        scale = Nonnegative(scale);
        return scale > 0.0 ? static_cast<float>(std::min(1.0, Nonnegative(value) / scale)) : 0.0f;
    }

    float Squash(double value, double scale = 1.0)
    {
        value = Nonnegative(value);
        return static_cast<float>(value / (scale + value));
    }

    float Balance(double own, double enemy)
    {
        own = Nonnegative(own);
        enemy = Nonnegative(enemy);
        return static_cast<float>((own - enemy) / std::max(1.0, own + enemy));
    }

    float ScaledDistance(Vec2 from, Vec2 to)
    {
        const float km = Distance(from, to);
        return std::isfinite(km) ? Squash(km, DistanceScaleKm) : 1.0f;
    }
}

void BuildGlobalFeatures(const World& world, const GameData& data, const AiBlackboard& board, int self, RlGlobal& out)
{
    const FactionState& state = world.factions[static_cast<std::size_t>(self)];
    const FactionState& enemy = world.factions[static_cast<std::size_t>(1 - self)];

    int ownDivisions = 0;
    int enemyDivisions = 0;
    for (const auto [entity, division, faction] : world.registry.view<Division, Faction>().each())
    {
        ++(faction.id == self ? ownDivisions : enemyDivisions);
    }
    int ownBases = 0;
    for (const auto [entity, base, faction] : world.registry.view<Base, Faction>().each())
    {
        ownBases += faction.id == self;
    }

    out = {
        Balance(board.ownStrength, board.enemyStrength),
        Balance(state.ownedHexes, enemy.ownedHexes),
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,   // Resource enum order, filled below.
        Squash(state.manpower, std::max(1.0, Nonnegative(data.config.startManpower))),
        Squash(state.munitions, std::max(1.0, Nonnegative(data.config.startMunitions))),
        Squash(state.draftPressure),
        UnitRatio(state.quality, 1.0f),
        static_cast<float>(state.fuelShortage),
        Squash(static_cast<double>(state.factories.size()), 5.0),
        Squash(static_cast<double>(state.queue.size()), 2.0),
        UnitRatio(board.timeLeftRatio, 1.0f),
        static_cast<float>(board.posture == AiPosture::Hold),
        static_cast<float>(board.posture == AiPosture::Regroup),
        static_cast<float>(board.posture == AiPosture::Attack),
        Squash(static_cast<double>(board.threats.size()), 5.0),
        Squash(static_cast<double>(board.objectives.size()), 5.0),   // One objective per enemy base.
        Squash(ownDivisions, 10.0),
        Squash(enemyDivisions, 10.0),
        Squash(ownBases, 5.0),
    };
    static_assert(ResourceCount == 6);
    for (std::size_t i = 0; i < state.stock.size(); ++i)
    {
        out[2 + i] = Squash(std::log1p(Nonnegative(state.stock[i])), 5.0);
    }
}

void BuildCandidateFeatures(const World& world, const GameData& data, const AiBlackboard&, int self, entt::entity division,
                            const RlGlobal& global, const std::vector<AiDecision>& candidates, std::vector<RlInput>& out)
{
    out.resize(candidates.size());
    if (candidates.empty())
    {
        return;
    }

    RlInput shared{};
    std::transform(global.begin(), global.end(), shared.begin(), [](float value)
    {
        return static_cast<float>(FiniteOrZero(value));
    });

    const Division* unit = world.registry.try_get<Division>(division);
    const Position* position = world.registry.try_get<Position>(division);
    const MovePath* path = world.registry.try_get<MovePath>(division);
    const bool moving = path != nullptr && !path->hexes.empty();
    const bool hasPosition = position != nullptr && std::isfinite(position->value.x) && std::isfinite(position->value.y);
    int goal = -1;
    if (moving || (hasPosition && world.map.cols > 0 && world.map.rows > 0
                   && std::isfinite(world.map.hexDistance) && world.map.hexDistance > 0.0f))
    {
        goal = CurrentGoal(world, division);
    }

    if (unit != nullptr)
    {
        float hqDistance = 1.0f;
        float enemyDistance = 1.0f;
        if (hasPosition)
        {
            for (const auto [entity, base, faction, target] : world.registry.view<Base, Faction, Position>().each())
            {
                if (faction.id == self && base.type == BaseType::Hq)
                {
                    hqDistance = std::min(hqDistance, ScaledDistance(position->value, target.value));
                }
            }
            for (const auto [entity, enemy, faction, target] : world.registry.view<Division, Faction, Position>().each())
            {
                if (faction.id != self)
                {
                    enemyDistance = std::min(enemyDistance, ScaledDistance(position->value, target.value));
                }
            }
        }
        const double sinceCombat = static_cast<double>(world.time) - unit->lastCombatTime;
        const std::array<float, RlDivisionFeatures> divisionFeatures{
            UnitRatio(unit->hp, unit->maxHp),
            Squash(unit->maxHp, 400.0),
            Squash(unit->speed, 2.0),
            UnitRatio(unit->frontageKm, data.config.frontageMaxKm),
            static_cast<float>(world.registry.all_of<Retreating>(division)),
            static_cast<float>(sinceCombat >= 0.0 && sinceCombat < 10.0),
            hqDistance,
            enemyDistance,
            UnitRatio(static_cast<double>(unit->units.size()), data.config.maxBattalions),
            Squash(unit->defenseAvg, 10.0),
            static_cast<float>(moving),
            static_cast<float>(unit->hasTarget),
        };
        std::copy(divisionFeatures.begin(), divisionFeatures.end(), shared.begin() + RlGlobalFeatures);
    }

    const std::size_t best = BestCandidate(candidates);
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        out[i] = shared;
        float* features = out[i].data() + RlGlobalFeatures + RlDivisionFeatures;
        const AiDecision& candidate = candidates[i];
        const std::size_t action = ToIndex(candidate.action);
        if (action < 6)
        {
            features[action] = 1.0f;
        }
        if (candidate.action != AiAction::Hold && candidate.hex >= 0 && static_cast<std::size_t>(candidate.hex) < world.map.hexes.size())
        {
            const Hex& target = world.map.hexes[static_cast<std::size_t>(candidate.hex)];
            if (hasPosition && world.map.cols > 0)
            {
                const Vec2 center = HexCenter(IndexToHex(world.map, candidate.hex), world.map.hexDistance);
                features[6] = ScaledDistance(position->value, center);
            }
            features[7] = static_cast<float>(candidate.hex == goal);
            features[8] = static_cast<float>(target.owner == self);
            features[9] = static_cast<float>(target.owner >= 0 && target.owner != self);
            features[10] = static_cast<float>(target.owner < 0);
            const std::size_t terrain = ToIndex(target.terrain);
            if (terrain < data.terrain.size())
            {
                features[11] = Squash(data.terrain[terrain].defense);
            }
        }
        features[12] = static_cast<float>(candidate.threatIndex >= 0);
        features[13] = static_cast<float>(std::clamp(FiniteOrZero(candidate.score), -1.0, 1.0));
        features[14] = static_cast<float>(i == best);
    }
}

RlRewardSnapshot TakeRewardSnapshot(const World& world, int self)
{
    RlRewardSnapshot snapshot;
    snapshot.ownHexes = world.factions[static_cast<std::size_t>(self)].ownedHexes;
    snapshot.enemyHexes = world.factions[static_cast<std::size_t>(1 - self)].ownedHexes;

    double ownHp = 0.0;
    double enemyHp = 0.0;
    for (const auto [entity, division, faction] : world.registry.view<Division, Faction>().each())
    {
        (faction.id == self ? ownHp : enemyHp) += Nonnegative(division.hp);
    }
    snapshot.ownHp = static_cast<float>(std::min(ownHp, static_cast<double>(std::numeric_limits<float>::max())));
    snapshot.enemyHp = static_cast<float>(std::min(enemyHp, static_cast<double>(std::numeric_limits<float>::max())));

    for (const auto [entity, base, faction] : world.registry.view<Base, Faction>().each())
    {
        ++(faction.id == self ? snapshot.ownBases : snapshot.enemyBases);
    }
    return snapshot;
}

float ComputeReward(const RlRewardSnapshot& before, const RlRewardSnapshot& after, const World& world, int self)
{
    const double hexDelta = (static_cast<double>(after.ownHexes) - before.ownHexes)
        - (static_cast<double>(after.enemyHexes) - before.enemyHexes);
    const double mapSize = static_cast<double>(std::max<std::size_t>(1, world.map.hexes.size()));

    const double ownHpBefore = Nonnegative(before.ownHp);
    const double enemyHpBefore = Nonnegative(before.enemyHp);
    const double ownHpAfter = Nonnegative(after.ownHp);
    const double enemyHpAfter = Nonnegative(after.enemyHp);
    double maxHp = 0.0;
    for (const auto [entity, division, faction] : world.registry.view<Division, Faction>().each())
    {
        maxHp += Nonnegative(division.maxHp);
    }
    // Include prior HP in the scale because destroyed divisions are gone from the registry.
    const double hpScale = std::max({ 1.0, maxHp, ownHpBefore + enemyHpBefore, ownHpAfter + enemyHpAfter });
    const double hpDelta = (enemyHpBefore - enemyHpAfter) - (ownHpBefore - ownHpAfter);
    const double baseDelta = (static_cast<double>(after.ownBases) - before.ownBases)
        - (static_cast<double>(after.enemyBases) - before.enemyBases);

    const double terminal = world.over ? (world.winner == self ? 5.0 : -5.0) : 0.0;
    return static_cast<float>(std::clamp(hexDelta * 10.0 / mapSize + hpDelta / hpScale + baseDelta + terminal, -10.0, 10.0));
}
