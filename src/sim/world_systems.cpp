#include "sim/systems.hpp"
#include "sim/economy.hpp"
#include "sim/components.hpp"
#include "sim/rules.hpp"
#include "sim/simulation.hpp"

#include <algorithm>
#include <limits>

int HexAt(const World& world, Vec2 position)
{
    HexCoord coord = WorldToHex(position, world.map.hexDistance);
    coord.col = std::clamp(coord.col, 0, world.map.cols - 1);
    coord.row = std::clamp(coord.row, 0, world.map.rows - 1);
    return HexIndex(world.map, coord);
}

Weather WeatherAtHex(const World& world, int hex)
{
    const int region = world.map.hexes[static_cast<std::size_t>(hex)].weatherRegion;
    return world.weather[static_cast<std::size_t>(region)].weather;
}

void RebuildOccupancy(World& world)
{
    std::fill(world.occupancy.begin(), world.occupancy.end(), std::uint8_t{ 0 });
    for (const auto [entity, division, position, faction] : world.registry.view<Division, Position, Faction>().each())
    {
        world.occupancy[static_cast<std::size_t>(HexAt(world, position.value))] |= static_cast<std::uint8_t>(1u << faction.id);
    }
}

bool IsHexBlockedFor(const World& world, int hex, int faction)
{
    const std::uint8_t enemies = static_cast<std::uint8_t>(world.occupancy[static_cast<std::size_t>(hex)] & ~(1u << faction));
    if (enemies != 0)
    {
        return true;
    }
    const entt::entity base = world.baseAtHex[static_cast<std::size_t>(hex)];
    return base != entt::null && world.registry.get<Faction>(base).id != faction;
}

float DivisionSupply(const World& world, const GameData& data, Vec2 position, int faction, std::uint32_t effects)
{
    float nearest = std::numeric_limits<float>::max();
    for (const auto [entity, base, owner, basePosition] : world.registry.view<Base, Faction, Position>().each())
    {
        if (owner.id == faction)
        {
            nearest = std::min(nearest, Distance(position, basePosition.value));
        }
    }
    return SupplyFactor(nearest, HasEffect(effects, Effect::Transport), data.config);
}

void RollWeather(World& world, const GameData& data, WeatherRegion& region)
{
    std::array<float, WeatherCount> weights{};
    for (std::size_t i = 0; i < WeatherCount; ++i)
    {
        weights[i] = data.weather[i].weight;
    }
    std::discrete_distribution<int> pick(weights.begin(), weights.end());
    std::uniform_real_distribution<float> duration(data.config.weatherMinSeconds, data.config.weatherMaxSeconds);
    region.weather = static_cast<Weather>(pick(world.rng));
    region.timer = duration(world.rng);
}

void UpdateWeather(World& world, const GameData& data, float dt)
{
    for (WeatherRegion& region : world.weather)
    {
        region.timer -= dt;
        if (region.timer <= 0.0f)
        {
            RollWeather(world, data, region);
        }
    }
}

