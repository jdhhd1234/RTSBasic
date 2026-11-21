#include "sim/simulation.hpp"
#include "sim/economy.hpp"
#include "ai/ai.hpp"
#include "sim/components.hpp"
#include "sim/rules.hpp"
#include "sim/systems.hpp"
#include "core/templates.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace
{
    constexpr std::size_t MaxEvents = 50;

    // 서쪽 기준 기본 배치 (맵 크기 비율). 적군은 좌우 대칭.
    std::vector<BasePlacement> DefaultBaseLayout(int cols, int rows)
    {
        const auto col = [cols](float fraction) { return static_cast<int>(fraction * static_cast<float>(cols)); };
        const std::array<std::pair<BaseType, HexCoord>, 7> west = { {
            { BaseType::Hq, { col(0.07f), rows / 2 } },
            { BaseType::Corps, { col(0.17f), rows / 4 } },
            { BaseType::Corps, { col(0.17f), rows * 3 / 4 } },
            { BaseType::Division, { col(0.27f), rows / 8 } },
            { BaseType::Division, { col(0.27f), rows * 3 / 8 } },
            { BaseType::Division, { col(0.27f), rows * 5 / 8 } },
            { BaseType::Division, { col(0.27f), rows * 7 / 8 } },
        } };

        std::vector<BasePlacement> result;
        for (int faction = 0; faction < FactionCount; ++faction)
        {
            for (const auto& [type, coord] : west)
            {
                const HexCoord placed = faction == PlayerFaction ? coord : HexCoord{ cols - 1 - coord.col, coord.row };
                result.push_back({ faction, type, placed });
            }
        }
        return result;
    }

    // desired에서 가장 가까운, 통행 가능하고 비어 있는 헥스.
    int FindFreeLand(const World& world, const GameData& data, HexCoord desired)
    {
        int best = -1;
        int bestDistance = std::numeric_limits<int>::max();
        for (int i = 0; i < static_cast<int>(world.map.hexes.size()); ++i)
        {
            if (!IsPassable(world.map, data, i) || world.baseAtHex[static_cast<std::size_t>(i)] != entt::null)
            {
                continue;
            }
            const int distance = HexDistance(desired, IndexToHex(world.map, i));
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    }

    void PlaceBases(World& world, const GameData& data, const MatchSetup& setup)
    {
        HexMap& map = world.map;
        const std::vector<BasePlacement> placements = setup.bases.empty() ? DefaultBaseLayout(map.cols, map.rows) : setup.bases;
        std::array<std::array<int, BaseTypeCount>, FactionCount> counters{};

        for (const BasePlacement& placement : placements)
        {
            const HexCoord desired = { std::clamp(placement.coord.col, 0, map.cols - 1), std::clamp(placement.coord.row, 0, map.rows - 1) };
            const int index = FindFreeLand(world, data, desired);
            if (index < 0)
            {
                continue;
            }
            Hex& hex = map.hexes[static_cast<std::size_t>(index)];
            hex.terrain = Terrain::City;
            hex.owner = placement.faction;

            const int number = ++counters[static_cast<std::size_t>(placement.faction)][ToIndex(placement.type)];
            const std::string& side = world.factions[static_cast<std::size_t>(placement.faction)].name;
            std::string name = side + (placement.type == BaseType::Hq ? " HQ" : (placement.type == BaseType::Corps ? " Corps Base " : " Division Base "));
            if (placement.type != BaseType::Hq)
            {
                name += std::to_string(number);
            }

            const BaseDef& def = data.bases[ToIndex(placement.type)];
            const entt::entity entity = world.registry.create();
            world.registry.emplace<Faction>(entity, placement.faction);
            world.registry.emplace<Position>(entity, HexCenter(IndexToHex(map, index), map.hexDistance));
            world.registry.emplace<Base>(entity, std::move(name), placement.type, placement.faction, index, def.hp, def.hp);
            world.baseAtHex[static_cast<std::size_t>(index)] = entity;
        }
    }

    // 초기 영토: 가장 가까운 기지의 소유 (보로노이).
    void AssignInitialTerritory(World& world, const GameData& data)
    {
        std::vector<std::pair<HexCoord, int>> bases;
        for (const auto [entity, base, faction] : world.registry.view<Base, Faction>().each())
        {
            bases.push_back({ IndexToHex(world.map, base.hex), faction.id });
        }
        for (int i = 0; i < static_cast<int>(world.map.hexes.size()); ++i)
        {
            Hex& hex = world.map.hexes[static_cast<std::size_t>(i)];
            hex.owner = -1;
            if (!IsPassable(world.map, data, i))
            {
                continue;
            }
            int bestDistance = std::numeric_limits<int>::max();
            for (const auto& [coord, faction] : bases)
            {
                const int distance = HexDistance(coord, IndexToHex(world.map, i));
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    hex.owner = faction;
                }
            }
        }
    }

    // 초기 사단 배치 위치: 사단기지 → 군단기지 → 본부 순으로 돌아가며.
    std::vector<Vec2> SpawnPoints(const World& world, int faction)
    {
        std::vector<std::pair<int, Vec2>> points;
        for (const auto [entity, base, owner, position] : world.registry.view<Base, Faction, Position>().each())
        {
            if (owner.id == faction)
            {
                points.push_back({ static_cast<int>(base.type), position.value });
            }
        }
        std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

        std::vector<Vec2> result;
        for (const auto& point : points)
        {
            result.push_back(point.second);
        }
        return result;
    }

    std::vector<int> CycleTemplates(int count, std::size_t templateCount)
    {
        std::vector<int> result;
        for (int i = 0; i < count && templateCount > 0; ++i)
        {
            result.push_back(static_cast<int>(static_cast<std::size_t>(i) % templateCount));
        }
        return result;
    }
}

