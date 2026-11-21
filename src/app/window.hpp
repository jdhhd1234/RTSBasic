#pragma once

#include <raylib.h>

// 창 수명 RAII.
class GameWindow
{
public:
    GameWindow(int width, int height, const char* title)
    {
        SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
        InitWindow(width, height, title);
    }

    ~GameWindow()
    {
        CloseWindow();
    }

    GameWindow(const GameWindow&) = delete;
    GameWindow& operator=(const GameWindow&) = delete;
    GameWindow(GameWindow&&) = delete;
    GameWindow& operator=(GameWindow&&) = delete;
};
