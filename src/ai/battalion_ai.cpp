#include "ai/ai.hpp"
#include "sim/components.hpp"
#include "sim/economy.hpp"
#include "sim/map.hpp"
#include "sim/rules.hpp"
#include "sim/systems.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

// 대대 AI: 대대마다 1초에 한 번 임무와 목표 위치를 고르고, 매 틱 그쪽으로 움직인다.
// 행동 범위는 사단 위치에서 담당 전선(frontageKm) 반경 안. 사단이 움직이면 범위도 같이 움직인다.

namespace
{
    constexpr float ThinkSeconds = 1.0f;
    constexpr float RearKm = 6.0f;              // 지원 대대가 배치선 뒤로 물러나는 거리
    constexpr float StandoffRatio = 0.7f;       // 교전 시 적과의 거리 = 사거리 × 비율
    constexpr float ArtilleryRatio = 0.85f;
    constexpr float CoverHpRatio = 0.4f;        // 사단 hp가 이보다 낮으면 교전 대신 엄폐

    Vec2 Add(Vec2 a, Vec2 b, float scale)
    {
        return { a.x + b.x * scale, a.y + b.y * scale };
    }

    Vec2 Direction(Vec2 from, Vec2 to)
    {
        const float length = Distance(from, to);
        return length > 0.0f ? Vec2{ (to.x - from.x) / length, (to.y - from.y) / length } : Vec2{ 0.0f, 0.0f };
    }

    Vec2 ClampToZone(Vec2 point, Vec2 center, float radius)
    {
        const float distance = Distance(point, center);
        return distance <= radius ? point : Add(center, Direction(center, point), radius);
    }

    // i번째 대대의 배치 슬롯: 바라보는 방향에 수직인 담당 전선 위에 고르게.
    Vec2 Slot(Vec2 center, Vec2 facing, float frontage, std::size_t index, std::size_t count)
    {
        const Vec2 axis = { -facing.y, facing.x };
        const float offset = ((static_cast<float>(index) + 0.5f) / static_cast<float>(count) - 0.5f) * frontage;
        return Add(center, axis, offset);
    }

    bool PassableAt(const World& world, const GameData& data, Vec2 point)
    {
        const HexCoord coord = WorldToHex(point, world.map.hexDistance);
        return InBounds(world.map, coord) && IsPassable(world.map, data, HexIndex(world.map, coord));
    }

    // 슬롯 헥스와 이웃 중 방어 보정이 가장 좋은 헥스 (같으면 슬롯 쪽).
    // 슬롯이 헥스 중심에서 떨어진 만큼 그대로 옮겨서 대대들이 한 점에 겹치지 않게 한다.
    Vec2 BestCover(const World& world, const GameData& data, Vec2 slot)
    {
        const HexCoord home = WorldToHex(slot, world.map.hexDistance);
        const Vec2 homeCenter = HexCenter(home, world.map.hexDistance);
        const Vec2 offset = { slot.x - homeCenter.x, slot.y - homeCenter.y };
        Vec2 best = slot;
        float bestScore = -std::numeric_limits<float>::infinity();
        const auto consider = [&](HexCoord coord)
        {
            if (!InBounds(world.map, coord))
            {
                return;
            }
            const int index = HexIndex(world.map, coord);
            if (!IsPassable(world.map, data, index))
            {
                return;
            }
            const Vec2 center = HexCenter(coord, world.map.hexDistance);
            const float score = data.terrain[ToIndex(world.map.hexes[static_cast<std::size_t>(index)].terrain)].defense
                - Distance(center, homeCenter) * 0.01f;
            if (score > bestScore)
            {
                bestScore = score;
                best = { center.x + offset.x, center.y + offset.y };
            }
        };
        consider(home);
        for (const HexCoord nb : HexNeighbors(home))
        {
            consider(nb);
        }
        return best;
    }

    // 담당 범위 안에서 이 대대에 가장 가까운 적 대대.
    const BattalionUnit* NearestThreat(const std::vector<std::pair<int, const BattalionUnit*>>& enemies, int faction,
                                       Vec2 center, float radius, Vec2 from)
    {
        const BattalionUnit* best = nullptr;
        float bestDistance = std::numeric_limits<float>::infinity();
        for (const auto& [enemyFaction, unit] : enemies)
        {
            if (enemyFaction == faction || Distance(center, unit->position) > radius)
            {
                continue;
            }
            const float distance = Distance(from, unit->position);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = unit;
            }
        }
        return best;
    }

    void Think(const World& world, const GameData& data, const Division& division, Vec2 center, Vec2 facing, bool retreating,
               const std::vector<std::pair<int, const BattalionUnit*>>& enemies, int faction, std::size_t index, BattalionUnit& unit)
    {
        const BattalionDef& def = data.battalions[static_cast<std::size_t>(division.battalions[index])];
        const float radius = division.frontageKm;
        const Vec2 slot = Slot(center, facing, division.frontageKm, index, division.units.size());
        const BattalionUnit* threat = NearestThreat(enemies, faction, center, radius, unit.position);
        const float hpRatio = division.maxHp > 0.0f ? division.hp / division.maxHp : 0.0f;

        if (retreating)
        {
            unit.task = BattalionTask::Withdraw;
            unit.goal = center;
        }
        else if (def.attack <= 0.0f)
        {
            unit.task = BattalionTask::Hold;          // 비전투 지원: 배치선 뒤
            unit.goal = Add(slot, facing, -RearKm);
        }
        else if (def.unitClass == UnitClass::Arty)
        {
            unit.task = threat != nullptr ? BattalionTask::Support : BattalionTask::Hold;
            unit.goal = threat != nullptr
                ? Add(threat->position, Direction(threat->position, unit.position), def.range * ArtilleryRatio)
                : Add(slot, facing, -RearKm);
        }
        else if (threat != nullptr && hpRatio >= CoverHpRatio)
        {
            unit.task = BattalionTask::Engage;
            unit.goal = Add(threat->position, Direction(threat->position, unit.position), def.range * StandoffRatio);
        }
        else if (threat == nullptr && def.effect == Effect::Recon)
        {
            unit.task = BattalionTask::Screen;        // 정찰: 담당 범위 앞쪽
            unit.goal = Add(slot, facing, radius * 0.6f);
        }
        else
        {
            unit.task = BattalionTask::Cover;
            unit.goal = BestCover(world, data, threat != nullptr ? Add(slot, facing, -RearKm) : slot);
        }

        unit.goal = ClampToZone(unit.goal, center, radius);
        if (!PassableAt(world, data, unit.goal))
        {
            unit.goal = center;
        }
    }
}

