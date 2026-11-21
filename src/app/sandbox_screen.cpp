#include "app/screens.hpp"
#include "app/ui.hpp"
#include "sim/geojson_map.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>

namespace
{
    std::vector<std::string> ListFiles(const std::string& dir, const std::string& extension)
    {
        std::vector<std::string> result;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (entry.is_regular_file() && entry.path().extension() == extension)
            {
                result.push_back(entry.path().string());
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    std::string FileName(const std::string& path)
    {
        return std::filesystem::path(path).filename().string();
    }

    void SelectScenario(AppState& app, const GameData& data, int index)
    {
        SandboxState& sb = app.sandbox;
        sb.selected = index;
        sb.error.clear();
        sb.loaded = LoadScenario(sb.scenarioFiles[static_cast<std::size_t>(index)], data, app.profile.templates, sb.scenario, sb.error);
        if (sb.loaded)
        {
            sb.botBattle = sb.scenario.setup.sides[PlayerFaction].ai;
            sb.mapChoice = 0;
        }
    }

    // 메뉴에서 고른 맵 설정을 시나리오 위에 덮어쓴다.
    bool BuildSetup(AppState& app, MatchSetup& setup)
    {
        SandboxState& sb = app.sandbox;
        setup = sb.scenario.setup;
        const GameConfig& config = sb.scenario.data.config;
        if (sb.mapChoice == 1)
        {
            SetGeneratedMapSize(setup, config, static_cast<float>(sb.randomSizeKm), static_cast<float>(sb.randomSizeKm));
            setup.mapSeed = sb.randomSeed;
            setup.bases.clear();   // 고정 기지 좌표는 원래 맵 기준이라 버린다.
        }
        else if (sb.mapChoice >= 2)
        {
            std::string error;
            if (!LoadGeoJsonMap(sb.mapFiles[static_cast<std::size_t>(sb.mapChoice - 2)], sb.scenario.data, setup.map, setup.layers, error))
            {
                sb.error = error;
                return false;
            }
            if (!error.empty())
            {
                std::clog << error << '\n';
            }
            setup.customMap = true;
            setup.bases.clear();
        }

        SideSetup& blue = setup.sides[PlayerFaction];
        if (sb.botBattle && !blue.ai)
        {
            blue.ai = true;
            blue.incomeMultiplier = sb.scenario.data.difficulties[static_cast<std::size_t>(blue.difficulty)].aiIncome;
        }
        else if (!sb.botBattle)
        {
            blue.ai = false;
        }
        return true;
    }
}

void OpenSandbox(AppState& app)
{
    SandboxState& sb = app.sandbox;
    sb.scenarioFiles = ListFiles(app.dataDir + "sandbox", ".lua");
    sb.mapFiles = ListFiles(app.dataDir + "maps", ".geojson");
    sb.selected = -1;
    sb.loaded = false;
    sb.error.clear();
    sb.randomSeed = static_cast<int>(std::random_device{}() % 10000u);
    app.screen = Screen::Sandbox;
}

void DrawSandbox(AppState& app, const GameData& data)
{
    SandboxState& sb = app.sandbox;

    TextDraw("Sandbox", 40, 30, 36, RAYWHITE);
    TextDraw("Scenarios are Lua files in data/sandbox (see README.txt there). Maps: data/maps/*.geojson (QGIS export).", 40, 74, 18, LIGHTGRAY);

    // 왼쪽: 시나리오 목록
    float y = 120.0f;
    if (sb.scenarioFiles.empty())
    {
        TextDraw("No scenarios found in data/sandbox", 40, static_cast<int>(y), 20, UiColors::Ai);
    }
    for (int i = 0; i < static_cast<int>(sb.scenarioFiles.size()); ++i)
    {
        if (UiButton({ 40.0f, y, 360.0f, 40.0f }, FileName(sb.scenarioFiles[static_cast<std::size_t>(i)]).c_str(), true, sb.selected == i))
        {
            SelectScenario(app, data, i);
        }
        y += 46.0f;
    }
    if (UiButton({ 40.0f, y + 10.0f, 360.0f, 40.0f }, "Reload files"))
    {
        const int previous = sb.selected;
        OpenSandbox(app);
        if (previous >= 0 && previous < static_cast<int>(sb.scenarioFiles.size()))
        {
            SelectScenario(app, data, previous);
        }
    }

    const float x = 440.0f;
    y = 120.0f;
    if (!sb.error.empty())
    {
        TextDraw(sb.error.c_str(), static_cast<int>(x), static_cast<int>(y), 18, UiColors::Ai);
        y += 30.0f;
    }

    if (sb.loaded)
    {
        const Scenario& sc = sb.scenario;
        TextDraw(sc.name.c_str(), static_cast<int>(x), static_cast<int>(y), 28, RAYWHITE);
        y += 36.0f;
        TextDraw(sc.description.c_str(), static_cast<int>(x), static_cast<int>(y), 18, LIGHTGRAY);
        y += 28.0f;
        TextDraw(("Scenario map: " + sc.mapLabel + "    Match: " + std::to_string(static_cast<int>(sc.setup.matchSeconds)) + "s").c_str(),
                 static_cast<int>(x), static_cast<int>(y), 18, LIGHTGRAY);
        y += 28.0f;

        if (!sc.customBattalions.empty())
        {
            std::string custom = "Custom battalions:";
            for (const std::string& name : sc.customBattalions)
            {
                custom += " " + name + ",";
            }
            custom.pop_back();
            TextDraw(custom.c_str(), static_cast<int>(x), static_cast<int>(y), 18, UiColors::Accent);
            y += 28.0f;
        }

        for (int f = 0; f < FactionCount; ++f)
        {
            const SideSetup& side = sc.setup.sides[static_cast<std::size_t>(f)];
            std::string line = side.name + (side.ai ? "  [AI " + sc.data.difficulties[static_cast<std::size_t>(side.difficulty)].name + "]" : "  [Player]")
                + "  income x" + std::to_string(side.incomeMultiplier).substr(0, 4)
                + "  start divisions " + std::to_string(side.initialDivisions.size()) + "  templates:";
            for (const TemplateDef& t : side.templates)
            {
                line += " " + t.name + ",";
            }
            line.pop_back();
            TextDraw(line.c_str(), static_cast<int>(x), static_cast<int>(y), 18, f == PlayerFaction ? UiColors::Player : UiColors::Ai);
            y += 26.0f;
        }

        // 맵 설정
        y += 18.0f;
        TextDraw("Map", static_cast<int>(x), static_cast<int>(y), 22, LIGHTGRAY);
        y += 30.0f;
        std::vector<std::string> choices = { "Scenario map", "Random" };
        for (const std::string& file : sb.mapFiles)
        {
            choices.push_back(FileName(file));
        }
        float bx = x;
        for (int i = 0; i < static_cast<int>(choices.size()); ++i)
        {
            const float w = static_cast<float>(TextWidth(choices[static_cast<std::size_t>(i)].c_str(), 18)) + 30.0f;
            if (bx + w > static_cast<float>(GetScreenWidth()) - 40.0f)
            {
                bx = x;
                y += 46.0f;
            }
            if (UiButton({ bx, y, w, 38.0f }, choices[static_cast<std::size_t>(i)].c_str(), true, sb.mapChoice == i))
            {
                sb.mapChoice = i;
            }
            bx += w + 8.0f;
        }
        y += 50.0f;

        if (sb.mapChoice == 1)
        {
            const std::string size = "Size " + std::to_string(sb.randomSizeKm) + " km";
            TextDraw(size.c_str(), static_cast<int>(x), static_cast<int>(y) + 10, 20, RAYWHITE);
            if (UiButton({ x + 170.0f, y, 40.0f, 38.0f }, "-", sb.randomSizeKm > static_cast<int>(data.config.mapMinKm)))
            {
                sb.randomSizeKm -= 100;
            }
            if (UiButton({ x + 216.0f, y, 40.0f, 38.0f }, "+", sb.randomSizeKm < static_cast<int>(data.config.mapMaxKm)))
            {
                sb.randomSizeKm += 100;
            }
            const std::string seed = "Seed " + std::to_string(sb.randomSeed);
            TextDraw(seed.c_str(), static_cast<int>(x) + 290, static_cast<int>(y) + 10, 20, RAYWHITE);
            if (UiButton({ x + 410.0f, y, 40.0f, 38.0f }, "-"))
            {
                sb.randomSeed = std::max(0, sb.randomSeed - 1);
            }
            if (UiButton({ x + 456.0f, y, 40.0f, 38.0f }, "+"))
            {
                sb.randomSeed += 1;
            }
            if (UiButton({ x + 502.0f, y, 90.0f, 38.0f }, "Shuffle"))
            {
                sb.randomSeed = static_cast<int>(std::random_device{}() % 10000u);
            }
            y += 50.0f;
        }

        y += 10.0f;
        if (UiButton({ x, y, 520.0f, 40.0f }, sb.botBattle ? "[x] Bot battle (AI vs AI, spectate)" : "[ ] Bot battle (AI vs AI, spectate)", true, sb.botBattle))
        {
            sb.botBattle = !sb.botBattle;
        }
        y += 46.0f;
        if (UiButton({ x, y, 520.0f, 40.0f }, sb.profiler ? "[x] Profiler (F3 in game)" : "[ ] Profiler (F3 in game)", true, sb.profiler))
        {
            sb.profiler = !sb.profiler;
        }
        y += 56.0f;

        if (UiButton({ x, y, 520.0f, 50.0f }, "Start Sandbox"))
        {
            MatchSetup setup;
            if (BuildSetup(app, setup))
            {
                StartMatch(app, sb.scenario.data, setup);
                app.view.profilerOpen = sb.profiler;
                app.world.profiler.enabled = sb.profiler;
            }
        }
    }

    if (UiButton({ 40.0f, static_cast<float>(GetScreenHeight()) - 80.0f, 220.0f, 48.0f }, "Back"))
    {
        app.screen = Screen::Menu;
    }
}
