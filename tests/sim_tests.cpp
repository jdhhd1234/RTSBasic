#include "ai/ai.hpp"
#include "ai/deeplearn/rl_features.hpp"
#include "core/lua_loader.hpp"
#include "core/phrases.hpp"
#include "core/templates.hpp"
#include "sim/components.hpp"
#include "sim/economy.hpp"
#include "sim/geojson_map.hpp"
#include "sim/map_features.hpp"
#include "sim/profile_report.hpp"
#include "sim/rules.hpp"
#include "sim/scenario.hpp"
#include "sim/simulation.hpp"
#include "sim/spline.hpp"
#include "sim/systems.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <source_location>
#include <string>

namespace
{
    int g_failures = 0;

    void Check(bool ok, const char* what, std::source_location where = std::source_location::current())
    {
        if (!ok)
        {
            ++g_failures;
            std::cerr << "FAIL line " << where.line() << ": " << what << '\n';
        }
    }

    bool Near(float a, float b)
    {
        return std::abs(a - b) < 1e-3f;
    }

    void TestHex()
    {
        for (const int row : { 4, 5 })
        {
            const HexCoord center{ 10, row };
            for (const HexCoord nb : HexNeighbors(center))
            {
                Check(HexDistance(center, nb) == 1, "neighbor distance is 1");
                Check(Near(Distance(HexCenter(center, 20.0f), HexCenter(nb, 20.0f)), 20.0f), "neighbor centers are 20km apart");
            }
        }
        for (int row = 0; row < 12; ++row)
        {
            for (int col = 0; col < 12; ++col)
            {
                const HexCoord back = WorldToHex(HexCenter({ col, row }, 20.0f), 20.0f);
                Check(back.col == col && back.row == row, "WorldToHex(HexCenter(c)) == c");
            }
        }
        Check(HexDistance({ 0, 0 }, { 5, 0 }) == 5, "row distance");
    }

    void TestSplines()
    {
        const std::vector<Vec2> points{ { 0.0f, 0.0f }, { 2.0f, 3.0f }, { 2.0f, 3.0f }, { 6.0f, 3.0f } };
        constexpr int segments = 4;
        const auto catmull = SampleCatmullRom(points, segments, false);
        const bool hasAllSpans = catmull.size() == (points.size() - 1) * segments + 1;
        Check(hasAllSpans, "open Catmull-Rom samples every span");
        if (hasAllSpans)
        {
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                Check(Near(Distance(catmull[i * segments], points[i]), 0.0f), "Catmull-Rom passes through each control point");
            }
        }
        Check(std::all_of(catmull.begin(), catmull.end(), [](Vec2 point)
        {
            return std::isfinite(point.x) && std::isfinite(point.y);
        }), "Catmull-Rom handles coincident control points");
        const auto closed = SampleCatmullRom(points, segments, true);
        Check(closed.size() == points.size() * segments + 1 && Near(Distance(closed.back(), points.front()), 0.0f),
              "closed Catmull-Rom returns to the first control point");
        const auto uneven = SampleCatmullRom({ { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 4.0f }, { 10.0f, 4.0f } }, 2, false);
        Check(uneven.size() == 7 && Near(uneven[3].x, 13.0f / 15.0f) && Near(uneven[3].y, 28.0f / 15.0f),
              "Catmull-Rom uses centripetal knots for unevenly spaced points");

        const auto bezier = SampleCubicBezier(points[0], points[1], points[2], points[3], segments);
        Check(bezier.size() == segments + 1 && Near(Distance(bezier.front(), points.front()), 0.0f)
              && Near(Distance(bezier.back(), points.back()), 0.0f), "cubic Bezier includes both endpoints");
        const auto arc = SampleArc({ 0.0f, 0.0f }, { 8.0f, 0.0f }, 0.25f, 2);
        Check(arc.size() == 3 && Near(arc[1].x, 4.0f) && Near(arc[1].y, 1.5f), "arc bends sideways in proportion to its length");

