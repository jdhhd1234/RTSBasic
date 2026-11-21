#include "sim/systems.hpp"
#include "sim/economy.hpp"
#include "sim/components.hpp"
#include "sim/rules.hpp"
#include "sim/simulation.hpp"

#include <algorithm>
#include <limits>
#include <random>
#include <unordered_map>

namespace
{
    // 인접 헥스 중심 거리는 정확히 20km라서 부동소수 오차를 흡수한다.
    constexpr float RangeTolerance = 0.5f;

    float TargetDefense(const World& world, const GameData& data, const CombatTarget& target)
    {
        const float terrain = data.terrain[ToIndex(world.map.hexes[static_cast<std::size_t>(target.hex)].terrain)].defense;
        if (target.isBase)
        {
            const Base& base = world.registry.get<Base>(target.entity);
            return data.bases[ToIndex(base.type)].defense * terrain;
        }
        const Division& division = world.registry.get<Division>(target.entity);
        float defense = division.defenseAvg * terrain;
        if (HasEffect(division.effects, Effect::Signals))
        {
            defense *= 1.0f + data.config.signalsBonus;
        }
        return defense;
    }

    const CombatTarget* NearestEnemy(const std::vector<CombatTarget>& targets, int faction, Vec2 position, float range, bool divisionsOnly)
    {
        const CombatTarget* best = nullptr;
        float bestDistance = range + RangeTolerance;
        for (const CombatTarget& target : targets)
        {
            if (target.faction == faction || (divisionsOnly && target.isBase))
            {
                continue;
            }
            const float distance = Distance(position, target.position);
            if (distance <= bestDistance)
            {
                bestDistance = distance;
                best = &target;
            }
        }
        return best;
    }

    float DefenseScale(const GameConfig& c, float defense)
    {
        return c.defenseK / (c.defenseK + defense);
    }
}

void UpdateCombat(World& world, const GameData& data, float dt)
{
    const GameConfig& c = data.config;

    std::vector<CombatTarget> targets;
    for (const auto [entity, division, position, faction] : world.registry.view<Division, Position, Faction>().each())
    {
        targets.push_back({ entity, faction.id, position.value, HexAt(world, position.value), false });
    }
    for (const auto [entity, base, position, faction] : world.registry.view<Base, Position, Faction>().each())
    {
        targets.push_back({ entity, faction.id, position.value, base.hex, true });
    }

    std::unordered_map<entt::entity, float> damage;
    std::unordered_map<entt::entity, int> attackerFaction;

    for (const auto [entity, division] : world.registry.view<Division>().each())
    {
        division.hasTarget = false;
    }

    // 사단 공격: 대대마다 사거리 안의 가장 가까운 적을 쏜다.
    auto attackers = world.registry.view<Division, Position, Faction>(entt::exclude<Retreating>);
    for (const auto [entity, division, position, faction] : attackers.each())
    {
        const int hex = HexAt(world, position.value);
        const Terrain terrain = world.map.hexes[static_cast<std::size_t>(hex)].terrain;
        const WeatherDef& weather = data.weather[ToIndex(WeatherAtHex(world, hex))];
        std::uniform_real_distribution<float> variance(-weather.variance, weather.variance);

        float divisionMult = FactionAttackMultiplier(world.factions[static_cast<std::size_t>(faction.id)], c)
            * DivisionSupply(world, data, position.value, faction.id, division.effects)
            * WeatherMultiplier(weather.attack, division.effects)
            * HpFactor(division.hp, division.maxHp, c)
            * (1.0f + variance(world.rng));
        if (HasEffect(division.effects, Effect::Recon))
        {
            divisionMult *= 1.0f + c.reconAttack;
        }
        if (HasEffect(division.effects, Effect::Signals))
        {
            divisionMult *= 1.0f + c.signalsBonus;
        }
        if (HasEffect(division.effects, Effect::Mountain) && (terrain == Terrain::Mountain || terrain == Terrain::Plateau))
        {
            divisionMult *= 1.0f + c.mountainAttack;
        }

        for (const int b : division.battalions)
        {
            const BattalionDef& def = data.battalions[static_cast<std::size_t>(b)];
            if (def.attack <= 0.0f)
            {
                continue;
            }
            const float range = std::max(c.engageKm, def.range * WeatherMultiplier(weather.range, division.effects));
            const CombatTarget* target = NearestEnemy(targets, faction.id, position.value, range, false);
            if (target == nullptr)
            {
                continue;
            }

            float mult = divisionMult;
            if (def.unitClass == UnitClass::Armor || def.unitClass == UnitClass::Mech)
            {
                mult *= data.terrain[ToIndex(terrain)].armorAttack;
            }
            if (!target->isBase)
            {
                const Division& targetDivision = world.registry.get<Division>(target->entity);
                mult *= MatchupAgainst(def.unitClass, targetDivision.battalions, data);
                if (HasEffect(targetDivision.effects, Effect::Ew))
                {
                    mult *= 1.0f - c.ewPenalty;
                }
            }

            damage[target->entity] += def.attack * mult * c.damageScale * DefenseScale(c, TargetDefense(world, data, *target)) * dt;
            attackerFaction[target->entity] = faction.id;
            division.lastCombatTime = world.time;
            if (!division.hasTarget)
            {
                division.hasTarget = true;
                division.targetPosition = target->position;
            }
        }
    }

    // 기지 반격: 교전 반경 안의 가장 가까운 적 사단.
    for (const auto [entity, base, position, faction] : world.registry.view<Base, Position, Faction>().each())
    {
        const CombatTarget* target = NearestEnemy(targets, faction.id, position.value, c.engageKm, true);
        if (target == nullptr)
        {
            continue;
        }
        const float attack = data.bases[ToIndex(base.type)].attack;
        damage[target->entity] += attack * c.damageScale * DefenseScale(c, TargetDefense(world, data, *target)) * dt;
        attackerFaction[target->entity] = faction.id;
    }

    std::vector<entt::entity> destroyed;
    std::vector<entt::entity> retreat;
    for (const auto& [entity, amount] : damage)
    {
        if (Division* division = world.registry.try_get<Division>(entity))
        {
            division->hp -= amount;
            division->lastCombatTime = world.time;
            if (division->hp <= 0.0f)
            {
                destroyed.push_back(entity);
                const int owner = world.registry.get<Faction>(entity).id;
                PushEvent(world, division->name + " (" + world.factions[static_cast<std::size_t>(owner)].name + ") was destroyed",
                          owner == PlayerFaction ? EventKind::Bad : EventKind::Good);
            }
            else if (!world.registry.all_of<Retreating>(entity) && division->hp < RetreatThreshold(division->effects, c) * division->maxHp)
            {
                retreat.push_back(entity);
            }
        }
        else if (Base* base = world.registry.try_get<Base>(entity))
        {
            base->hp -= amount;
            if (base->hp <= 0.0f)
            {
                CaptureBase(world, data, entity, attackerFaction[entity]);
            }
        }
    }

    for (const entt::entity entity : destroyed)
    {
        world.registry.destroy(entity);
    }
    for (const entt::entity entity : retreat)
    {
        StartRetreat(world, data, entity);
    }

    // 기지: 교전 반경 안에 적 사단이 없고 이번 틱 피해가 없으면 회복
    for (const auto [entity, base, position, faction] : world.registry.view<Base, Position, Faction>().each())
    {
        if (damage.contains(entity) || NearestEnemy(targets, faction.id, position.value, c.engageKm, true) != nullptr)
        {
            continue;
        }
        base.hp = std::min(base.maxHp, base.hp + c.baseRegen * dt);
    }

    // 의무·정비: 교전이 없을 때 회복. regen: 교전 중에도 회복
    for (const auto [entity, division] : world.registry.view<Division>().each())
    {
        if (HasEffect(division.effects, Effect::Regeneration))
        {
            division.hp = std::min(division.maxHp, division.hp + division.maxHp * c.regenRate * dt);
        }
        if (world.time - division.lastCombatTime < c.recoverIdleSeconds)
        {
            continue;
        }
        float rate = 0.0f;
        if (HasEffect(division.effects, Effect::Medical))
        {
            rate += c.medicalRate;
        }
        if (HasEffect(division.effects, Effect::Maintenance))
        {
            rate += c.maintenanceRate;
        }
        division.hp = std::min(division.maxHp, division.hp + division.maxHp * rate * dt);
    }
}

