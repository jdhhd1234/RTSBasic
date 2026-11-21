#include "sim/profile_report.hpp"
#include "sim/components.hpp"

#include <format>

double TickTotalMs(const Profiler& profiler)
{
    double total = 0.0;
    for (std::size_t i = 0; i < ProfileSectionCount; ++i)
    {
        if (IsTickSection(static_cast<ProfileSection>(i)))
        {
            total += profiler.stats[i].totalMs;
        }
    }
    return total;
}

std::string ProfileReport(const World& world, double wallSeconds)
{
    const Profiler& profiler = world.profiler;
    const double tickTotal = TickTotalMs(profiler);
    std::string out = std::format("MinRTS profile: {}\nsim time {:.1f}s  ticks {}  divisions {}  bases {}\n",
        world.title, world.time, profiler.ticks, world.registry.view<Division>().size(), world.registry.view<Base>().size());
    if (wallSeconds > 0.0)
    {
        out += std::format("wall {:.2f}s  speed x{:.1f} realtime  avg tick {:.3f} ms\n",
            wallSeconds, world.time / wallSeconds, profiler.ticks > 0 ? tickTotal / static_cast<double>(profiler.ticks) : 0.0);
    }

    out += std::format("\n{:<15}{:>10}{:>10}{:>12}{:>8}{:>10}\n", "section", "avg ms", "max ms", "total ms", "%tick", "calls");
    for (std::size_t i = 0; i < ProfileSectionCount; ++i)
    {
        const ProfileStat& stat = profiler.stats[i];
        if (stat.calls == 0)
        {
            continue;
        }
        const bool drawSection = static_cast<ProfileSection>(i) >= ProfileSection::DrawMap;
        const std::string percent = drawSection || tickTotal <= 0.0 ? "-" : std::format("{:.1f}", 100.0 * stat.totalMs / tickTotal);
        out += std::format("{:<15}{:>10.3f}{:>10.3f}{:>12.1f}{:>8}{:>10}\n",
            ProfileSectionNames[i], stat.avgMs, stat.maxMs, stat.totalMs, percent, stat.calls);
    }

    for (const FactionState& state : world.factions)
    {
        if (!state.aiControlled)
        {
            continue;
        }
        const AiProfile& ai = state.aiProfile;
        const AiBlackboard& board = state.blackboard;
        out += std::format("\nAI {}: thinks {}  last {:.3f} ms  max {:.3f} ms  posture {}  objective hex {}  move orders {}\n",
            state.name, ai.thinks, ai.lastThinkMs, ai.maxThinkMs, AiPostureNames[static_cast<std::size_t>(board.posture)], board.attackHex, ai.moveOrders);
        out += "  plan:";
        for (const AiTask& task : board.plan)
        {
            out += std::string(" ") + AiTaskNames[static_cast<std::size_t>(task.type)];
        }
        out += "\n  actions:";
        for (std::size_t a = 0; a < ai.actions.size(); ++a)
        {
            out += std::format(" {} {}", AiActionNames[a], ai.actions[a]);
        }
        out += std::format("\n  strength own {:.0f} / enemy {:.0f}  threats {}  manpower {:.0f}  draft {:.0f}%  factories {}\n",
            board.ownStrength, board.enemyStrength, board.threats.size(), state.manpower, state.draftPressure * 100.0f, state.factories.size());
    }
    return out;
}