Vec2 DivisionFacing(const World& world, Vec2 position, int faction)
{
    Vec2 facing = { MapWidthKm(world.map) / 2.0f - position.x, MapHeightKm(world.map) / 2.0f - position.y };
    float nearest = std::numeric_limits<float>::infinity();
    for (const auto [entity, division, enemy, enemyFaction] : world.registry.view<Division, Position, Faction>().each())
    {
        const float distance = Distance(position, enemy.value);
        if (enemyFaction.id != faction && distance > 0.0f && distance < nearest)
        {
            nearest = distance;
            facing = { enemy.value.x - position.x, enemy.value.y - position.y };
        }
    }
    const float length = std::hypot(facing.x, facing.y);
    return length > 0.0f ? Vec2{ facing.x / length, facing.y / length } : Vec2{ 1.0f, 0.0f };
}

void ResetBattalionPositions(World& world, entt::entity entity)
{
    Division& division = world.registry.get<Division>(entity);
    const Vec2 center = world.registry.get<Position>(entity).value;
    const Vec2 facing = DivisionFacing(world, center, world.registry.get<Faction>(entity).id);
    division.units.assign(division.battalions.size(), BattalionUnit{});
    for (std::size_t i = 0; i < division.units.size(); ++i)
    {
        BattalionUnit& unit = division.units[i];
        // 슬롯이 바다여도 첫 판단(Think)에서 지나갈 수 있는 목표로 바로잡는다.
        unit.position = Slot(center, facing, division.frontageKm, i, division.units.size());
        unit.goal = unit.position;
        unit.thinkTimer = ThinkSeconds * static_cast<float>(i) / static_cast<float>(division.units.size());   // 판단 시점 분산
    }
}

void UpdateBattalionAI(World& world, const GameData& data, float dt)
{
    // 적 대대 목록은 실제 대대를 가리키므로 같은 틱에 먼저 움직인 대대는 새 위치로 보인다.
    std::vector<std::pair<int, const BattalionUnit*>> enemies;
    for (const auto [entity, division, faction] : world.registry.view<Division, Faction>().each())
    {
        if (division.units.size() != division.battalions.size())
        {
            ResetBattalionPositions(world, entity);
        }
    }
    for (const auto [entity, division, faction] : world.registry.view<Division, Faction>().each())
    {
        for (const BattalionUnit& unit : division.units)
        {
            enemies.emplace_back(faction.id, &unit);
        }
    }

    for (const auto [entity, division, position, faction] : world.registry.view<Division, Position, Faction>().each())
    {
        const Vec2 center = position.value;
        const Vec2 facing = DivisionFacing(world, center, faction.id);
        const bool retreating = world.registry.all_of<Retreating>(entity);
        const float factionSpeed = FactionSpeedMultiplier(world.factions[static_cast<std::size_t>(faction.id)], data.config);

        for (std::size_t i = 0; i < division.units.size(); ++i)
        {
            BattalionUnit& unit = division.units[i];
            unit.thinkTimer -= dt;
            if (unit.thinkTimer <= 0.0f)
            {
                unit.thinkTimer += ThinkSeconds;
                Think(world, data, division, center, facing, retreating, enemies, faction.id, i, unit);
            }

            const BattalionDef& def = data.battalions[static_cast<std::size_t>(division.battalions[i])];
            const int hexIndex = HexAt(world, unit.position);
            const Hex& hex = world.map.hexes[static_cast<std::size_t>(hexIndex)];
            const float speed = def.speed * factionSpeed
                * std::max(0.1f, TerrainMoveMultiplier(hex.terrain, division.effects, data))
                * RoadSpeedMultiplier(hex.road, data.config)
                * WeatherMultiplier(data.weather[ToIndex(WeatherAtHex(world, hexIndex))].move, division.effects);
            const float distance = Distance(unit.position, unit.goal);
            const float step = speed * dt;
            unit.position = distance <= step ? unit.goal : Add(unit.position, Direction(unit.position, unit.goal), step);
            // 사단이 움직여 범위를 벗어났으면 경계로 끌어온다.
            unit.position = ClampToZone(unit.position, center, division.frontageKm);
        }
    }
}
