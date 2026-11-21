#pragma once

#include "core/game_data.hpp"
#include "app/map_view.hpp"
#include "core/profile.hpp"
#include "sim/scenario.hpp"
#include "sim/simulation.hpp"
#include "sim/world.hpp"

#include <raylib.h>

#include <array>
#include <string>
#include <vector>

enum class Screen { Menu, Templates, Sandbox, Game, Result };

inline constexpr std::array<int, 5> SpeedSteps = { 1, 2, 5, 10, 20 };

struct GameView
{
    Camera2D camera{};
    std::vector<entt::entity> selected;
    bool paused = false;
    int speedIndex = 0;              // SpeedSteps 인덱스
    bool dragging = false;
    Vector2 dragStart{};
    bool rightDown = false;          // 우클릭: 짧게 = 이동 명령, 끌면 = 화면 이동
    bool rightPanned = false;
    Vector2 rightStart{};
    float accumulator = 0.0f;
    bool economyOpen = true;         // 왼쪽 경제 탭 (Tab 키)
    bool profilerOpen = false;       // 프로파일러 오버레이 (F3, 샌드박스만). 열려 있을 때만 측정한다
    int lastTicks = 0;               // 지난 프레임에 돈 시뮬레이션 틱 수
    std::string profileMessage;
    int economyFaction = PlayerFaction;   // 관전 중 경제 탭에 보여줄 진영
    MapView mapView;
};

struct TemplateEditor
{
    std::vector<TemplateDef> templates;
    int current = 0;
};

struct SandboxState
{
    std::vector<std::string> scenarioFiles;
    std::vector<std::string> mapFiles;     // data/maps/*.geojson
    int selected = -1;
    bool loaded = false;
    Scenario scenario;
    std::string error;
    int mapChoice = 0;                     // 0 시나리오 그대로, 1 랜덤, 2+ mapFiles[i-2]
    int randomSizeKm = 1200;
    int randomSeed = 1;
    bool botBattle = false;
    bool profiler = false;                 // 시작할 때 프로파일러 켜기
};

struct AppState
{
    Screen screen = Screen::Menu;
    Profile profile;
    std::string profilePath;
    std::string dataDir;
    int stage = 0;
    int difficulty = 1;
    bool botBattle = false;
    GameData matchData;              // 진행 중인 판의 데이터 (샌드박스는 커스텀 대대 포함)
    World world;
    GameView view;
    TemplateEditor editor;
    SandboxState sandbox;
    std::string resultTitle;
    std::string resultText;
    bool resultGood = false;
    bool quit = false;
};

// 각 화면은 그리기 단계에서 UI 버튼을 처리하므로 Update(입력·시뮬레이션)와 Draw(그리기·버튼)로 나눈다.
void DrawMenu(AppState& app, const GameData& data);
void DrawTemplates(AppState& app, const GameData& data);
void DrawResult(AppState& app);

void OpenSandbox(AppState& app);
void DrawSandbox(AppState& app, const GameData& data);

void StartMatch(AppState& app, GameData matchData, const MatchSetup& setup);
// 왼쪽 경제 탭 (economy_panel.cpp). 접혀 있으면 펼치기 손잡이 크기만 차지한다.
Rectangle EconomyPanelRect(const GameView& view, float top, float bottomMargin);
void DrawEconomyPanel(AppState& app, const GameData& data, float top, float bottomMargin);

// 샌드박스 프로파일러 오버레이 (profiler_overlay.cpp).
void DrawProfilerOverlay(AppState& app, float top);

void UpdateGame(AppState& app, const GameData& data);
void DrawGame(AppState& app, const GameData& data);
