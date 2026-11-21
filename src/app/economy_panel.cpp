#include "app/screens.hpp"
#include "app/render.hpp"
#include "app/ui.hpp"
#include "core/templates.hpp"
#include "sim/economy.hpp"

#include <algorithm>
#include <string>

// 왼쪽 경제 탭: 인력·징집, 자원·광산, 공장·건설. 관전 중에는 진영을 골라 보기만 한다.

namespace
{
    constexpr float PanelWidth = 330.0f;
    constexpr float Pad = 12.0f;
    constexpr Color Bad = { 255, 140, 120, 255 };

    std::string Fixed(float value, int decimals)
    {
        return TextFormat(decimals == 1 ? "%.1f" : "%.2f", value);
    }

    void SectionTitle(const char* title, float x, float& y)
    {
        y += 6.0f;
        TextDraw(title, static_cast<int>(x), static_cast<int>(y), 20, UiColors::Accent);
        y += 26.0f;
    }

    // ratio 0~1 막대. marker(0~1)에 안전선 표시.
    void Bar(float x, float y, float width, float ratio, Color fill, float marker = -1.0f)
    {
        DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(width), 8, Color{ 40, 44, 52, 255 });
        DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(width * std::clamp(ratio, 0.0f, 1.0f)), 8, fill);
        if (marker >= 0.0f)
        {
            const int mx = static_cast<int>(x + width * std::clamp(marker, 0.0f, 1.0f));
            DrawLine(mx, static_cast<int>(y) - 2, mx, static_cast<int>(y) + 10, RAYWHITE);
        }
    }

    void DrawManpower(World& world, const GameData& data, int faction, bool control, float x, float& y, std::string& tooltip)
    {
        const GameConfig& c = data.config;
        const FactionState& state = world.factions[static_cast<std::size_t>(faction)];
        const float width = PanelWidth - Pad * 2.0f;

        SectionTitle("Manpower", x, y);
        TextDraw(std::to_string(static_cast<int>(state.manpower)).c_str(), static_cast<int>(x), static_cast<int>(y), 22, RAYWHITE);
        y += 30.0f;

        const bool overPressure = state.draftPressure > c.pressureSafe;
        TextDraw(("Draft " + std::to_string(static_cast<int>(state.draftPressure * 100.0f)) + "%").c_str(), static_cast<int>(x), static_cast<int>(y), 18, overPressure ? Bad : LIGHTGRAY);
        y += 22.0f;
        Bar(x, y, width, state.draftPressure, overPressure ? Bad : Color{ 120, 170, 230, 255 }, c.pressureSafe);
        y += 16.0f;

        TextDraw(("Quality " + std::to_string(static_cast<int>(state.quality * 100.0f)) + "%").c_str(), static_cast<int>(x), static_cast<int>(y), 18, state.quality < 1.0f ? Bad : LIGHTGRAY);
        y += 22.0f;
        Bar(x, y, width, state.quality, state.quality < 0.8f ? Bad : Color{ 90, 200, 110, 255 });
        y += 18.0f;

        if (control)
        {
            const Rectangle button = { x, y, width, 38.0f };
            if (CheckCollisionPointRec(GetMousePosition(), button))
            {
                const bool risky = state.draftPressure + c.conscriptPressure > c.pressureSafe;
                tooltip = Translate("Conscript") + " +" + std::to_string(static_cast<int>(c.conscriptManpower)) + " " + Translate("manpower")
                    + (risky ? "   [" + Translate("risk: draft refusal, desertion, quality drop") + "]" : "");
            }
            if (UiButton(button, "Conscript (C)"))
            {
                ApplyCommand(world, data, Command{ .type = CommandType::Conscript, .faction = faction });
            }
            y += 44.0f;
        }
    }

    void DrawResources(const World& world, int faction, float x, float& y)
    {
        const FactionState& state = world.factions[static_cast<std::size_t>(faction)];
        SectionTitle("Resources / Mines", x, y);

        if (state.fuelShortage)
        {
            TextDraw("FUEL SHORTAGE", static_cast<int>(x), static_cast<int>(y), 18, Bad);
            y += 22.0f;
        }
        TextDraw(("Munitions " + std::to_string(static_cast<int>(state.munitions))).c_str(), static_cast<int>(x), static_cast<int>(y), 18,
                 state.munitions <= 0.0f ? Bad : LIGHTGRAY);
        y += 24.0f;

        for (std::size_t r = 0; r < ResourceCount; ++r)
        {
            int owned = 0;
            int connected = 0;
            for (const Mine& mine : world.mines)
            {
                if (ToIndex(mine.resource) == r && world.map.hexes[static_cast<std::size_t>(mine.hex)].owner == faction)
                {
                    ++owned;
                    connected += mine.connected ? 1 : 0;
                }
            }
            const bool oil = r == ToIndex(Resource::Oil);
            const float net = state.mineRate[r] - (oil ? state.upkeep : 0.0f);

            DrawRectangle(static_cast<int>(x), static_cast<int>(y) + 4, 10, 10, ResourceColor(static_cast<Resource>(r)));
            TextDraw(ResourceNames[r], static_cast<int>(x) + 16, static_cast<int>(y), 18, LIGHTGRAY);
            TextDraw(std::to_string(static_cast<int>(state.stock[r])).c_str(), static_cast<int>(x) + 130, static_cast<int>(y), 18, RAYWHITE);
            TextDraw(((net >= 0.0f ? "+" : "") + Fixed(net, 2)).c_str(), static_cast<int>(x) + 185, static_cast<int>(y), 18, net < 0.0f ? Bad : Color{ 130, 210, 130, 255 });
            // 광산: 공장과 이어진 수 / 가진 수
            TextDraw((std::to_string(connected) + "/" + std::to_string(owned)).c_str(), static_cast<int>(x) + 260, static_cast<int>(y), 18,
                     connected < owned ? Bad : GRAY);
            y += 22.0f;
        }
        TextDraw(("Upkeep oil -" + Fixed(state.upkeep, 2) + "/s").c_str(), static_cast<int>(x), static_cast<int>(y), 16, GRAY);
        y += 20.0f;
        TextDraw("rate/s, mines linked/owned", static_cast<int>(x), static_cast<int>(y), 16, GRAY);
        y += 20.0f;
    }

    void DrawFactories(World& world, const GameData& data, int faction, bool control, float x, float& y, std::string& tooltip)
    {
        const FactionState& state = world.factions[static_cast<std::size_t>(faction)];
        const float width = PanelWidth - Pad * 2.0f;
        SectionTitle("Factories", x, y);

        for (int type = 0; type < static_cast<int>(data.factories.size()); ++type)
        {
            const FactoryDef& def = data.factories[static_cast<std::size_t>(type)];
            const std::string blocker = FactoryBlocker(world, data, faction, type);
            const std::string label = def.name + "  " + std::to_string(ActiveFactories(state, type));
            const Rectangle button = { x, y, width, 36.0f };
            if (CheckCollisionPointRec(GetMousePosition(), button))
            {
                tooltip = Translate("Build") + " " + Translate(def.name) + ": " + Translate(ResourceText(def.cost)) + ", "
                    + Fixed(def.buildDays / 365.0f, 1) + " " + Translate("years") + (blocker.empty() || !control ? "" : "   [" + Translate(blocker) + "]");
            }
            if (UiButton(button, label.c_str(), control && blocker.empty()) && control)
            {
                ApplyCommand(world, data, Command{ .type = CommandType::BuildFactory, .faction = faction, .factoryType = type });
            }
            y += 40.0f;

            // 건설 중인 공장 진행 막대
            for (const FactoryState& factory : state.factories)
            {
                if (factory.type == type && factory.daysLeft > 0.0f)
                {
                    const float progress = 1.0f - factory.daysLeft / def.buildDays;
                    Bar(x + 8.0f, y, width - 70.0f, progress, Color{ 230, 190, 60, 255 });
                    TextDraw((std::to_string(static_cast<int>(progress * 100.0f)) + "%").c_str(), static_cast<int>(x + width - 54.0f), static_cast<int>(y) - 5, 16, LIGHTGRAY);
                    y += 16.0f;
                }
            }
        }
    }
}