        const std::vector<Vec2> line{ { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 2.0f, 2.0f }, { 3.0f, 3.0f } };
        const auto simplified = SimplifyPolyline(line, 0.1f);
        Check(simplified.size() == 2 && Near(Distance(simplified.front(), line.front()), 0.0f)
              && Near(Distance(simplified.back(), line.back()), 0.0f), "simplification keeps the ends and removes collinear points");
    }

    void TestMapFeatures(const GameData& data)
    {
        HexMap map;
        map.cols = map.rows = 4;
        map.hexes.resize(16);
        for (int i = 0; i < static_cast<int>(map.hexes.size()); ++i)
        {
            map.hexes[static_cast<std::size_t>(i)].owner = IndexToHex(map, i).col < 2 ? 0 : 1;
        }
        const auto fronts = FrontLines(map);
        Check(fronts.size() == 1 && !fronts.front().closed && fronts.front().points.size() == 7,
              "two halves have one open front with seven distinct edges");
        if (!fronts.empty())
        {
            const auto& points = fronts.front().points;
            for (const Vec2 point : points)
            {
                Check(point.x >= 1.5f * map.hexDistance && point.x <= 2.0f * map.hexDistance,
                      "front edge midpoint lies between the halves");
            }
            const float stepY = 0.5f * HexCenter({ 0, 1 }, map.hexDistance).y;
            Check(std::adjacent_find(points.begin(), points.end(), [&](Vec2 a, Vec2 b)
            {
                return !Near(std::abs(a.y - b.y), stepY);
            }) == points.end(), "front edge midpoints follow the boundary in order");
        }
        for (Hex& hex : map.hexes)
        {
            hex.owner = 0;
        }
        map.hexes[static_cast<std::size_t>(HexIndex(map, { 1, 1 }))].owner = 1;
        const auto loop = FrontLines(map);
        Check(loop.size() == 1 && loop.front().closed && loop.front().points.size() == 6,
              "an enclosed owner produces a closed front with six edges");

        World world;
        InitWorld(world, data, CampaignSetup(data, 0, 1, data.defaultTemplates, false), 42);
        Check(!world.features.coastlines.empty(), "generated map has coastlines");
        Check(!world.features.rivers.empty(), "generated map has rivers");
        Check(!world.features.roads.empty(), "generated map has roads");

        MatchSetup setup;
        setup.customMap = true;
        setup.map = map;
        setup.bases.push_back({ PlayerFaction, BaseType::Hq, { 1, 1 } });
        InitWorld(world, data, setup, 42);
        Check(world.features.coastlines.empty() && world.features.rivers.empty() && world.features.roads.empty()
              && world.features.sites == std::vector<int>{ HexIndex(map, { 1, 1 }) }, "reinitializing replaces features and deduplicates the base city");
    }

    void TestRules(const GameData& data)
    {
        const GameConfig& c = data.config;
        Check(Near(SupplyFactor(250.0f, false, c), 1.0f), "supply 250km = 100%");
        Check(Near(SupplyFactor(450.0f, false, c), 0.75f), "supply 450km = 75%");
        Check(Near(SupplyFactor(650.0f, false, c), 0.5f), "supply 650km = 50%");
        Check(Near(SupplyFactor(1000.0f, false, c), 0.5f), "supply floor 50%");
        Check(Near(SupplyFactor(450.0f, true, c), 0.9375f), "transport 450km = 93.75%");

        Check(Near(TerrainMoveMultiplier(Terrain::Mountain, EffectBit(Effect::Mountain), data), 1.0f), "mountain unit ignores mountains");
        Check(Near(TerrainMoveMultiplier(Terrain::Mountain, EffectBit(Effect::Engineer), data), 0.7f), "engineer halves mountain penalty");
        Check(Near(WeatherMultiplier(0.5f, EffectBit(Effect::Cbrn)), 0.75f), "cbrn halves fog range penalty");
        Check(Near(RetreatThreshold(EffectBit(Effect::Police), c), 0.2f), "police retreat at 20%");

        const int tank = BattalionIndex(data, "tank");
        const int at = BattalionIndex(data, "at");
        Check(MatchupAgainst(UnitClass::At, { tank }, data) > 1.0f, "anti-tank beats tank");
        Check(MatchupAgainst(UnitClass::Aa, { BattalionIndex(data, "heli") }, data) > 1.0f, "air defense beats heli");
        Check(MatchupAgainst(UnitClass::Armor, { at }, data) < 1.0f, "tank weak vs anti-tank");
    }

    void TestTemplates(const GameData& data)
    {
        const int inf = BattalionIndex(data, "inf");
        Check(!ValidateTemplate(TemplateDef{ "empty", {} }, data).empty(), "empty template rejected");
        Check(!ValidateTemplate(TemplateDef{ "five", { 0, 1, 2, 3, 4 } }, data).empty(), "5 battalions rejected");
        Check(!ValidateTemplate(TemplateDef{ "triple", { inf, inf, inf } }, data).empty(), "3 of the same rejected");
        Check(ValidateTemplate(TemplateDef{ "double", { inf, inf } }, data).empty(), "2 of the same allowed");
        for (const TemplateDef& templ : data.defaultTemplates)
        {
            Check(ValidateTemplate(templ, data).empty(), "default templates are valid");
        }
        const TemplateNeeds armored = GetTemplateNeeds(data.defaultTemplates[1], data);
        Check(Near(armored.materials[ToIndex(Resource::Iron)], 64.0f) && Near(armored.manpower, 2200.0f) && armored.factories.size() == 2,
              "armored division needs iron 64, manpower 2200, armor + munitions factories");
        Check(Near(SummarizeTemplate(data.defaultTemplates[0], data).hp, 350.0f), "infantry division hp 350");
    }

    void TestGeoJson(const GameData& data, const std::string& dataDir)
    {
        const std::vector<std::vector<Vec2>> square = { { { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 } }, { { 4, 4 }, { 6, 4 }, { 6, 6 }, { 4, 6 } } };
        Check(PolygonContains(square, { 2, 2 }), "point inside polygon");
        Check(!PolygonContains(square, { 5, 5 }), "point inside hole is outside");
        Check(!PolygonContains(square, { 12, 2 }), "point outside polygon");

        HexMap map;
        MapLayers layers;
        std::string error;
        const bool ok = LoadGeoJsonMap(dataDir + "maps/rhineland.geojson", data, map, layers, error);
        Check(ok, "rhineland.geojson loads");
        if (!ok)
        {
            std::cerr << error << '\n';
            return;
        }
        std::array<int, TerrainCount> counts{};
        for (const Hex& hex : map.hexes)
        {
            ++counts[ToIndex(hex.terrain)];
        }
        Check(map.cols >= 20 && map.cols <= 30 && map.rows >= 25 && map.rows <= 40, "rhineland map ~500x550 km");
        Check(counts[ToIndex(Terrain::Sea)] > 0 && counts[ToIndex(Terrain::Forest)] > 0 && counts[ToIndex(Terrain::Mountain)] > 0, "polygons painted");
        Check(counts[ToIndex(Terrain::River)] > 10, "river lines painted");
        Check(counts[ToIndex(Terrain::City)] >= 5, "city points painted");
        std::cout << "geojson " << map.cols << "x" << map.rows << ": sea=" << counts[ToIndex(Terrain::Sea)]
                  << " forest=" << counts[ToIndex(Terrain::Forest)] << " river=" << counts[ToIndex(Terrain::River)] << '\n';

        HexMap bad;
        Check(!LoadGeoJsonMap(dataDir + "game_data.lua", data, bad, layers, error), "non-geojson file rejected");
    }

    void TestQgisLayers(const GameData& data)
    {
        const auto path = std::filesystem::temp_directory_path() / ("minrts-qgis-" + std::to_string(std::random_device{}()) + ".geojson");
        {
            std::ofstream file(path);
            file << R"({"type":"FeatureCollection","features":[
                {"type":"Feature","properties":{"terrain":"plain"},"geometry":{"type":"Polygon","coordinates":[[[100000,100000],[300000,100000],[300000,300000],[100000,300000],[100000,100000]]]}},
                {"type":"Feature","properties":{"layer":"terrain","terrain":"forest","elevation":600},"geometry":{"type":"Polygon","coordinates":[[[205000,226000],[235000,226000],[235000,248000],[205000,248000],[205000,226000]]]}},
                {"type":"Feature","properties":{"layer":"road","tier":"highway"},"geometry":{"type":"LineString","coordinates":[[120000,274019],[280000,274019]]}},
                {"type":"Feature","properties":{"layer":"road","tier":null},"geometry":{"type":"MultiLineString","coordinates":[[[160000,274019],[160000,235000]]]}},
                {"type":"Feature","properties":{"layer":"river"},"geometry":{"type":"LineString","coordinates":[[120000,130000],[280000,130000]]}},
                {"type":"Feature","properties":{"layer":"city","tier":"metro","name":"Test Metro","population":200000},"geometry":{"type":"Point","coordinates":[140000,274019]}},
                {"type":"Feature","properties":{"layer":"city","name":"Test Town","population":100000},"geometry":{"type":"Point","coordinates":[260000,200000]}},
                {"type":"Feature","properties":{"layer":"mine","resource":"oil","reserve":0.3},"geometry":{"type":"Point","coordinates":[140000,274019]}},
                {"type":"Feature","properties":{"layer":"unit","faction":0,"template":"QGIS infantry"},"geometry":{"type":"Point","coordinates":[140000,274019]}},
                {"type":"Feature","properties":{"layer":"unit","faction":"blue","template":"missing"},"geometry":{"type":"Point","coordinates":[160000,274019]}},
                {"type":"Feature","properties":{"layer":"territory","owner":"blue"},"geometry":{"type":"Polygon","coordinates":[[[100000,100000],[180000,100000],[180000,300000],[100000,300000],[100000,100000]]]}},
                {"type":"Feature","properties":{"layer":"territory","owner":"1"},"geometry":{"type":"MultiPolygon","coordinates":[[[[220000,100000],[300000,100000],[300000,300000],[220000,300000],[220000,100000]]]]}},
                {"type":"Feature","properties":{"layer":"unknown"},"geometry":{"type":"Point","coordinates":[140000,274019]}},
                {"type":"Feature","properties":{"layer":"road","tier":"invalid"},"geometry":{"type":"LineString","coordinates":[[120000,274019],[280000,274019]]}},
                {"type":"Feature","properties":{"layer":"city","name":"Bad population","population":"100000"},"geometry":{"type":"Point","coordinates":[260000,200000]}}
            ]})";
            Check(static_cast<bool>(file), "temporary QGIS fixture written");
        }

        MatchSetup setup;
        std::string error;
        const bool loaded = LoadGeoJsonMap(path.string(), data, setup.map, setup.layers, error);
        std::filesystem::remove(path);
        Check(loaded, "QGIS fixture loads with optional layers");
        if (!loaded)
        {
            std::cerr << error << '\n';
            return;
        }

        const MapLayers& layers = setup.layers;
        Check(error.find("3 invalid or out-of-bounds items skipped") != std::string::npos, "bad layer and attributes are counted as warnings");
        Check(layers.roads.size() == 2 && layers.roads[0].tier == RoadTier::Highway && layers.roads[1].tier == RoadTier::Minor,
              "QGIS road tiers parse including the default");
        Check(layers.rivers.size() == 1 && std::any_of(setup.map.hexes.begin(), setup.map.hexes.end(), [](const Hex& hex)
        {
            return hex.terrain == Terrain::River;
        }), "QGIS river retains its geometry and paints river terrain");
        Check(layers.cities.size() == 2 && layers.cities[0].population == 200000 && layers.cities[1].tier == CityTier::Town,
              "QGIS city population and default tier parse");
        Check(layers.mines.size() == 1 && layers.mines[0].resource == Resource::Oil && Near(layers.mines[0].reserve, 0.3f), "QGIS mine parses");
        Check(layers.units.size() == 2 && layers.units[0].faction == PlayerFaction && layers.units[0].templateName == "QGIS infantry", "QGIS units parse");
        Check(layers.territory.size() == 2 && layers.territory[1].faction == AiFaction, "QGIS territory parses polygons and multipolygons");
        Check(Near(setup.map.hexes[static_cast<std::size_t>(HexIndex(setup.map, { 5, 3 }))].elevation, 600.0f), "QGIS terrain paints elevation in metres");

        setup.customMap = true;
        setup.bases = { { PlayerFaction, BaseType::Hq, { 1, 1 } }, { AiFaction, BaseType::Hq, { 8, 8 } } };
        for (auto& side : setup.sides)
        {
            side.templates = { data.defaultTemplates.front() };
            side.templates.front().name = "QGIS infantry";
            side.initialDivisions = { 0 };
        }
        setup.sides[PlayerFaction].initialDivisions.push_back(0);
        World world;
        InitWorld(world, data, setup, 11);
        const int blueHex = HexIndex(world.map, { 1, 1 });
        const int overlapHex = HexIndex(world.map, { 2, 1 });
        Check(world.features.cities.size() == 2 && world.features.cities[0].hex == blueHex
              && world.features.cities[0].tier == CityTier::Metro && world.features.cities[0].name == "Test Metro", "authored city data reaches world features");
        Check(world.features.roads.size() == 2 && world.map.hexes[static_cast<std::size_t>(overlapHex)].road == RoadTier::Highway,
              "authored roads replace generation and overlapping roads keep the fastest tier");
        Check(world.features.rivers.size() == 1 && world.features.rivers[0].points.size() == 2, "world retains authored river geometry");
        Check(world.map.hexes[static_cast<std::size_t>(blueHex)].terrain == Terrain::City, "city point paints city terrain");
        Check(world.map.hexes[static_cast<std::size_t>(HexIndex(world.map, { 2, 4 }))].owner == PlayerFaction
              && world.map.hexes[static_cast<std::size_t>(HexIndex(world.map, { 7, 6 }))].owner == AiFaction
              && world.map.hexes[static_cast<std::size_t>(HexIndex(world.map, { 4, 4 }))].owner == -1, "authored territory sets owners and leaves gaps neutral");
        Check(world.factions[PlayerFaction].spawnedDivisions == 1 && world.factions[AiFaction].spawnedDivisions == 1, "authored units override defaults only for their faction");
        Check(std::any_of(world.events.begin(), world.events.end(), [](const GameEvent& event)
        {
            return event.text == "Unknown unit template: missing";
        }), "unknown authored unit template emits an event");

        const float manpower = world.factions[PlayerFaction].manpower;
        UpdateEconomy(world, data, 1.0f);
        Check(Near(world.factions[PlayerFaction].manpower - manpower, 2.0f * data.config.cityManpowerPer100k), "owned QGIS city generates manpower");
        Check(world.mines.size() == 1 && Near(world.mines[0].reserve, 0.1f), "connected authored mine uses the configured resource rate");
        UpdateEconomy(world, data, 1.0f);
        Check(world.mines.size() == 1 && Near(world.mines[0].reserve, 0.0f) && Near(world.mines[0].rate, 0.0f)
              && Near(world.factions[PlayerFaction].mineRate[ToIndex(Resource::Oil)], 0.1f), "last mine extraction is capped by its reserve");
        UpdateEconomy(world, data, 1.0f);
        Check(Near(world.factions[PlayerFaction].mineRate[ToIndex(Resource::Oil)], 0.0f)
              && std::count_if(world.events.begin(), world.events.end(), [](const GameEvent& event)
              {
                  return event.text == "oil mine exhausted";
              }) == 1, "an exhausted mine stops permanently and posts one event");

        for (const auto [entity, division, position, faction] : world.registry.view<Division, Position, Faction>().each())
        {
            if (faction.id != PlayerFaction)
            {
                continue;
            }
            const Vec2 start = position.value;
            Check(Near(Distance(start, layers.units.front().position), 0.0f), "unit spawns at the authored location");
            world.registry.emplace<MovePath>(entity, MovePath{ { overlapHex } });
            UpdateMovement(world, data, 0.25f);
            const float onRoad = Distance(start, position.value);
            position.value = start;
            world.map.hexes[static_cast<std::size_t>(blueHex)].road = RoadTier::None;
            UpdateMovement(world, data, 0.25f);
            const float offRoad = Distance(start, position.value);
            Check(offRoad > 0.0f && Near(onRoad, offRoad * data.config.roadSpeedHighway), "highway speeds up unit movement");
        }

        HexMap routing;
        routing.cols = 5;
        routing.rows = 3;
        routing.hexes.resize(15);
        const int start = HexIndex(routing, { 0, 1 });
        const int goal = HexIndex(routing, { 4, 1 });
        Check(FindPath(routing, data, start, goal).size() == 4, "without roads the short direct path wins");
        for (int col = 1; col < routing.cols; ++col)
        {
            routing.hexes[static_cast<std::size_t>(HexIndex(routing, { col, 0 }))].road = RoadTier::Highway;
        }
        const auto pathOnRoad = FindPath(routing, data, start, goal);
        Check(pathOnRoad.size() == 5 && pathOnRoad.front() == HexIndex(routing, { 1, 0 }), "A* prefers a longer but faster highway path");

        const auto defenderDamage = [&](float elevation)
        {
            World combat;
            combat.map = routing;
            combat.weather.resize(1);
            combat.rng.seed(7);
            combat.map.hexes[static_cast<std::size_t>(HexIndex(combat.map, { 1, 1 }))].elevation = elevation;
            const TemplateDef& type = data.defaultTemplates.front();
            SpawnDivision(combat, data, PlayerFaction, type, HexCenter({ 0, 1 }, combat.map.hexDistance));
            const auto defender = SpawnDivision(combat, data, AiFaction, type, HexCenter({ 1, 1 }, combat.map.hexDistance));
            UpdateCombat(combat, data, 1.0f);
            const Division& division = combat.registry.get<Division>(defender);
            return division.maxHp - division.hp;
        };
        const float flatDamage = defenderDamage(0.0f);
        const float hillDamage = defenderDamage(100.0f);
        const float cappedDamage = defenderDamage(1500.0f);
        Check(flatDamage > hillDamage && hillDamage > cappedDamage && Near(cappedDamage, defenderDamage(3000.0f))
              && Near(flatDamage, defenderDamage(-100.0f)), "only a higher defender receives a capped elevation bonus");
    }

    void TestScenarios(const GameData& data, const std::string& dataDir)
    {
        for (const char* file : { "rhineland_qgis.lua", "dragon_valley.lua", "bot_battle.lua" })
        {
            Scenario scenario;
            std::string error;
            const bool ok = LoadScenario(dataDir + "sandbox/" + file, data, data.defaultTemplates, scenario, error);
            Check(ok, "sandbox scenario loads");
            if (!ok)
            {
                std::cerr << file << ": " << error << '\n';
                continue;
            }

            World world;
            InitWorld(world, scenario.data, scenario.setup, 3);
            int bases = 0;
            for (const auto entity : world.registry.view<Base>())
            {
                (void)entity;
                ++bases;
            }
            Check(bases == 14, "scenario places 14 bases");
            for (int i = 0; i < 3000; ++i)   // 5분
            {
                StepWorld(world, scenario.data, 0.1f);
            }
            std::cout << file << ": divisions " << world.registry.view<Division>().size() << ", events " << world.events.size() << '\n';
        }

        Scenario dragons;
        std::string error;
        LoadScenario(dataDir + "sandbox/dragon_valley.lua", data, data.defaultTemplates, dragons, error);
        Check(dragons.data.battalions.size() == data.battalions.size() + 6, "custom battalions appended");
        Check(dragons.setup.sides[AiFaction].initialDivisions.size() == 5, "enemy divisions from Lua");
        const int dragon = BattalionIndex(dragons.data, "dragon");
        Check(dragon >= 20 && Near(TerrainMoveMultiplier(Terrain::Mountain, EffectBit(Effect::AllTerrain), dragons.data), 1.0f), "all_terrain ignores mountains");

        // AI vs AI: 끝까지 돈다.
        Scenario bots;
        LoadScenario(dataDir + "sandbox/bot_battle.lua", data, data.defaultTemplates, bots, error);
        World world;
        InitWorld(world, bots.data, bots.setup, 11);
        Check(world.factions[PlayerFaction].aiControlled && world.factions[AiFaction].aiControlled, "bot battle: both sides AI");
        for (int i = 0; i < 12100 && !world.over; ++i)
        {
            StepWorld(world, bots.data, 0.1f);
        }
        Check(world.over && world.winner >= 0, "bot battle ends with a winner");
        std::cout << "bot battle: " << world.endReason << " at " << world.time << "s\n";
    }

    void TestWorld(const GameData& data)
    {
        World world;
        InitWorld(world, data, CampaignSetup(data, 0, 1, data.defaultTemplates, false), 42);

        int bases = 0;
        int hqHexes[2] = { -1, -1 };
        for (const auto [entity, base, faction] : world.registry.view<Base, Faction>().each())
        {
            ++bases;
            if (base.type == BaseType::Hq)
            {
                hqHexes[faction.id] = base.hex;
            }
        }
        Check(bases == 14, "7 bases per faction");
        Check(world.registry.view<Division>().size() == static_cast<std::size_t>(data.stages[0].playerDivisions + data.stages[0].aiDivisions), "initial divisions");
        Check(!FindPath(world.map, data, hqHexes[0], hqHexes[1]).empty(), "path exists between HQs");

        std::array<int, TerrainCount> counts{};
        for (const Hex& hex : world.map.hexes)
        {
            ++counts[ToIndex(hex.terrain)];
        }
        std::cout << "terrain:";
        for (std::size_t i = 0; i < TerrainCount; ++i)
        {
            std::cout << ' ' << TerrainNames[i] << '=' << counts[i];
        }
        std::cout << '\n';

        // 인접한 두 적 사단은 교전해서 체력이 준다.
        const HexCoord a{ 30, 34 };
        const HexCoord b{ 31, 34 };
        world.map.hexes[static_cast<std::size_t>(HexIndex(world.map, a))].terrain = Terrain::Plain;
        world.map.hexes[static_cast<std::size_t>(HexIndex(world.map, b))].terrain = Terrain::Plain;
        const entt::entity player = SpawnDivision(world, data, PlayerFaction, data.defaultTemplates[0], HexCenter(a, 20.0f));
        const entt::entity enemy = SpawnDivision(world, data, AiFaction, data.defaultTemplates[0], HexCenter(b, 20.0f));
        for (int i = 0; i < 50; ++i)
        {
            StepWorld(world, data, 0.1f);
        }
        Check(world.registry.valid(player) && world.registry.get<Division>(player).hp < world.registry.get<Division>(player).maxHp, "player division took damage");
        Check(world.registry.valid(enemy) && world.registry.get<Division>(enemy).hp < world.registry.get<Division>(enemy).maxHp, "enemy division took damage");

        // 생산: 자원 차감 후 대기열
        FactionState& blue = world.factions[PlayerFaction];
        const float ironBefore = blue.stock[ToIndex(Resource::Iron)];
        const float manpowerBefore = blue.manpower;
        ApplyCommand(world, data, Command{ .type = CommandType::Produce, .faction = PlayerFaction, .templateIndex = 0 });
        const TemplateNeeds infantry = GetTemplateNeeds(data.defaultTemplates[0], data);
        Check(Near(blue.stock[ToIndex(Resource::Iron)], ironBefore - infantry.materials[ToIndex(Resource::Iron)])
              && Near(blue.manpower, manpowerBefore - infantry.manpower), "produce deducts materials and manpower");
        Check(blue.queue.size() == 1, "produce enqueues");
        blue.stock.fill(0.0f);
        ApplyCommand(world, data, Command{ .type = CommandType::Produce, .faction = PlayerFaction, .templateIndex = 1 });
        Check(blue.queue.size() == 1, "produce refused without materials");

        // 한 판 끝까지: 20분 안에 반드시 끝난다.
        World full;
        InitWorld(full, data, CampaignSetup(data, 2, 2, data.defaultTemplates, false), 7);
        for (int i = 0; i < 12100 && !full.over; ++i)
        {
            StepWorld(full, data, 0.1f);
        }
        Check(full.over && full.winner >= 0, "match ends with a winner");

        // 항복
        World surrender;
        InitWorld(surrender, data, CampaignSetup(data, 0, 0, data.defaultTemplates, false), 1);
        ApplyCommand(surrender, data, Command{ .type = CommandType::Surrender, .faction = PlayerFaction });
        Check(surrender.over && surrender.winner == AiFaction, "surrender gives the win to the enemy");

        // 초기 영토는 가장 가까운 기지 소유
        Check(surrender.factions[PlayerFaction].ownedHexes > 0 && surrender.factions[AiFaction].ownedHexes > 0, "voronoi territory");
        std::cout << "full match: " << full.endReason << " at " << full.time << "s, divisions alive "
                  << full.registry.view<Division>().size() << '\n';
    }
    void TestFrontage(const GameData& data)
    {
        GameData controlled = data;
        controlled.config.frontageDefaultKm = 35.0f;
        controlled.weather[ToIndex(Weather::Clear)] = WeatherDef{};
        const TemplateDef infantry{ "Infantry", { BattalionIndex(controlled, "inf") } };

        World world;
        world.map.cols = 8;
        world.map.rows = 5;
        world.map.hexes.resize(40);
        world.occupancy.resize(world.map.hexes.size());
        world.baseAtHex.resize(world.map.hexes.size(), entt::null);
        world.weather.resize(1);

        const Vec2 center = HexCenter({ 3, 2 }, world.map.hexDistance);
        const Vec2 distantEnemy{ center.x + 50.0f, center.y };
        const entt::entity player = SpawnDivision(world, controlled, PlayerFaction, infantry, center);
        const entt::entity enemy = SpawnDivision(world, controlled, AiFaction, infantry, distantEnemy);
        Division& playerUnit = world.registry.get<Division>(player);
        Division& enemyUnit = world.registry.get<Division>(enemy);
        const auto setFrontage = [&](entt::entity entity, int faction, float km)
        {
            ApplyCommand(world, controlled, Command{ .type = CommandType::SetFrontage, .entity = entity, .faction = faction, .value = km });
        };
        const auto tickDamage = [&]()
        {
            for (const auto [entity, unit] : world.registry.view<Division>().each())
            {
                unit.hp = unit.maxHp;
            }
            UpdateCombat(world, controlled, 0.1f);
            return playerUnit.maxHp - playerUnit.hp;
        };

        Check(Near(playerUnit.frontageKm, 35.0f), "division spawns with the configured frontage");
        setFrontage(player, PlayerFaction, -10.0f);
        Check(Near(playerUnit.frontageKm, controlled.config.frontageMinKm), "frontage clamps to the minimum");
        setFrontage(player, PlayerFaction, 1000.0f);
        Check(Near(playerUnit.frontageKm, controlled.config.frontageMaxKm), "frontage clamps to the maximum");
        setFrontage(player, AiFaction, 30.0f);
        Check(Near(playerUnit.frontageKm, controlled.config.frontageMaxKm), "cannot change enemy frontage");

        // 대대 AI가 담당 범위 안의 적에게 다가가 사거리에 들면 쏜다.
        const auto runBattle = [&](float seconds)
        {
            for (const auto [entity, unit] : world.registry.view<Division>().each())
            {
                unit.hp = unit.maxHp;
            }
            ResetBattalionPositions(world, player);
            ResetBattalionPositions(world, enemy);
            for (float t = 0.0f; t < seconds; t += 0.1f)
            {
                UpdateBattalionAI(world, controlled, 0.1f);
                UpdateCombat(world, controlled, 0.1f);
            }
        };
        setFrontage(enemy, AiFaction, 30.0f);
        setFrontage(player, PlayerFaction, 30.0f);
        runBattle(60.0f);
        Check(Near(enemyUnit.hp, enemyUnit.maxHp) && Near(playerUnit.hp, playerUnit.maxHp), "30 km frontage does not engage an enemy 50 km away");
        for (const BattalionUnit& unit : playerUnit.units)
        {
            Check(Distance(unit.position, center) <= 30.0f + 1e-3f, "battalions stay inside the division zone");
        }
        setFrontage(player, PlayerFaction, 70.0f);
        runBattle(60.0f);
        Check(enemyUnit.hp < enemyUnit.maxHp, "70 km frontage engages an enemy 50 km away");
        Check(playerUnit.units.front().task == BattalionTask::Engage, "battalion AI picks engage for a threat in its zone");
        for (const BattalionUnit& unit : playerUnit.units)
        {
            Check(Distance(unit.position, center) <= 70.0f + 1e-3f, "engaging battalions stay inside the division zone");
        }

        const int next = HexIndex(world.map, { 3, 3 });
        world.registry.emplace<MovePath>(player, std::vector<int>{ next });
        UpdateMovement(world, controlled, 0.1f);
        Check(Near(Distance(world.registry.get<Position>(player).value, center), 0.0f), "movement stops inside its frontage");
        setFrontage(player, PlayerFaction, 30.0f);
        UpdateMovement(world, controlled, 0.1f);
        Check(Distance(world.registry.get<Position>(player).value, center) > 0.0f, "movement resumes outside its frontage");
        world.registry.get<Position>(player).value = center;

        world.registry.get<Position>(enemy).value = { center.x + 20.0f, center.y };
        ResetBattalionPositions(world, player);
        ResetBattalionPositions(world, enemy);
        const float narrowTaken = tickDamage();
        const float narrowDealt = enemyUnit.maxHp - enemyUnit.hp;
        setFrontage(player, PlayerFaction, 70.0f);
        const float wideTaken = tickDamage();
        const float wideDealt = enemyUnit.maxHp - enemyUnit.hp;
        Check(narrowDealt > wideDealt && wideDealt > 0.0f, "wider frontage lowers attack");
        Check(wideTaken > narrowTaken && narrowTaken > 0.0f, "wider frontage increases damage taken");

        world.registry.get<Position>(enemy).value = { center.x + 15.0f, center.y };
        ResetBattalionPositions(world, enemy);
        const entt::entity secondEnemy = SpawnDivision(world, controlled, AiFaction, infantry, { center.x - 15.0f, center.y });
        setFrontage(enemy, AiFaction, 70.0f);
        setFrontage(secondEnemy, AiFaction, 70.0f);
        world.registry.emplace<Retreating>(secondEnemy);
        const float firstDamage = tickDamage();
        world.registry.remove<Retreating>(secondEnemy);
        world.registry.emplace<Retreating>(enemy);
        const float secondDamage = tickDamage();
        world.registry.remove<Retreating>(enemy);
        Check(firstDamage > 0.0f && secondDamage > 0.0f && Near(tickDamage(), firstDamage + secondDamage),
              "both attackers damage one defender in the same tick");
    }

    void TestPhrases(const std::string& dataDir)
    {
        PhraseList phrases = { { "Tank", "전차" }, { "Anti-Tank", "대전차" }, { "at", "AT" }, { "(x)", "(엑스)" }, { "", "빈 키" } };
        PreparePhrases(phrases);
        Check(phrases.size() == 4 && phrases.front().first == "Anti-Tank", "phrases sorted, empty key dropped");
        Check(ReplacePhrases("Anti-Tank Tank", phrases) == "대전차 전차", "longest phrase wins");
        Check(ReplacePhrases("Tanks attack at dawn", phrases) == "Tanks attack AT dawn", "word boundary only");
        Check(ReplacePhrases("a(x)b", phrases) == "a(엑스)b", "punctuation phrase ignores boundary");
        Check(ReplacePhrases("탱크Tank", phrases) == "탱크Tank", "no replace inside utf-8 word");

        PhraseList korean;
        std::string error;
        Check(LoadStringPairs(dataDir + "lang/ko.lua", korean, error), "ko.lua loads");
        PreparePhrases(korean);
        Check(ReplacePhrases("Frontage 30 km", korean) == "전선 길이 30 km", "ko frontage label");
        Check(ReplacePhrases("Infantry Div 3 is ready", korean) == "보병사단 3 생산 완료", "ko event line");
        Check(ReplacePhrases("Time up - territory Blue 10 vs Red 20 hexes", korean) == "시간 종료 - 영토 청군 10 대 적군 20 헥스", "ko end reason");
    }
    void TestEconomy(const GameData& data)
    {
        const GameConfig& c = data.config;
        Check(GameDateText(0.0f, c) == "2030-01" && GameDateText(365.0f / c.daysPerSecond, c) == "2031-01", "game calendar");
        Check(UpkeepPerSecond(20, c) > 2.0f * UpkeepPerSecond(10, c), "upkeep grows faster than division count");

        World world;
        InitWorld(world, data, CampaignSetup(data, 0, 1, data.defaultTemplates, false), 5);
        world.factions[AiFaction].aiControlled = false;   // 경제만 본다
        FactionState& blue = world.factions[PlayerFaction];
        Check(world.mines.size() == static_cast<std::size_t>(c.minesPerSide * 2), "mines placed symmetric");
        Check(blue.factories.size() == data.startFactories.size(), "start factories placed");

        // 징집: 안전선 안에서는 손해 없음, 넘기면 군사질 하락
        const float manpower = blue.manpower;
        ApplyCommand(world, data, Command{ .type = CommandType::Conscript, .faction = PlayerFaction });
        Check(Near(blue.manpower, manpower + c.conscriptManpower) && Near(blue.quality, 1.0f), "safe conscription");
        for (int i = 0; i < 3; ++i)
        {
            ApplyCommand(world, data, Command{ .type = CommandType::Conscript, .faction = PlayerFaction });
        }
        Check(blue.draftPressure > c.pressureSafe && blue.quality < 1.0f, "over-conscription lowers quality");

        // 공장: EW 공장이 없으면 신호대대 편제는 생산 불가, 4년 뒤 완공되면 가능
        const int ewFactory = data.battalions[static_cast<std::size_t>(BattalionIndex(data, "sig"))].factory;
        const TemplateDef signals{ "Signals Div", { BattalionIndex(data, "sig"), BattalionIndex(data, "inf") } };
        blue.stock.fill(1000.0f);
        blue.manpower = 100000.0f;
        Check(!ProductionBlocker(blue, data, signals).empty(), "no EW factory -> cannot produce");
        ApplyCommand(world, data, Command{ .type = CommandType::BuildFactory, .faction = PlayerFaction, .factoryType = ewFactory });
        Check(FactoriesUnderConstruction(blue, ewFactory) == 1, "factory construction started");
        const int days = static_cast<int>(data.factories[static_cast<std::size_t>(ewFactory)].buildDays / c.daysPerSecond);
        for (int i = 0; i < days - 5; ++i)
        {
            UpdateEconomy(world, data, 1.0f);
        }
        Check(ActiveFactories(blue, ewFactory) == 0, "factory not done before 4 years");
        for (int i = 0; i < 10; ++i)
        {
            UpdateEconomy(world, data, 1.0f);
        }
        Check(ActiveFactories(blue, ewFactory) == 1 && ProductionBlocker(blue, data, signals).empty(), "factory done after 4 years");

        // 광산은 아군 영토로 공장과 이어져야 채굴된다
        const float connectedRate = std::accumulate(blue.mineRate.begin(), blue.mineRate.end(), 0.0f);
        for (Hex& hex : world.map.hexes)
        {
            hex.owner = hex.owner == PlayerFaction ? AiFaction : hex.owner;
        }
        for (const Mine& mine : world.mines)
        {
            world.map.hexes[static_cast<std::size_t>(mine.hex)].owner = PlayerFaction;   // 광산만 남기고 고립
        }
        UpdateEconomy(world, data, 1.0f);
        Check(connectedRate > 0.0f && std::accumulate(blue.mineRate.begin(), blue.mineRate.end(), 0.0f) == 0.0f, "isolated mines produce nothing");

        // 석유가 바닥나면 연료 부족: 공격·이동 저하
        blue.stock[ToIndex(Resource::Oil)] = 0.0f;
        UpdateEconomy(world, data, 1.0f);
        Check(blue.fuelShortage && FactionAttackMultiplier(blue, c) < 1.0f && FactionSpeedMultiplier(blue, c) < 1.0f, "fuel shortage penalties");
    }

    bool PlanHas(const std::vector<AiTask>& plan, AiTaskType type, AiPosture posture = AiPosture::Hold)
    {
        return std::any_of(plan.begin(), plan.end(), [&](const AiTask& t) { return t.type == type && (type != AiTaskType::SetPosture || t.posture == posture); });
    }

    void TestAi(const GameData& data)
    {
        const DifficultyDef& normal = data.difficulties[1];

        // HTN: 전력비에 따라 공격 / 집결 / 수세 메서드를 고른다.
        AiBlackboard board;
        board.canProduce = true;
        board.objectives.push_back({ .hex = 5, .position = { 300.0f, 0.0f } });
        board.ownStrength = 1000.0f;
        board.enemyStrength = 300.0f;
        std::vector<AiTask> plan = PlanCommand(board, normal);
        Check(PlanHas(plan, AiTaskType::Produce) && PlanHas(plan, AiTaskType::ChooseObjective) && PlanHas(plan, AiTaskType::SetPosture, AiPosture::Attack), "htn: strong -> attack");
        board.enemyStrength = 1250.0f;
        Check(PlanHas(PlanCommand(board, normal), AiTaskType::SetPosture, AiPosture::Regroup), "htn: even -> regroup");
        board.enemyStrength = 5000.0f;
        Check(PlanHas(PlanCommand(board, normal), AiTaskType::SetPosture, AiPosture::Hold), "htn: weak -> hold");
        board.threats.push_back({ .baseHex = 9, .enemyStrength = 400.0f, .ownStrength = 100.0f });
        Check(PlanHas(PlanCommand(board, normal), AiTaskType::Defend), "htn: outnumbered base -> defend");
        board.needManpower = true;
        board.canConscript = true;
        board.factoryToBuild = 2;
        board.canBuildFactory = true;
        plan = PlanCommand(board, normal);
        Check(PlanHas(plan, AiTaskType::Conscript) && PlanHas(plan, AiTaskType::BuildFactory), "htn: economy -> conscript, build factory");
        board.canAffordUpkeep = false;
        Check(!PlanHas(PlanCommand(board, normal), AiTaskType::Produce), "htn: oil deficit -> no new divisions");

        // Blackboard + Utility: 실제 월드에서
        World world;
        InitWorld(world, data, CampaignSetup(data, 0, 1, data.defaultTemplates, false), 42);
        AiBlackboard red;
        BuildBlackboard(world, data, AiFaction, red);
        Check(red.ownStrength > 0.0f && red.enemyStrength > 0.0f && red.objectives.size() == 7, "blackboard senses strength and enemy bases");

        entt::entity hurt = entt::null;
        for (const auto [entity, division, faction] : world.registry.view<Division, Faction>().each())
        {
            if (faction.id == AiFaction)
            {
                hurt = entity;
            }
        }
        Division& division = world.registry.get<Division>(hurt);
        division.hp = division.maxHp * 0.15f;
        Check(ChooseDivisionAction(world, data, red, AiFaction, hurt).action == AiAction::Recover, "utility: badly hurt -> recover");
        division.hp = division.maxHp;
        red.posture = AiPosture::Attack;
        red.attackHex = red.objectives.front().hex;
        red.attackPosition = red.objectives.front().position;
        Check(ChooseDivisionAction(world, data, red, AiFaction, hurt).action == AiAction::Attack, "utility: attack posture -> attack");
    }
    void TestRlFeatures(const GameData& data)
    {
        World world;
        InitWorld(world, data, CampaignSetup(data, 0, 1, data.defaultTemplates, false), 42);
        AiBlackboard board;
        BuildBlackboard(world, data, AiFaction, board);
        board.posture = AiPosture::Attack;
        Check(!board.objectives.empty(), "RL fixture has an enemy objective");
        if (!board.objectives.empty())
        {
            board.attackHex = board.objectives.front().hex;
        }

        entt::entity ownDivision = entt::null;
        for (const auto [entity, division, faction] : world.registry.view<Division, Faction>().each())
        {
            if (faction.id == AiFaction)
            {
                ownDivision = entity;
                break;
            }
        }
        Check(ownDivision != entt::null, "RL fixture has an own division");
        if (ownDivision == entt::null)
        {
            return;
        }

        const auto candidates = ListDivisionCandidates(world, data, board, AiFaction, ownDivision);
        Check(!candidates.empty() && candidates.front().action == AiAction::Hold, "RL candidate zero is Hold");
        if (!candidates.empty())
        {
            const AiDecision& best = candidates[BestCandidate(candidates)];
            const AiDecision chosen = ChooseDivisionAction(world, data, board, AiFaction, ownDivision);
            Check(best.action == chosen.action && best.hex == chosen.hex && Near(best.score, chosen.score)
                  && best.threatIndex == chosen.threatIndex, "RL best candidate matches utility AI");
        }

        RlGlobal global{};
        BuildGlobalFeatures(world, data, board, AiFaction, global);
        Check(global.size() == RlGlobalFeatures && std::all_of(global.begin(), global.end(), [](float value)
        {
            return std::isfinite(value) && value >= -5.0f && value <= 5.0f;
        }), "RL global features have the expected size and range");

        std::vector<RlInput> inputs;
        BuildCandidateFeatures(world, data, board, AiFaction, ownDivision, global, candidates, inputs);
        Check(inputs.size() == candidates.size(), "RL features include one input per candidate");
        for (const RlInput& input : inputs)
        {
            Check(input.size() == RlInputFeatures && std::all_of(input.begin(), input.end(), [](float value)
            {
                return std::isfinite(value);
            }), "RL input has the expected size and finite features");
            Check(std::equal(global.begin(), global.end(), input.begin()), "RL input starts with global features");
        }
        if (!inputs.empty() && inputs.size() == candidates.size())
        {
            constexpr int candidateOffset = RlGlobalFeatures + RlDivisionFeatures;
            Check(Near(inputs.front()[candidateOffset], 1.0f) && Near(inputs.front()[candidateOffset + 6], 0.0f),
                  "RL Hold encodes its action and zero target distance");
            Check(Near(inputs[BestCandidate(candidates)][candidateOffset + 14], 1.0f), "RL best candidate is marked");
        }

        const RlRewardSnapshot before = TakeRewardSnapshot(world, AiFaction);
        ++world.factions[AiFaction].ownedHexes;
        Check(ComputeReward(before, TakeRewardSnapshot(world, AiFaction), world, AiFaction) > 0.0f,
              "RL reward increases when own territory grows");
        --world.factions[AiFaction].ownedHexes;

        for (const auto [entity, base, faction] : world.registry.view<Base, Faction>().each())
        {
            if (faction.id == PlayerFaction)
            {
                faction.id = AiFaction;
                break;
            }
        }
        const RlRewardSnapshot captured = TakeRewardSnapshot(world, AiFaction);
        Check(ComputeReward(before, captured, world, AiFaction) > 0.0f, "RL reward increases when enemy loses a base");
        Check(ComputeReward(captured, before, world, AiFaction) < 0.0f, "RL reward decreases when own base is lost");

        world.over = true;
        world.winner = AiFaction;
        Check(ComputeReward(captured, captured, world, AiFaction) > 0.0f, "RL terminal win is rewarded");
        Check(ComputeReward(captured, captured, world, PlayerFaction) < 0.0f, "RL terminal loss is penalized");
    }

    // 경기 중 Lua 스크립트: 읽기는 되고 쓰기·무한 루프는 오류로 끊긴다.
    void TestMatchScript(const GameData& data)
    {
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "minrts_script_test";
        std::filesystem::create_directories(dir);
        const auto run = [&](const std::string& name, const std::string& code)
        {
            const std::filesystem::path file = dir / (name + ".lua");
            std::ofstream(file) << code;
            MatchSetup setup = CampaignSetup(data, 0, 1, data.defaultTemplates, true);
            setup.scriptPath = file.string();
            World world;
            InitWorld(world, data, setup, 3);
            const float dt = 1.0f / data.config.tickHz;
            for (int i = 0; i < static_cast<int>(2.5f * data.config.tickHz); ++i)
            {
                StepWorld(world, data, dt);
            }
            std::vector<std::string> lines;
            for (const GameEvent& event : world.events)
            {
                lines.push_back(event.text);
            }
            return std::pair{ std::move(lines), world.script.Loaded() };
        };
        const auto has = [](const std::vector<std::string>& lines, const std::string& text)
        {
            return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) { return line.find(text) != std::string::npos; });
        };

        const auto [readLines, readLoaded] = run("read", R"(
            interval = 1
            function on_start(game) print("start", #game.factions, game.factions[2].id) end
            function on_tick(game)
                local n = 0
                for _, d in ipairs(game.divisions) do n = n + #d.battalions end
                local keys = 0
                for _ in pairs(game.factions[1].stock) do keys = keys + 1 end
                print("tick", game.time > 0, n > 0, keys)
            end)");
        Check(has(readLines, "[script] start\t2\t1"), "script: on_start reads factions");
        Check(has(readLines, "[script] tick\ttrue\ttrue\t6"), "script: on_tick reads divisions, battalions and stock via pairs");
        Check(readLoaded, "script: stays loaded without errors");

        const auto [writeLines, writeLoaded] = run("write", "function on_tick(game) game.time = 5 end");
        Check(has(writeLines, "game is read-only") && !writeLoaded, "script: writing game is an error and stops the script");
        const auto [nestedLines, nestedLoaded] = run("nested", "function on_tick(game) game.factions[1].manpower = 1e9 end");
        Check(has(nestedLines, "game is read-only") && !nestedLoaded, "script: nested tables are read-only too");
        const auto [rawLines, rawLoaded] = run("raw", "function on_tick(game) rawset(game, 'time', 1) end");
        Check(has(rawLines, "Script error") && !rawLoaded, "script: rawset is not available");
        const auto [loopLines, loopLoaded] = run("loop", "function on_tick(game) while true do end end");
        Check(has(loopLines, "took too long") && !loopLoaded, "script: infinite loop is cut off");
        std::filesystem::remove_all(dir);

        Check(data.ai.raidRadiusKm == 150.0f && data.ai.standoffRatio == 0.7f, "ai_tuning: read from game_data.lua");
    }

    void TestMatchLength(const GameData& data)
    {
        MatchSetup setup = CampaignSetup(data, 0, 1, data.defaultTemplates, true);
        setup.matchSeconds = 0.0f;   // 무제한
        World world;
        InitWorld(world, data, setup, 5);
        world.time = 100000.0f;
        CheckVictory(world, data);
        Check(!world.over, "unlimited match never ends on time");
        world.matchSeconds = 60.0f;
        CheckVictory(world, data);
        Check(world.over, "timed match ends at its limit");
    }

    void TestProfiler(const GameData& data)
    {
        Profiler off;
        { ProfileScope scope(off, ProfileSection::Combat); }
        Check(off.stats[static_cast<std::size_t>(ProfileSection::Combat)].calls == 0, "disabled profiler records nothing");

        World world;
        InitWorld(world, data, CampaignSetup(data, 0, 1, data.defaultTemplates, true), 3);
        world.profiler.enabled = true;
        for (int i = 0; i < 100; ++i)
        {
            StepWorld(world, data, 0.1f);
        }
        const Profiler& p = world.profiler;
        Check(p.ticks == 100 && p.stats[static_cast<std::size_t>(ProfileSection::Combat)].calls == 100
              && p.stats[static_cast<std::size_t>(ProfileSection::Occupancy)].calls == 200, "every tick section timed");
        Check(p.stats[static_cast<std::size_t>(ProfileSection::AiUtility)].calls > 0 && world.factions[AiFaction].aiProfile.thinks > 0, "ai layers timed");
        const std::string report = ProfileReport(world, 1.0);
        Check(report.find("ai.htn") != std::string::npos && report.find("AI Red") != std::string::npos, "report lists sections and ai");
        ResetProfiler(world.profiler);
        Check(world.profiler.enabled && world.profiler.ticks == 0, "reset keeps enabled");
    }
}

int main(int argc, char** argv)
{
    const std::string path = argc > 1 ? argv[1] : "data/game_data.lua";
    GameData data;
    std::string error;
    if (!LoadGameData(path, data, error))
    {
        std::cerr << "load failed: " << error << '\n';
        return 1;
    }
    Check(data.battalions.size() == 20, "20 battalions loaded");

    TestHex();
    TestSplines();
    TestMapFeatures(data);
    TestRules(data);
    TestTemplates(data);
    TestWorld(data);
    TestFrontage(data);

    const std::string dataDir = path.substr(0, path.find_last_of("/\\") + 1);
    TestGeoJson(data, dataDir);
    TestQgisLayers(data);
    TestScenarios(data, dataDir);
    TestPhrases(dataDir);
    TestEconomy(data);
    TestAi(data);
    TestRlFeatures(data);
    TestProfiler(data);
    TestMatchLength(data);
    TestMatchScript(data);

    std::cout << (g_failures == 0 ? "ALL PASS" : "FAILED") << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