MatchSetup CampaignSetup(const GameData& data, int stage, int difficulty,
                         const std::vector<TemplateDef>& playerTemplates, bool botBattle)
{
    stage = std::clamp(stage, 0, static_cast<int>(data.stages.size()) - 1);
    difficulty = std::clamp(difficulty, 0, static_cast<int>(data.difficulties.size()) - 1);
    const StageDef& stageDef = data.stages[static_cast<std::size_t>(stage)];
    const DifficultyDef& difficultyDef = data.difficulties[static_cast<std::size_t>(difficulty)];

    MatchSetup setup;
    setup.title = "Stage " + std::to_string(stage + 1) + " " + stageDef.name + " [" + difficultyDef.name + "]" + (botBattle ? " - Bot Battle" : "");
    setup.stage = botBattle ? -1 : stage;   // 봇전투는 진행도에 반영하지 않는다.
    setup.matchSeconds = data.config.matchSeconds;
    setup.mapSeed = data.config.mapSeed + stage;
    setup.mapCols = data.config.mapCols;
    setup.mapRows = data.config.mapRows;

    SideSetup& blue = setup.sides[PlayerFaction];
    blue.name = "Blue";
    blue.ai = botBattle;
    blue.difficulty = difficulty;
    blue.incomeMultiplier = botBattle ? difficultyDef.aiIncome : 1.0f;
    blue.templates = playerTemplates;
    blue.initialDivisions = CycleTemplates(stageDef.playerDivisions, blue.templates.size());

    SideSetup& red = setup.sides[AiFaction];
    red.name = "Red";
    red.ai = true;
    red.difficulty = difficulty;
    red.incomeMultiplier = stageDef.aiIncome * difficultyDef.aiIncome;
    red.templates = data.aiTemplates;
    red.initialDivisions = CycleTemplates(stageDef.aiDivisions, red.templates.size());
    return setup;
}

void SetGeneratedMapSize(MatchSetup& setup, const GameConfig& config, float widthKm, float heightKm)
{
    widthKm = std::clamp(widthKm, config.mapMinKm, config.mapMaxKm);
    heightKm = std::clamp(heightKm, config.mapMinKm, config.mapMaxKm);
    setup.customMap = false;
    setup.mapCols = std::max(8, static_cast<int>(std::lround(widthKm / config.hexDistanceKm)));
    setup.mapRows = std::max(8, static_cast<int>(std::lround(heightKm / (config.hexDistanceKm * std::numbers::sqrt3_v<float> / 2.0f))));
}

