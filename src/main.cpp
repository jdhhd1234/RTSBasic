#include "app/i18n.hpp"
#include "app/screens.hpp"
#include "app/window.hpp"
#include "core/lua_loader.hpp"

#include <raylib.h>

#include <iostream>
#include <string>

int main()
{
    GameWindow window(1600, 900, "MinRTS");
    LanguageSession language;   // window보다 먼저 소멸해 폰트를 해제한다
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);   // Esc는 게임 화면에서 메뉴로 돌아가는 데 쓴다.

    const std::string baseDir = GetApplicationDirectory();
    GameData data;
    std::string error;
    if (!LoadGameData(baseDir + "data/game_data.lua", data, error))
    {
        std::cerr << "Failed to load game data: " << error << '\n';
        return 1;
    }

    AppState app;
    app.dataDir = baseDir + "data/";
    app.profilePath = baseDir + "profile.txt";
    app.profile = LoadProfile(app.profilePath, data);
    if (!SetLanguage(app.dataDir, app.profile.language, error))
    {
        std::cerr << "Language '" << app.profile.language << "' unavailable, using English: " << error << '\n';
        app.profile.language = "en";
    }

    while (!WindowShouldClose() && !app.quit)
    {
        if (app.screen == Screen::Game)
        {
            UpdateGame(app, app.matchData);
        }

        BeginDrawing();
        ClearBackground(Color{ 18, 20, 24, 255 });
        switch (app.screen)
        {
        case Screen::Menu: DrawMenu(app, data); break;
        case Screen::Templates: DrawTemplates(app, data); break;
        case Screen::Sandbox: DrawSandbox(app, data); break;
        case Screen::Game: DrawGame(app, app.matchData); break;
        case Screen::Result: DrawResult(app); break;
        }
        EndDrawing();
    }
    return 0;
}
