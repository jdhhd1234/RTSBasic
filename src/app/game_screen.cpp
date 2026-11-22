#include "app/screens.hpp"
#include "app/map_view.hpp"
#include "app/render.hpp"
#include "app/ui.hpp"
#include "core/templates.hpp"
#include "sim/components.hpp"
#include "sim/economy.hpp"
#include "sim/simulation.hpp"
#include "sim/systems.hpp"

#ifdef MINRTS_TORCH
#include "ai/deeplearn/rl_agent.hpp"
#include <filesystem>
#endif
#include <algorithm>
#include <cmath>
#include <format>
#include <random>
#include <string>

namespace
{
    constexpr float TopBarHeight = 36.0f;
    constexpr float TerritoryBarHeight = 6.0f;
    constexpr float BottomPanelHeight = 96.0f;
    constexpr float InfoPanelWidth = 330.0f;
    constexpr float InfoPanelHeight = 330.0f;
    constexpr float DragThreshold = 5.0f;
    constexpr float FrontageStepKm = 10.0f;

    bool Spectating(const World& world)
    {
        return world.factions[PlayerFaction].aiControlled;
    }

    void AdjustSelectedFrontage(AppState& app, const GameData& data, float deltaKm)
    {
        World& world = app.world;
        for (const entt::entity entity : app.view.selected)
        {
            const Division& division = world.registry.get<Division>(entity);
            ApplyCommand(world, data, Command{ .type = CommandType::SetFrontage, .entity = entity,
                .faction = PlayerFaction, .value = division.frontageKm + deltaKm });
        }
    }

    bool MouseOverUi(const AppState& app)
    {
        const Vector2 mouse = GetMousePosition();
        const float width = static_cast<float>(GetScreenWidth());
        const float height = static_cast<float>(GetScreenHeight());
        if (mouse.y < TopBarHeight + TerritoryBarHeight || mouse.y > height - BottomPanelHeight)
        {
            return true;
        }
        if (CheckCollisionPointRec(mouse, EconomyPanelRect(app.view, TopBarHeight + TerritoryBarHeight, BottomPanelHeight)))
        {
            return true;
        }
        return !app.view.selected.empty() && mouse.x > width - InfoPanelWidth - 10.0f && mouse.y < TopBarHeight + 10.0f + InfoPanelHeight;
    }

