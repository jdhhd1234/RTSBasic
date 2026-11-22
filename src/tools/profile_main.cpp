// 창 없이 샌드박스 시나리오를 끝까지 돌려 프로파일 리포트를 출력한다.
//   minrts_profile [scenario.lua] [--seconds N] [--seed N] [--keep-player] [--out report.txt]
// 기본: data/sandbox/bot_battle.lua, 시나리오 경기 시간 전체, 양쪽 모두 AI.

#include "core/lua_loader.hpp"
#include "sim/profile_report.hpp"
#include "sim/scenario.hpp"
#include "sim/simulation.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    const std::filesystem::path exeDir = std::filesystem::absolute(argv[0]).parent_path();
    const std::string dataDir = (exeDir / "data").string() + "/";

    std::string scenarioPath = dataDir + "sandbox/bot_battle.lua";
    float seconds = -1.0f;
    unsigned seed = 1;
    bool keepPlayer = false;
    std::string outPath;
    const std::vector<std::string> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const bool hasValue = i + 1 < args.size();
        if (args[i] == "--seconds" && hasValue)
        {
            seconds = std::stof(args[++i]);
        }
        else if (args[i] == "--seed" && hasValue)
        {
            seed = static_cast<unsigned>(std::stoul(args[++i]));
        }
        else if (args[i] == "--out" && hasValue)
        {
            outPath = args[++i];
        }
        else if (args[i] == "--keep-player")
        {
            keepPlayer = true;
        }
        else if (!args[i].starts_with("--"))
        {
            scenarioPath = args[i];
        }
        else
        {
            std::cerr << "unknown option " << args[i] << '\n'
                      << "usage: minrts_profile [scenario.lua] [--seconds N] [--seed N] [--keep-player] [--out report.txt]\n";
            return 2;
        }
    }

    GameData data;
    std::string error;
    if (!LoadGameData(dataDir + "game_data.lua", data, error))
    {
        std::cerr << "game data: " << error << '\n';
        return 1;
    }
    Scenario scenario;
    if (!LoadScenario(scenarioPath, data, data.defaultTemplates, scenario, error))
    {
        std::cerr << "scenario: " << error << '\n';
        return 1;
    }

    MatchSetup setup = scenario.setup;
    if (!keepPlayer)
    {
        setup.sides[PlayerFaction].ai = true;   // 사람이 없으니 청군도 AI가 움직여야 의미 있는 측정이 된다
    }
    if (seconds > 0.0f)
    {
        setup.matchSeconds = seconds;
    }
    const float runSeconds = setup.matchSeconds > 0.0f ? setup.matchSeconds : 1200.0f;   // 무제한 시나리오도 끝은 있어야 한다

    World world;
    InitWorld(world, scenario.data, setup, seed);
    world.profiler.enabled = true;

    const float dt = 1.0f / scenario.data.config.tickHz;
    const auto start = std::chrono::steady_clock::now();
    while (!world.over && world.time < runSeconds + dt)
    {
        StepWorld(world, scenario.data, dt);
    }
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::string report = ProfileReport(world, wall);
    report += "\nresult: " + (world.endReason.empty() ? std::string("not finished") : world.endReason) + "\n";
    std::cout << report;
    if (!outPath.empty())
    {
        std::ofstream(outPath) << report;
    }
    return 0;
}
