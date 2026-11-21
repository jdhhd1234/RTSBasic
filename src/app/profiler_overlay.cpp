#include "app/screens.hpp"
#include "app/ui.hpp"
#include "sim/components.hpp"
#include "sim/profile_report.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>

// 샌드박스 프로파일러 오버레이 (F3). 구간별 시간, 틱 비중, AI 판단 통계.

namespace
{
    constexpr float Width = 430.0f;

    void Line(const std::string& text, float x, float& y, int size = 16, Color color = LIGHTGRAY)
    {
        TextDraw(text.c_str(), static_cast<int>(x), static_cast<int>(y), size, color);
        y += static_cast<float>(size) + 4.0f;
    }

    // 구간 하나: 이름, 평균, 최대, 틱 비중 막대.
    void SectionRow(const ProfileStat& stat, const char* name, double tickTotal, bool tickShare, float x, float& y)
    {
        TextDraw(name, static_cast<int>(x), static_cast<int>(y), 16, LIGHTGRAY);
        TextDraw(std::format("{:.3f}", stat.avgMs).c_str(), static_cast<int>(x) + 130, static_cast<int>(y), 16, RAYWHITE);
        TextDraw(std::format("{:.2f}", stat.maxMs).c_str(), static_cast<int>(x) + 200, static_cast<int>(y), 16, GRAY);
        if (tickShare && tickTotal > 0.0)
        {
            const float share = static_cast<float>(stat.totalMs / tickTotal);
            const Color color = share > 0.4f ? Color{ 230, 90, 70, 255 } : (share > 0.2f ? Color{ 230, 190, 60, 255 } : Color{ 90, 190, 110, 255 });
            DrawRectangle(static_cast<int>(x) + 265, static_cast<int>(y) + 4, static_cast<int>(100.0f * std::min(share, 1.0f)), 10, color);
            TextDraw(std::format("{:.0f}%", share * 100.0f).c_str(), static_cast<int>(x) + 370, static_cast<int>(y), 16, LIGHTGRAY);
        }
        y += 20.0f;
    }
}

void DrawProfilerOverlay(AppState& app, float top)
{
    GameView& view = app.view;
    World& world = app.world;
    Profiler& profiler = world.profiler;
    if (!view.profilerOpen)
    {
        return;
    }

    const float x = static_cast<float>(GetScreenWidth()) - Width - 350.0f;   // 오른쪽 사단 정보 패널 왼쪽
    const Rectangle rect = { x, top, Width, 560.0f };
    UiPanel(rect);
    const float left = x + 12.0f;
    float y = top + 10.0f;

    TextDraw("Profiler (F3)", static_cast<int>(left), static_cast<int>(y), 20, UiColors::Accent);
    if (UiButton({ x + Width - 170.0f, y - 2.0f, 76.0f, 28.0f }, "Reset"))
    {
        ResetProfiler(profiler);
        for (FactionState& state : world.factions)
        {
            state.aiProfile = AiProfile{};
        }
    }
    if (UiButton({ x + Width - 88.0f, y - 2.0f, 76.0f, 28.0f }, "Save"))
    {
        const std::filesystem::path path = std::filesystem::path(app.dataDir).parent_path().parent_path() / "profile_report.txt";
        std::ofstream file(path);
        file << ProfileReport(world);
        view.profileMessage = file ? "saved " + path.string() : "save failed " + path.string();
    }
    y += 32.0f;

    const double tickTotal = TickTotalMs(profiler);
    const double tickAvg = profiler.ticks > 0 ? tickTotal / static_cast<double>(profiler.ticks) : 0.0;
    Line(std::format("FPS {}   frame {:.1f} ms   ticks/frame {}", GetFPS(), GetFrameTime() * 1000.0f, view.lastTicks), left, y);
    Line(std::format("tick avg {:.3f} ms   ticks {}   divisions {}", tickAvg, profiler.ticks, world.registry.view<Division>().size()), left, y);

    y += 4.0f;
    TextDraw("section", static_cast<int>(left), static_cast<int>(y), 16, GRAY);
    TextDraw("avg ms", static_cast<int>(left) + 130, static_cast<int>(y), 16, GRAY);
    TextDraw("max", static_cast<int>(left) + 200, static_cast<int>(y), 16, GRAY);
    TextDraw("%tick", static_cast<int>(left) + 265, static_cast<int>(y), 16, GRAY);
    y += 20.0f;
    for (std::size_t i = 0; i < ProfileSectionCount; ++i)
    {
        const ProfileStat& stat = profiler.stats[i];
        if (stat.calls > 0)
        {
            const ProfileSection section = static_cast<ProfileSection>(i);
            SectionRow(stat, ProfileSectionNames[i], tickTotal, section < ProfileSection::DrawMap, left, y);
        }
    }

    for (const FactionState& state : world.factions)
    {
        if (!state.aiControlled)
        {
            continue;
        }
        const AiProfile& ai = state.aiProfile;
        const AiBlackboard& board = state.blackboard;
        y += 4.0f;
        Line(std::format("AI {}: think {:.2f} ms (max {:.2f})  {}  moves {}", state.name, ai.lastThinkMs, ai.maxThinkMs,
                         AiPostureNames[static_cast<std::size_t>(board.posture)], ai.moveOrders), left, y, 16, RAYWHITE);
        std::string plan = "plan:";
        for (const AiTask& task : board.plan)
        {
            plan += std::string(" ") + AiTaskNames[static_cast<std::size_t>(task.type)];
        }
        Line(plan, left + 10.0f, y);
        std::string actions;
        for (std::size_t a = 0; a < ai.actions.size(); ++a)
        {
            actions += std::format("{} {}  ", AiActionNames[a], ai.actions[a]);
        }
        Line(actions, left + 10.0f, y);
    }

    if (!view.profileMessage.empty())
    {
        y += 4.0f;
        Line(view.profileMessage, left, y, 14, GRAY);
    }
}