    void UpdateCameraControls(GameView& view, bool overUi)
    {
        Camera2D& camera = view.camera;
        const float pan = 700.0f * GetFrameTime() / camera.zoom;
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) camera.target.y -= pan;
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) camera.target.y += pan;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) camera.target.x -= pan;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) camera.target.x += pan;

        const bool rightPan = view.rightDown && view.rightPanned;
        if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || rightPan)
        {
            const Vector2 delta = GetMouseDelta();
            camera.target.x -= delta.x / camera.zoom;
            camera.target.y -= delta.y / camera.zoom;
        }

        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f && !overUi)
        {
            const Vector2 mouseWorld = GetScreenToWorld2D(GetMousePosition(), camera);
            camera.offset = GetMousePosition();
            camera.target = mouseWorld;
            camera.zoom = std::clamp(camera.zoom * (1.0f + 0.12f * wheel), 0.3f, 8.0f);
        }
    }

    // anyFaction: 관전 중에는 적군도 선택해서 정보를 본다.
    entt::entity PickDivision(const World& world, Vector2 worldPoint, float zoom, bool anyFaction)
    {
        const Vector2 half = CounterHalfSize(zoom);
        entt::entity best = entt::null;
        float bestDistance = half.x * 1.2f;
        for (const auto [entity, division, position, faction] : world.registry.view<Division, Position, Faction>().each())
        {
            if (!anyFaction && faction.id != PlayerFaction)
            {
                continue;
            }
            const float distance = Distance(position.value, ToSim(worldPoint));
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = entity;
            }
        }
        return best;
    }

    void AddSelection(std::vector<entt::entity>& selected, entt::entity entity)
    {
        if (std::find(selected.begin(), selected.end(), entity) == selected.end())
        {
            selected.push_back(entity);
        }
    }

    void SelectInBox(AppState& app, Rectangle screenBox, bool additive)
    {
        if (!additive)
        {
            app.view.selected.clear();
        }
        const bool anyFaction = Spectating(app.world);
        for (const auto [entity, division, position, faction] : app.world.registry.view<Division, Position, Faction>().each())
        {
            if ((anyFaction || faction.id == PlayerFaction) && CheckCollisionPointRec(GetWorldToScreen2D(ToRay(position.value), app.view.camera), screenBox))
            {
                AddSelection(app.view.selected, entity);
            }
        }
    }

    Rectangle DragRect(Vector2 a, Vector2 b)
    {
        return { std::min(a.x, b.x), std::min(a.y, b.y), std::abs(a.x - b.x), std::abs(a.y - b.y) };
    }

    float DragDistance(Vector2 a, Vector2 b)
    {
        return std::hypot(a.x - b.x, a.y - b.y);
    }

    const char* DivisionStatus(const World& world, entt::entity entity, const Division& division)
    {
        if (world.registry.all_of<Retreating>(entity)) return "Retreating";
        if (world.time - division.lastCombatTime < 1.0f) return "Engaged";
        if (world.registry.all_of<MovePath>(entity)) return "Moving";
        return "Idle";
    }

    std::string Fixed(float value, int decimals)
    {
        return TextFormat(decimals == 1 ? "%.1f" : "%.2f", value);
    }

    void DrawTopBar(AppState& app)
    {
        const World& world = app.world;
        const FactionState& blue = world.factions[PlayerFaction];
        const FactionState& red = world.factions[AiFaction];
        // 무제한이면 남은 시간 대신 경과 시간을 보여준다.
        const bool unlimited = world.matchSeconds <= 0.0f;
        const int remaining = unlimited ? static_cast<int>(world.time) : std::max(0, static_cast<int>(world.matchSeconds - world.time));
        const int width = GetScreenWidth();
        DrawRectangle(0, 0, width, static_cast<int>(TopBarHeight), UiColors::Panel);

        const std::string text = world.title + "    " + GameDateText(world.time, app.matchData.config)
            + (unlimited ? "    Elapsed " : "    Time ") + std::to_string(remaining / 60) + ":" + (remaining % 60 < 10 ? "0" : "") + std::to_string(remaining % 60)
            + "    x" + std::to_string(SpeedSteps[static_cast<std::size_t>(app.view.speedIndex)])
            + "    " + blue.name + " " + std::to_string(blue.ownedHexes) + " / " + red.name + " " + std::to_string(red.ownedHexes);
        TextDraw(text.c_str(), 12, 9, 20, RAYWHITE);

        // 영토 비율 바
        const int total = std::max(1, blue.ownedHexes + red.ownedHexes);
        const int blueWidth = width * blue.ownedHexes / total;
        DrawRectangle(0, static_cast<int>(TopBarHeight), blueWidth, static_cast<int>(TerritoryBarHeight), UiColors::Player);
        DrawRectangle(blueWidth, static_cast<int>(TopBarHeight), width - blueWidth, static_cast<int>(TerritoryBarHeight), UiColors::Ai);
    }

    std::string QueueText(const FactionState& state, const GameData& data)
    {
        std::string text = state.name + " queue: " + std::to_string(state.queue.size());
        if (!state.queue.empty())
        {
            text += "   next: " + state.templates[static_cast<std::size_t>(state.queue.front())].name
                  + " " + std::to_string(static_cast<int>(state.queueProgress * 100.0f)) + "%"
                  + "   production x" + Fixed(state.productionSpeed, 2);
        }
        for (const FactoryState& factory : state.factories)
        {
            if (factory.daysLeft > 0.0f)
            {
                const FactoryDef& def = data.factories[static_cast<std::size_t>(factory.type)];
                text += "   building: " + def.name + " " + std::to_string(static_cast<int>(100.0f * (1.0f - factory.daysLeft / def.buildDays))) + "%";
            }
        }
        return text;
    }

    void DrawSpeedControls(AppState& app, float top)
    {
        GameView& view = app.view;
        const float right = static_cast<float>(GetScreenWidth()) - 12.0f;
        float x = right - static_cast<float>(SpeedSteps.size()) * 50.0f - 64.0f;
        if (UiButton({ x, top + 36.0f, 60.0f, 44.0f }, view.paused ? "Play" : "Pause", true, view.paused))
        {
            view.paused = !view.paused;
        }
        x += 64.0f;
        for (int i = 0; i < static_cast<int>(SpeedSteps.size()); ++i)
        {
            const std::string label = "x" + std::to_string(SpeedSteps[static_cast<std::size_t>(i)]);
            if (UiButton({ x, top + 36.0f, 46.0f, 44.0f }, label.c_str(), true, view.speedIndex == i))
            {
                view.speedIndex = i;
            }
            x += 50.0f;
        }
    }

    void DrawBottomPanel(AppState& app, const GameData& data)
    {
        World& world = app.world;
        const float top = static_cast<float>(GetScreenHeight()) - BottomPanelHeight;
        UiPanel({ 0.0f, top, static_cast<float>(GetScreenWidth()), BottomPanelHeight });
        const Vector2 mouse = GetMousePosition();
        std::string tooltip;

        if (Spectating(world))
        {
            for (int f = 0; f < FactionCount; ++f)
            {
                const FactionState& state = world.factions[static_cast<std::size_t>(f)];
                const Color color = f == PlayerFaction ? UiColors::Player : UiColors::Ai;
                TextDraw(QueueText(state, data).c_str(), 12, static_cast<int>(top) + 14 + f * 30, 18, color);
            }
        }
        else
        {
            FactionState& player = world.factions[PlayerFaction];
            TextDraw(QueueText(player, data).c_str(), 12, static_cast<int>(top) + 8, 18, LIGHTGRAY);

            // 생산: 공장·자원·인력이 모두 있어야 한다.
            for (int i = 0; i < static_cast<int>(player.templates.size()); ++i)
            {
                const TemplateDef& templ = player.templates[static_cast<std::size_t>(i)];
                const std::string blocker = ProductionBlocker(player, data, templ);
                const std::string label = std::to_string(i + 1) + ". " + templ.name;
                const Rectangle rect = { 12.0f + static_cast<float>(i) * 206.0f, top + 32.0f, 200.0f, 44.0f };
                if (CheckCollisionPointRec(mouse, rect))
                {
                    const TemplateNeeds needs = GetTemplateNeeds(templ, data);
                    tooltip = Translate(templ.name) + ": " + Translate(ResourceText(needs.materials)) + ", " + Translate("manpower") + " "
                        + std::to_string(static_cast<int>(needs.manpower)) + (blocker.empty() ? "" : "   [" + Translate(blocker) + "]");
                }
                if (UiButton(rect, label.c_str(), blocker.empty()) || (blocker.empty() && IsKeyPressed(KEY_ONE + i)))
                {
                    ApplyCommand(world, data, Command{ .type = CommandType::Produce, .faction = PlayerFaction, .templateIndex = i });
                }
            }

            const float surrenderX = static_cast<float>(GetScreenWidth()) - static_cast<float>(SpeedSteps.size()) * 50.0f - 200.0f;
            if (UiButton({ surrenderX, top + 32.0f, 110.0f, 44.0f }, "Surrender"))
            {
                ApplyCommand(world, data, Command{ .type = CommandType::Surrender, .faction = PlayerFaction });
            }
        }

        DrawSpeedControls(app, top);
        const char* help = Spectating(world)
            ? "Spectating   LMB inspect   Tab economy   F3 profiler   Space pause   +/- speed   WASD/RMB drag pan   Wheel zoom   Esc menu"
            : "LMB select (Shift add)   RMB move / drag pan   R retreat   [ ] frontage   1-6 produce   C conscript   Tab economy   Wheel zoom   Space pause   +/- speed   Esc menu";
        TextDraw(help, GetScreenWidth() - TextWidth(help, 16) - 12, static_cast<int>(top) + 10, 16, GRAY);
        UiTooltip(tooltip);
    }

    void DrawEventLog(const World& world, int left)
    {
        const int shown = std::min(6, static_cast<int>(world.events.size()));
        float y = static_cast<float>(GetScreenHeight()) - BottomPanelHeight - 10.0f - static_cast<float>(shown) * 22.0f;
        for (int i = static_cast<int>(world.events.size()) - shown; i < static_cast<int>(world.events.size()); ++i)
        {
            const GameEvent& event = world.events[static_cast<std::size_t>(i)];
            const Color color = event.kind == EventKind::Good ? Color{ 130, 190, 255, 255 } : (event.kind == EventKind::Bad ? Color{ 255, 140, 120, 255 } : RAYWHITE);
            const int t = static_cast<int>(event.time);
            const std::string line = std::to_string(t / 60) + ":" + (t % 60 < 10 ? "0" : "") + std::to_string(t % 60) + "  " + event.text;
            const int width = TextWidth(line.c_str(), 18);
            DrawRectangle(left + 8, static_cast<int>(y) - 2, width + 12, 22, Color{ 0, 0, 0, 140 });
            TextDraw(line.c_str(), left + 14, static_cast<int>(y), 18, color);
            y += 22.0f;
        }
    }

    void DrawInfoPanel(AppState& app, const GameData& data)
    {
        const World& world = app.world;
        if (app.view.selected.empty())
        {
            return;
        }
        const float x = static_cast<float>(GetScreenWidth()) - InfoPanelWidth - 10.0f;
        const float y = TopBarHeight + 10.0f + TerritoryBarHeight;
        UiPanel({ x, y, InfoPanelWidth, InfoPanelHeight });

        const entt::entity entity = app.view.selected.front();
        const Division& division = world.registry.get<Division>(entity);
        TextDraw(std::format("Frontage {:g} km", division.frontageKm).c_str(),
                 static_cast<int>(x) + 12, static_cast<int>(y) + 48, 18, LIGHTGRAY);
        if (!Spectating(world))
        {
            if (UiButton({ x + 246.0f, y + 44.0f, 32.0f, 26.0f }, "[-]"))
            {
                AdjustSelectedFrontage(app, data, -FrontageStepKm);
            }
            if (UiButton({ x + 286.0f, y + 44.0f, 32.0f, 26.0f }, "[+]"))
            {
                AdjustSelectedFrontage(app, data, FrontageStepKm);
            }
        }

        if (app.view.selected.size() > 1)
        {
            float hp = 0.0f;
            float maxHp = 0.0f;
            for (const entt::entity selected : app.view.selected)
            {
                const Division& unit = world.registry.get<Division>(selected);
                hp += unit.hp;
                maxHp += unit.maxHp;
            }
            TextDraw((std::to_string(app.view.selected.size()) + " divisions selected").c_str(), static_cast<int>(x) + 12, static_cast<int>(y) + 12, 20, RAYWHITE);
            TextDraw(("Total HP " + std::to_string(static_cast<int>(hp)) + " / " + std::to_string(static_cast<int>(maxHp))).c_str(),
                     static_cast<int>(x) + 12, static_cast<int>(y) + 76, 18, LIGHTGRAY);
            return;
        }

        const int faction = world.registry.get<Faction>(entity).id;
        const Vec2 position = world.registry.get<Position>(entity).value;
        const float supply = DivisionSupply(world, data, position, faction, division.effects);

        float ly = y + 12.0f;
        TextDraw(division.name.c_str(), static_cast<int>(x) + 12, static_cast<int>(ly), 22, faction == PlayerFaction ? RAYWHITE : UiColors::Ai);
        ly += 64.0f;
        const std::string lines[] = {
            world.factions[static_cast<std::size_t>(faction)].name,
            "HP " + std::to_string(static_cast<int>(division.hp)) + " / " + std::to_string(static_cast<int>(division.maxHp)),
            "Supply " + std::to_string(static_cast<int>(supply * 100.0f)) + "%",
            std::string("Status ") + DivisionStatus(world, entity, division),
            "Speed " + Fixed(division.speed, 1) + " km/s   Def avg " + Fixed(division.defenseAvg, 1),
        };
        for (const std::string& line : lines)
        {
            TextDraw(line.c_str(), static_cast<int>(x) + 12, static_cast<int>(ly), 18, LIGHTGRAY);
            ly += 24.0f;
        }
        ly += 6.0f;
        for (const int b : division.battalions)
        {
            const BattalionDef& def = data.battalions[static_cast<std::size_t>(b)];
            TextDraw(("- " + def.name).c_str(), static_cast<int>(x) + 12, static_cast<int>(ly), 18, RAYWHITE);
            ly += 22.0f;
        }
    }

    void FinishMatch(AppState& app)
    {
        const World& world = app.world;
        const bool spectating = Spectating(world);
        const bool victory = world.winner == PlayerFaction;
        app.resultGood = victory;
        app.resultText = world.endReason;
        app.resultTitle = spectating
            ? world.factions[static_cast<std::size_t>(world.winner)].name + " WINS"
            : (victory ? "VICTORY" : "DEFEAT");

        if (victory && !spectating && world.stage >= 0)
        {
            app.profile.clearedStages = std::max(app.profile.clearedStages, world.stage + 1);
            SaveProfile(app.profilePath, app.profile, app.matchData);
        }
        app.screen = Screen::Result;
    }
}

