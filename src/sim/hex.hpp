#pragma once

#include <array>

// 월드 좌표 (km). raylib Vector2와 분리해 시뮬레이션이 raylib에 의존하지 않게 한다.
struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;
};

// pointy-top 헥스, odd-r 오프셋 좌표.
struct HexCoord
{
    int col = 0;
    int row = 0;
};

Vec2 HexCenter(HexCoord coord, float hexDistance);

HexCoord WorldToHex(Vec2 position, float hexDistance);

int HexDistance(HexCoord a, HexCoord b);

std::array<HexCoord, 6> HexNeighbors(HexCoord coord);

float Distance(Vec2 a, Vec2 b);