void UpdateMovement(World& world, const GameData& data, float dt)
{
    // 교전 판정용 위치 목록 (사단 + 기지)
    std::vector<std::pair<Vec2, int>> presence;
    for (const auto [entity, position, faction] : world.registry.view<Position, Faction>().each())
    {
        presence.push_back({ position.value, faction.id });
    }

    std::vector<entt::entity> finished;
    auto view = world.registry.view<Division, Position, Faction, MovePath>();
    for (const auto [entity, division, position, faction, path] : view.each())
    {
        if (path.hexes.empty())
        {
            finished.push_back(entity);
            continue;
        }

        // 적이 교전 반경 안이면 정지해서 싸운다. 경로는 유지해서 적이 사라지면 다시 간다.
        if (!world.registry.all_of<Retreating>(entity))
        {
            const float engageRange = division.frontageKm + 0.5f;
            const bool engaged = std::any_of(presence.begin(), presence.end(), [&](const auto& other)
            {
                return other.second != faction.id && Distance(other.first, position.value) <= engageRange;
            });
            if (engaged)
            {
                continue;
            }
        }

        const int next = path.hexes.front();
        if (IsHexBlockedFor(world, next, faction.id))
        {
            finished.push_back(entity);   // 적이 있는 헥스 앞에서 멈추고 교전한다.
            continue;
        }

        const int current = HexAt(world, position.value);
        const Hex& hex = world.map.hexes[static_cast<std::size_t>(current)];
        const WeatherDef& weather = data.weather[ToIndex(WeatherAtHex(world, current))];
        const float speed = division.speed
            * FactionSpeedMultiplier(world.factions[static_cast<std::size_t>(faction.id)], data.config)
            * std::max(0.1f, TerrainMoveMultiplier(hex.terrain, division.effects, data))
            * RoadSpeedMultiplier(hex.road, data.config)
            * WeatherMultiplier(weather.move, division.effects);

        const Vec2 target = HexCenter(IndexToHex(world.map, next), world.map.hexDistance);
        const float distance = Distance(position.value, target);
        const float step = speed * dt;
        if (distance <= step)
        {
            position.value = target;
            path.hexes.erase(path.hexes.begin());
        }
        else
        {
            position.value.x += (target.x - position.value.x) / distance * step;
            position.value.y += (target.y - position.value.y) / distance * step;
        }
    }

    for (const entt::entity entity : finished)
    {
        world.registry.remove<MovePath>(entity);
        world.registry.remove<Retreating>(entity);
    }
}

void UpdateTerritory(World& world, const GameData& data)
{
    const std::size_t hexCount = world.map.hexes.size();
    std::vector<std::uint8_t> claims(hexCount, 0);

    std::vector<CombatTarget> all;
    for (const auto [entity, division, position, faction] : world.registry.view<Division, Position, Faction>().each())
    {
        all.push_back({ entity, faction.id, position.value, HexAt(world, position.value), false });
    }

    for (const CombatTarget& unit : all)
    {
        if (world.registry.all_of<Retreating>(unit.entity))
        {
            continue;
        }
        const std::uint8_t bit = static_cast<std::uint8_t>(1u << unit.faction);
        claims[static_cast<std::size_t>(unit.hex)] |= bit;

        const bool enemyNear = std::any_of(all.begin(), all.end(), [&](const CombatTarget& other)
        {
            return other.faction != unit.faction && Distance(other.position, unit.position) <= data.config.territoryClearKm;
        });
        if (enemyNear)
        {
            continue;
        }
        for (const HexCoord nb : HexNeighbors(IndexToHex(world.map, unit.hex)))
        {
            if (InBounds(world.map, nb) && IsPassable(world.map, data, HexIndex(world.map, nb)))
            {
                claims[static_cast<std::size_t>(HexIndex(world.map, nb))] |= bit;
            }
        }
    }

    for (FactionState& state : world.factions)
    {
        state.ownedHexes = 0;
    }

    for (std::size_t i = 0; i < hexCount; ++i)
    {
        Hex& hex = world.map.hexes[i];
        const entt::entity base = world.baseAtHex[i];
        if (base != entt::null)
        {
            hex.owner = world.registry.get<Faction>(base).id;   // 기지 헥스는 기지 소유를 따른다.
        }
        else if (claims[i] == 1u)
        {
            hex.owner = PlayerFaction;
        }
        else if (claims[i] == 2u)
        {
            hex.owner = AiFaction;
        }
        // 3이면 양측 동시 요구 → 보류

        if (hex.owner >= 0)
        {
            ++world.factions[static_cast<std::size_t>(hex.owner)].ownedHexes;
        }
    }
}

void CheckVictory(World& world, const GameData& data)
{
    (void)data;
    if (world.over || world.time < world.matchSeconds)
    {
        return;
    }
    const int blue = world.factions[PlayerFaction].ownedHexes;
    const int red = world.factions[AiFaction].ownedHexes;
    world.over = true;
    world.winner = blue > red ? PlayerFaction : AiFaction;   // 동률은 적군(AI) 승리
    world.endReason = "Time up - territory " + world.factions[PlayerFaction].name + " " + std::to_string(blue)
        + " vs " + world.factions[AiFaction].name + " " + std::to_string(red) + " hexes";
}