void InitWorld(World& world, const GameData& data, const MatchSetup& setup, std::uint32_t seed)
{
    const GameConfig& c = data.config;
    world.registry.clear();
    world.rng.seed(seed);
    world.title = setup.title;
    world.time = 0.0f;
    world.matchSeconds = setup.matchSeconds;
    world.stage = setup.stage;
    world.over = false;
    world.winner = -1;
    world.endReason.clear();
    world.events.clear();
    ResetProfiler(world.profiler);

    if (setup.customMap)
    {
        world.map = setup.map;
    }
    else
    {
        GenerateMap(world.map, data, setup.mapCols, setup.mapRows, static_cast<std::uint32_t>(setup.mapSeed));
    }
    const std::size_t hexCount = world.map.hexes.size();
    world.occupancy.assign(hexCount, 0);
    world.baseAtHex.assign(hexCount, entt::null);

    const int regions = c.weatherRegions;
    world.weather.assign(static_cast<std::size_t>(regions * regions), WeatherRegion{});
    for (WeatherRegion& region : world.weather)
    {
        RollWeather(world, data, region);
    }
    for (int i = 0; i < static_cast<int>(hexCount); ++i)
    {
        const HexCoord coord = IndexToHex(world.map, i);
        world.map.hexes[static_cast<std::size_t>(i)].weatherRegion =
            (coord.row * regions / world.map.rows) * regions + coord.col * regions / world.map.cols;
    }

    for (int f = 0; f < FactionCount; ++f)
    {
        const SideSetup& side = setup.sides[static_cast<std::size_t>(f)];
        FactionState& state = world.factions[static_cast<std::size_t>(f)];
        state = FactionState{};
        state.name = side.name.empty() ? (f == PlayerFaction ? "Blue" : "Red") : side.name;
        state.aiControlled = side.ai;
        state.difficulty = std::clamp(side.difficulty, 0, static_cast<int>(data.difficulties.size()) - 1);
        state.incomeMultiplier = side.incomeMultiplier;
        state.templates = side.templates;
    }

    PlaceBases(world, data, setup);
    AssignInitialTerritory(world, data);
    PlaceMines(world, data);
    for (int f = 0; f < FactionCount; ++f)
    {
        SetupFactionEconomy(world, data, f);
    }

    for (int f = 0; f < FactionCount; ++f)
    {
        const SideSetup& side = setup.sides[static_cast<std::size_t>(f)];
        const std::vector<Vec2> points = SpawnPoints(world, f);
        if (points.empty())
        {
            continue;
        }
        for (std::size_t i = 0; i < side.initialDivisions.size(); ++i)
        {
            const int templateIndex = side.initialDivisions[i];
            if (templateIndex < 0 || templateIndex >= static_cast<int>(side.templates.size()))
            {
                continue;
            }
            SpawnDivision(world, data, f, side.templates[static_cast<std::size_t>(templateIndex)], points[i % points.size()]);
        }
    }

    RebuildOccupancy(world);
    UpdateTerritory(world, data);
}

entt::entity SpawnDivision(World& world, const GameData& data, int faction, const TemplateDef& templ, Vec2 position)
{
    Division division;
    division.battalions = templ.battalions;
    division.speed = std::numeric_limits<float>::max();

    float defenseSum = 0.0f;
    for (const int b : templ.battalions)
    {
        const BattalionDef& def = data.battalions[static_cast<std::size_t>(b)];
        division.maxHp += def.hp;
        defenseSum += def.defense;
        division.speed = std::min(division.speed, def.speed);
        if (def.effect != Effect::None)
        {
            division.effects |= EffectBit(def.effect);
        }
    }
    division.defenseAvg = templ.battalions.empty() ? 0.0f : defenseSum / static_cast<float>(templ.battalions.size());
    division.hp = division.maxHp;

    FactionState& state = world.factions[static_cast<std::size_t>(faction)];
    ++state.spawnedDivisions;
    division.name = templ.name + " " + std::to_string(state.spawnedDivisions);

    const entt::entity entity = world.registry.create();
    world.registry.emplace<Faction>(entity, faction);
    world.registry.emplace<Position>(entity, position);
    world.registry.emplace<Division>(entity, std::move(division));
    return entity;
}

