#pragma once

#include "core/game_data.hpp"
#include "sim/world.hpp"

#include <raylib.h>

#include <vector>

inline Vector2 ToRay(Vec2 v)
{
    return { v.x, v.y };
}

inline Vec2 ToSim(Vector2 v)
{
    return { v.x, v.y };
}

Color FactionColor(int faction);
Color ResourceColor(Resource resource);

// 월드 좌표(km) 그리기. BeginMode2D 안에서 호출.
void DrawWorldUnits(const World& world, const GameData& data, float zoom, const std::vector<entt::entity>& selected);

// 화면 좌표 그리기. EndMode2D 뒤에 호출.
void DrawWeatherLabels(const World& world, const Camera2D& camera);

// 사단 카운터 반쪽 크기(월드 단위). 화면에서 일정한 크기로 보이게 줌으로 나눈다.
Vector2 CounterHalfSize(float zoom);