void StartMatch(AppState& app, GameData matchData, const MatchSetup& setup)
{
    app.matchData = std::move(matchData);
    InitWorld(app.world, app.matchData, setup, std::random_device{}());
#ifdef MINRTS_TORCH
    // AI 진영에 학습된 정책을 붙인다. 모델이 없거나 못 읽으면 기존 Utility AI 그대로.
    const std::string checkpoint = app.dataDir + "ai/rl_policy.pt";
    if (app.rlAi && std::filesystem::exists(checkpoint))
    {
        try
        {
            for (FactionState& faction : app.world.factions)
            {
                if (faction.aiControlled)
                {
                    faction.policy = MakeRlInferenceHook(app.matchData, checkpoint);
                }
            }
            PushEvent(app.world, "RL AI on", EventKind::Info);
        }
        catch (const std::exception& error)
        {
            PushEvent(app.world, std::string("RL AI load failed: ") + error.what(), EventKind::Bad);
        }
    }
#endif

    const float mapWidth = MapWidthKm(app.world.map);
    const float mapHeight = MapHeightKm(app.world.map);
    app.view = GameView{};
    // 왼쪽 경제 탭에 가리지 않게 남은 영역 가운데에 맞춘다.
    const float left = EconomyPanelRect(app.view, TopBarHeight + TerritoryBarHeight, BottomPanelHeight).width;
    const float screenWidth = static_cast<float>(GetScreenWidth()) - left;
    const float screenHeight = static_cast<float>(GetScreenHeight()) - TopBarHeight - BottomPanelHeight;

    app.view.camera.target = { mapWidth / 2.0f, mapHeight / 2.0f };
    app.view.camera.offset = { left + screenWidth / 2.0f, TopBarHeight + screenHeight / 2.0f };
    app.view.camera.zoom = std::min(screenWidth / mapWidth, screenHeight / mapHeight) * 0.95f;
    app.screen = Screen::Game;
}