void PushEvent(World& world, std::string text, EventKind kind)
{
    world.events.push_back({ world.time, std::move(text), kind });
    if (world.events.size() > MaxEvents)
    {
        world.events.erase(world.events.begin());
    }
}

void ApplyCommand(World& world, const GameData& data, const Command& command)
{
    if (world.over || command.faction < 0 || command.faction >= FactionCount)
    {
        return;
    }

    const auto ownsDivision = [&]()
    {
        return world.registry.valid(command.entity) && world.registry.all_of<Division>(command.entity)
            && world.registry.get<Faction>(command.entity).id == command.faction;
    };

    switch (command.type)
    {
    case CommandType::Move:
    {
        if (!ownsDivision())
        {
            return;
        }
        const Vec2 position = world.registry.get<Position>(command.entity).value;
        std::vector<int> path = FindPath(world.map, data, HexAt(world, position), command.targetHex);
        if (path.empty())
        {
            return;
        }
        world.registry.emplace_or_replace<MovePath>(command.entity, std::move(path));
        world.registry.remove<Retreating>(command.entity);
        break;
    }
    case CommandType::Retreat:
        if (ownsDivision())
        {
            StartRetreat(world, data, command.entity);
        }
        break;
    case CommandType::Produce:
    {
        FactionState& state = world.factions[static_cast<std::size_t>(command.faction)];
        if (command.templateIndex < 0 || command.templateIndex >= static_cast<int>(state.templates.size()))
        {
            return;
        }
        const TemplateDef& templ = state.templates[static_cast<std::size_t>(command.templateIndex)];
        if (!ProductionBlocker(state, data, templ).empty())
        {
            return;
        }
        const TemplateNeeds needs = GetTemplateNeeds(templ, data);
        for (std::size_t r = 0; r < ResourceCount; ++r)
        {
            state.stock[r] -= needs.materials[r];
        }
        state.manpower -= needs.manpower;
        state.queue.push_back(command.templateIndex);
        break;
    }
    case CommandType::Conscript:
        Conscript(world, data, command.faction);
        break;
    case CommandType::BuildFactory:
        StartFactory(world, data, command.faction, command.factoryType);
        break;
    case CommandType::Surrender:
    {
        world.over = true;
        world.winner = command.faction == PlayerFaction ? AiFaction : PlayerFaction;
        world.endReason = world.factions[static_cast<std::size_t>(command.faction)].name + " surrendered";
        break;
    }
    }
}

void StepWorld(World& world, const GameData& data, float dt)
{
    if (world.over)
    {
        return;
    }

    world.time += dt;
    Profiler& profiler = world.profiler;
    ++profiler.ticks;
    { ProfileScope scope(profiler, ProfileSection::Occupancy); RebuildOccupancy(world); }
    { ProfileScope scope(profiler, ProfileSection::Weather); UpdateWeather(world, data, dt); }
    { ProfileScope scope(profiler, ProfileSection::Ai); UpdateAI(world, data, dt); }
    { ProfileScope scope(profiler, ProfileSection::Movement); UpdateMovement(world, data, dt); }
    { ProfileScope scope(profiler, ProfileSection::Occupancy); RebuildOccupancy(world); }
    { ProfileScope scope(profiler, ProfileSection::Combat); UpdateCombat(world, data, dt); }
    { ProfileScope scope(profiler, ProfileSection::Territory); UpdateTerritory(world, data); }
    { ProfileScope scope(profiler, ProfileSection::Economy); UpdateEconomy(world, data, dt); }
    { ProfileScope scope(profiler, ProfileSection::Victory); CheckVictory(world, data); }
}