void StartRetreat(World& world, const GameData& data, entt::entity entity)
{
    if (!world.registry.valid(entity) || !world.registry.all_of<Division>(entity))
    {
        return;
    }
    const Vec2 position = world.registry.get<Position>(entity).value;
    const int faction = world.registry.get<Faction>(entity).id;

    int bestHex = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (const auto [baseEntity, base, owner, basePosition] : world.registry.view<Base, Faction, Position>().each())
    {
        const float distance = Distance(position, basePosition.value);
        if (owner.id == faction && distance < bestDistance)
        {
            bestDistance = distance;
            bestHex = base.hex;
        }
    }
    if (bestHex < 0)
    {
        return;
    }

    std::vector<int> path = FindPath(world.map, data, HexAt(world, position), bestHex);
    if (path.empty())
    {
        return;
    }
    world.registry.emplace_or_replace<MovePath>(entity, std::move(path));
    world.registry.emplace_or_replace<Retreating>(entity);
}

void CaptureBase(World& world, const GameData& data, entt::entity entity, int captor)
{
    Base& base = world.registry.get<Base>(entity);
    DestroyFactoriesAt(world, base.hex);
    world.registry.get<Faction>(entity).id = captor;
    base.hp = base.maxHp * data.config.capturedBaseHp;

    const std::string& captorName = world.factions[static_cast<std::size_t>(captor)].name;
    const bool recaptured = base.originalOwner == captor;
    PushEvent(world, base.name + (recaptured ? " was recaptured by " : " was captured by ") + captorName,
              captor == PlayerFaction ? EventKind::Good : EventKind::Bad);

    const HexCoord center = IndexToHex(world.map, base.hex);
    for (int i = 0; i < static_cast<int>(world.map.hexes.size()); ++i)
    {
        if (IsPassable(world.map, data, i) && HexDistance(center, IndexToHex(world.map, i)) <= data.config.baseCaptureRadius)
        {
            world.map.hexes[static_cast<std::size_t>(i)].owner = captor;
        }
    }

    if (base.type == BaseType::Hq)
    {
        world.over = true;
        world.winner = captor;
        world.endReason = base.name + " captured by " + captorName;
    }
}