void UpdateGame(AppState& app, const GameData& data)
{
    GameView& view = app.view;
    World& world = app.world;
    const bool spectating = Spectating(world);

    if (IsKeyPressed(KEY_ESCAPE))
    {
        app.screen = Screen::Menu;
        return;
    }
    if (IsKeyPressed(KEY_SPACE))
    {
        view.paused = !view.paused;
    }
    if (IsKeyPressed(KEY_TAB))
    {
        view.economyOpen = !view.economyOpen;
    }
    if (IsKeyPressed(KEY_F3) && world.stage < 0)   // 샌드박스(봇전투 포함)만
    {
        view.profilerOpen = !view.profilerOpen;
        world.profiler.enabled = view.profilerOpen;
    }
    if (IsKeyPressed(KEY_C) && !spectating)
    {
        ApplyCommand(world, data, Command{ .type = CommandType::Conscript, .faction = PlayerFaction });
    }
    if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD))
    {
        view.speedIndex = std::min(view.speedIndex + 1, static_cast<int>(SpeedSteps.size()) - 1);
    }
    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT))
    {
        view.speedIndex = std::max(view.speedIndex - 1, 0);
    }

    const bool overUi = MouseOverUi(app);
    const Vector2 mouse = GetMousePosition();

    // 우클릭: 누른 채 끌면 화면 이동, 짧게 떼면 이동 명령
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !overUi)
    {
        view.rightDown = true;
        view.rightPanned = false;
        view.rightStart = mouse;
    }
    if (view.rightDown && DragDistance(mouse, view.rightStart) > DragThreshold)
    {
        view.rightPanned = true;
    }

    UpdateCameraControls(view, overUi);

    if (view.rightDown && IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
    {
        view.rightDown = false;
        if (!view.rightPanned && !spectating && !view.selected.empty())
        {
            const HexCoord target = WorldToHex(ToSim(GetScreenToWorld2D(mouse, view.camera)), world.map.hexDistance);
            if (InBounds(world.map, target))
            {
                const int hex = HexIndex(world.map, target);
                for (const entt::entity entity : view.selected)
                {
                    ApplyCommand(world, data, Command{ .type = CommandType::Move, .entity = entity, .targetHex = hex, .faction = PlayerFaction });
                }
            }
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !overUi)
    {
        view.dragging = true;
        view.dragStart = mouse;
    }
    if (view.dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        view.dragging = false;
        const bool additive = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        if (DragDistance(view.dragStart, mouse) <= DragThreshold)
        {
            if (!additive)
            {
                view.selected.clear();
            }
            const entt::entity picked = PickDivision(world, GetScreenToWorld2D(mouse, view.camera), view.camera.zoom, spectating);
            if (picked != entt::null)
            {
                AddSelection(view.selected, picked);
            }
        }
        else
        {
            SelectInBox(app, DragRect(view.dragStart, mouse), additive);
        }
    }

    if (IsKeyPressed(KEY_R) && !spectating)
    {
        for (const entt::entity entity : view.selected)
        {
            ApplyCommand(world, data, Command{ .type = CommandType::Retreat, .entity = entity, .faction = PlayerFaction });
        }
    }
    if (!spectating)
    {
        if (IsKeyPressed(KEY_LEFT_BRACKET))
        {
            AdjustSelectedFrontage(app, data, -FrontageStepKm);
        }
        else if (IsKeyPressed(KEY_RIGHT_BRACKET))
        {
            AdjustSelectedFrontage(app, data, FrontageStepKm);
        }
    }

    if (!view.paused)
    {
        const float step = 1.0f / data.config.tickHz;
        const float speed = static_cast<float>(SpeedSteps[static_cast<std::size_t>(view.speedIndex)]);
        view.accumulator += std::min(GetFrameTime(), 0.25f) * speed;
        view.lastTicks = 0;
        while (view.accumulator >= step && !world.over)
        {
            StepWorld(world, data, step);
            view.accumulator -= step;
            ++view.lastTicks;
        }
    }

    std::erase_if(view.selected, [&](entt::entity e) { return !world.registry.valid(e); });
    if (world.over)
    {
        FinishMatch(app);
    }
}

void DrawGame(AppState& app, const GameData& data)
{
    GameView& view = app.view;
    Profiler& profiler = app.world.profiler;

    view.mapView.Refresh(app.world, data);
    BeginMode2D(view.camera);
    { ProfileScope scope(profiler, ProfileSection::DrawMap); view.mapView.DrawMap(app.world, data, view.camera.zoom); }
    { ProfileScope scope(profiler, ProfileSection::DrawUnits); view.mapView.DrawUnits(app.world, data, view.camera.zoom, view.selected); }
    EndMode2D();
    ProfileScope uiScope(profiler, ProfileSection::DrawUi);

    DrawWeatherLabels(app.world, view.camera);

    if (view.dragging && DragDistance(view.dragStart, GetMousePosition()) > DragThreshold)
    {
        const Rectangle box = DragRect(view.dragStart, GetMousePosition());
        DrawRectangleRec(box, Fade(UiColors::Accent, 0.15f));
        DrawRectangleLinesEx(box, 1.0f, UiColors::Accent);
    }

    const Rectangle economy = EconomyPanelRect(view, TopBarHeight + TerritoryBarHeight, BottomPanelHeight);
    DrawEventLog(app.world, static_cast<int>(economy.x + economy.width));
    DrawTopBar(app);
    DrawBottomPanel(app, data);
    DrawInfoPanel(app, data);
    DrawEconomyPanel(app, data, TopBarHeight + TerritoryBarHeight, BottomPanelHeight);
    DrawProfilerOverlay(app, TopBarHeight + TerritoryBarHeight + 10.0f);

    if (view.paused)
    {
        const char* text = "PAUSED";
        TextDraw(text, GetScreenWidth() / 2 - TextWidth(text, 48) / 2, 60, 48, UiColors::Accent);
    }
}