Rectangle EconomyPanelRect(const GameView& view, float top, float bottomMargin)
{
    if (!view.economyOpen)
    {
        return { 0.0f, top + 6.0f, 110.0f, 32.0f };   // 펼치기 손잡이
    }
    return { 0.0f, top, PanelWidth, static_cast<float>(GetScreenHeight()) - top - bottomMargin };
}

void DrawEconomyPanel(AppState& app, const GameData& data, float top, float bottomMargin)
{
    GameView& view = app.view;
    const Rectangle rect = EconomyPanelRect(view, top, bottomMargin);
    if (!view.economyOpen)
    {
        if (UiButton(rect, "Economy >"))
        {
            view.economyOpen = true;
        }
        return;
    }

    UiPanel(rect);
    const bool spectating = app.world.factions[PlayerFaction].aiControlled;
    const int faction = spectating ? view.economyFaction : PlayerFaction;
    const float x = rect.x + Pad;
    float y = rect.y + 10.0f;

    TextDraw("Economy", static_cast<int>(x), static_cast<int>(y), 24, RAYWHITE);
    if (UiButton({ rect.x + rect.width - 44.0f, y - 2.0f, 34.0f, 30.0f }, "<"))
    {
        view.economyOpen = false;
    }
    y += 34.0f;

    if (spectating)
    {
        const float half = (PanelWidth - Pad * 2.0f - 6.0f) / 2.0f;
        for (int f = 0; f < FactionCount; ++f)
        {
            if (UiButton({ x + static_cast<float>(f) * (half + 6.0f), y, half, 32.0f }, app.world.factions[static_cast<std::size_t>(f)].name.c_str(), true, faction == f))
            {
                view.economyFaction = f;
            }
        }
        y += 38.0f;
    }

    std::string tooltip;
    DrawManpower(app.world, data, faction, !spectating, x, y, tooltip);
    DrawResources(app.world, faction, x, y);
    DrawFactories(app.world, data, faction, !spectating, x, y, tooltip);
    UiTooltip(tooltip);
}
