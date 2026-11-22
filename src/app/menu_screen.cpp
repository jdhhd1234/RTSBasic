#include "app/screens.hpp"
#include "app/ui.hpp"

#include <algorithm>
#include <string>

void DrawMenu(AppState& app, const GameData& data)
{
    const float cx = static_cast<float>(GetScreenWidth()) / 2.0f;
    float y = 70.0f;

    const char* title = "MinRTS";
    TextDraw(title, static_cast<int>(cx) - TextWidth(title, 64) / 2, static_cast<int>(y), 64, RAYWHITE);
    y += 90.0f;

    const std::string progress = "Cleared stages: " + std::to_string(app.profile.clearedStages) + " / " + std::to_string(data.stages.size());
    TextDraw(progress.c_str(), static_cast<int>(cx) - TextWidth(progress.c_str(), 20) / 2, static_cast<int>(y), 20, LIGHTGRAY);
    y += 44.0f;

    TextDraw("Campaign stage", static_cast<int>(cx) - 300, static_cast<int>(y), 22, LIGHTGRAY);
    y += 32.0f;
    for (int i = 0; i < static_cast<int>(data.stages.size()); ++i)
    {
        const bool unlocked = i <= app.profile.clearedStages;
        const std::string label = std::to_string(i + 1) + ". " + data.stages[static_cast<std::size_t>(i)].name + (unlocked ? "" : "  (locked)");
        if (UiButton({ cx - 300.0f, y, 600.0f, 44.0f }, label.c_str(), unlocked, app.stage == i))
        {
            app.stage = i;
        }
        y += 52.0f;
    }

    y += 10.0f;
    TextDraw("Difficulty", static_cast<int>(cx) - 300, static_cast<int>(y), 22, LIGHTGRAY);
    y += 32.0f;
    const float width = 600.0f / static_cast<float>(data.difficulties.size()) - 8.0f;
    for (int i = 0; i < static_cast<int>(data.difficulties.size()); ++i)
    {
        const Rectangle rect = { cx - 300.0f + static_cast<float>(i) * (width + 8.0f), y, width, 44.0f };
        if (UiButton(rect, data.difficulties[static_cast<std::size_t>(i)].name.c_str(), true, app.difficulty == i))
        {
            app.difficulty = i;
        }
    }
    y += 64.0f;

    TextDraw("Match length", static_cast<int>(cx) - 300, static_cast<int>(y), 22, LIGHTGRAY);
    y += 32.0f;
    DrawMatchLengthPicker(app, cx - 300.0f, y, 600.0f);
    y += 60.0f;

    if (UiButton({ cx - 300.0f, y, 600.0f, 40.0f }, app.botBattle ? "[x] Bot battle (AI vs AI, spectate)" : "[ ] Bot battle (AI vs AI, spectate)", true, app.botBattle))
    {
        app.botBattle = !app.botBattle;
    }
    y += 52.0f;
    y += DrawRlToggle(app, cx - 300.0f, y, 600.0f);

    if (UiButton({ cx - 300.0f, y, 600.0f, 50.0f }, "Start Campaign", !app.profile.templates.empty()))
    {
        MatchSetup setup = CampaignSetup(data, app.stage, app.difficulty, app.profile.templates, app.botBattle);
        ApplyMatchLength(app, setup);
        StartMatch(app, data, setup);
    }
    y += 60.0f;
    if (UiButton({ cx - 300.0f, y, 296.0f, 44.0f }, "Sandbox"))
    {
        OpenSandbox(app);
    }
    if (UiButton({ cx + 4.0f, y, 296.0f, 44.0f }, "Division Templates"))
    {
        app.editor.templates = app.profile.templates;
        app.editor.current = 0;
        app.screen = Screen::Templates;
    }
    y += 52.0f;
    if (UiButton({ cx - 300.0f, y, 600.0f, 44.0f }, "Quit"))
    {
        app.quit = true;
    }

    // 언어 전환: 영어 <-> 한국어. 실패하면 SetLanguage가 영어로 되돌린다.
    const bool korean = CurrentLanguage() == "ko";
    if (UiButton({ static_cast<float>(GetScreenWidth()) - 232.0f, 16.0f, 216.0f, 40.0f }, korean ? "Language: 한국어" : "Language: English"))
    {
        std::string error;
        SetLanguage(app.dataDir, korean ? "en" : "ko", error);
        app.profile.language = CurrentLanguage();
        SaveProfile(app.profilePath, app.profile, data);
    }
}

void DrawResult(AppState& app)
{
    const float cx = static_cast<float>(GetScreenWidth()) / 2.0f;
    const float cy = static_cast<float>(GetScreenHeight()) / 2.0f;

    const Color color = app.resultGood ? UiColors::Player : UiColors::Ai;
    TextDraw(app.resultTitle.c_str(), static_cast<int>(cx) - TextWidth(app.resultTitle.c_str(), 72) / 2, static_cast<int>(cy) - 140, 72, color);
    TextDraw(app.resultText.c_str(), static_cast<int>(cx) - TextWidth(app.resultText.c_str(), 24) / 2, static_cast<int>(cy) - 40, 24, RAYWHITE);
    TextDraw(app.world.title.c_str(), static_cast<int>(cx) - TextWidth(app.world.title.c_str(), 20) / 2, static_cast<int>(cy), 20, LIGHTGRAY);

    if (UiButton({ cx - 150.0f, cy + 60.0f, 300.0f, 50.0f }, "Back to Menu"))
    {
        app.stage = std::min(app.stage, app.profile.clearedStages);
        app.screen = Screen::Menu;
    }
}

void DrawMatchLengthPicker(AppState& app, float x, float y, float width)
{
    const float buttonWidth = width / static_cast<float>(MatchLengthMinutes.size()) - 8.0f;
    for (std::size_t i = 0; i < MatchLengthMinutes.size(); ++i)
    {
        const int minutes = MatchLengthMinutes[i];
        const std::string label = minutes == 0 ? "Default" : (minutes < 0 ? "Unlimited" : std::to_string(minutes) + " min");
        const Rectangle rect = { x + static_cast<float>(i) * (buttonWidth + 8.0f), y, buttonWidth, 44.0f };
        if (UiButton(rect, label.c_str(), true, app.matchLength == static_cast<int>(i)))
        {
            app.matchLength = static_cast<int>(i);
        }
    }
}

void ApplyMatchLength(const AppState& app, MatchSetup& setup)
{
    const int minutes = MatchLengthMinutes[static_cast<std::size_t>(app.matchLength)];
    if (minutes != 0)
    {
        setup.matchSeconds = minutes < 0 ? 0.0f : static_cast<float>(minutes) * 60.0f;
    }
}

float DrawRlToggle(AppState& app, float x, float y, float width)
{
#ifdef MINRTS_TORCH
    if (UiButton({ x, y, width, 40.0f }, app.rlAi ? "[x] RL AI (data/ai/rl_policy.pt)" : "[ ] RL AI (data/ai/rl_policy.pt)", true, app.rlAi))
    {
        app.rlAi = !app.rlAi;
    }
    return 52.0f;
#else
    (void)app;
    (void)x;
    (void)y;
    (void)width;
    return 0.0f;
#endif
}
