#include "app/ui.hpp"

#include <algorithm>

bool UiButton(Rectangle rect, const char* text, bool enabled, bool selected)
{
    const bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), rect);
    Color fill = { 52, 60, 72, 255 };
    if (!enabled)
    {
        fill = { 38, 40, 46, 255 };
    }
    else if (selected)
    {
        fill = { 70, 95, 130, 255 };
    }
    else if (hover)
    {
        fill = { 72, 82, 98, 255 };
    }

    DrawRectangleRec(rect, fill);
    DrawRectangleLinesEx(rect, selected ? 2.0f : 1.0f, selected ? UiColors::Accent : Color{ 95, 105, 120, 255 });

    const int size = 18;
    const int width = TextWidth(text, size);
    TextDraw(text, static_cast<int>(rect.x + (rect.width - static_cast<float>(width)) / 2.0f),
             static_cast<int>(rect.y + (rect.height - static_cast<float>(size)) / 2.0f), size,
             enabled ? RAYWHITE : Color{ 110, 110, 115, 255 });

    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

void UiPanel(Rectangle rect)
{
    DrawRectangleRec(rect, UiColors::Panel);
    DrawRectangleLinesEx(rect, 1.0f, Color{ 80, 90, 105, 255 });
}

void UiText(const char* text, float x, float y, int size, Color color)
{
    TextDraw(text, static_cast<int>(x), static_cast<int>(y), size, color);
}

void UiTooltip(const std::string& text)
{
    if (text.empty())
    {
        return;
    }
    const Vector2 mouse = GetMousePosition();
    const int width = TextWidth(text.c_str(), 18) + 16;
    const int x = std::clamp(static_cast<int>(mouse.x) + 16, 4, std::max(4, GetScreenWidth() - width - 4));
    const int y = std::max(4, static_cast<int>(mouse.y) - 34);
    DrawRectangle(x, y, width, 26, Color{ 10, 12, 16, 235 });
    DrawRectangleLines(x, y, width, 26, Color{ 95, 105, 120, 255 });
    TextDraw(text.c_str(), x + 8, y + 4, 18, UiColors::Accent);
}
