#include "ai/ai.hpp"
#include "core/lua_loader.hpp"
#include "core/phrases.hpp"
#include "core/templates.hpp"
#include "sim/components.hpp"
#include "sim/economy.hpp"
#include "sim/geojson_map.hpp"
#include "sim/profile_report.hpp"
#include "sim/rules.hpp"
#include "sim/scenario.hpp"
#include "sim/simulation.hpp"
#include "sim/systems.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numeric>
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
        std::string error;
        const bool ok = LoadGeoJsonMap(dataDir + "maps/rhineland.geojson", data, map, error);
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
        Check(!LoadGeoJsonMap(dataDir + "game_data.lua", data, bad, error), "non-geojson file rejected");
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
        Check(ChooseDivisionAction(world, red, AiFaction, hurt).action == AiAction::Recover, "utility: badly hurt -> recover");
        division.hp = division.maxHp;
        red.posture = AiPosture::Attack;
        red.attackHex = red.objectives.front().hex;
        red.attackPosition = red.objectives.front().position;
        Check(ChooseDivisionAction(world, red, AiFaction, hurt).action == AiAction::Attack, "utility: attack posture -> attack");
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
    TestRules(data);
    TestTemplates(data);
    TestWorld(data);

    const std::string dataDir = path.substr(0, path.find_last_of("/\\") + 1);
    TestGeoJson(data, dataDir);
    TestScenarios(data, dataDir);
    TestPhrases(dataDir);
    TestEconomy(data);
    TestAi(data);
    TestProfiler(data);

    std::cout << (g_failures == 0 ? "ALL PASS" : "FAILED") << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
