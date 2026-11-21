#pragma once

#include "app/i18n.hpp"

#include <raylib.h>

#include <string>

// 즉시 모드 UI 위젯. 그리기 단계(BeginDrawing 안)에서 호출한다.

// 클릭(마우스를 뗀 순간)이면 true.
bool UiButton(Rectangle rect, const char* text, bool enabled = true, bool selected = false);

void UiPanel(Rectangle rect);

void UiText(const char* text, float x, float y, int size = 20, Color color = RAYWHITE);

// 마우스 옆 툴팁 (이미 번역된 문자열). 빈 문자열이면 그리지 않는다. 다른 UI 위에 그리도록 마지막에 호출.
void UiTooltip(const std::string& text);

namespace UiColors
{
    inline constexpr Color Player = { 70, 125, 215, 255 };
    inline constexpr Color Ai = { 205, 65, 55, 255 };
    inline constexpr Color Panel = { 24, 28, 34, 230 };
    inline constexpr Color Accent = { 235, 200, 80, 255 };
}
