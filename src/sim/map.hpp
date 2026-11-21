#pragma once

#include "core/game_data.hpp"
#include "sim/hex.hpp"

#include <cstdint>
#include <vector>

struct Hex
{
    Terrain terrain = Terrain::Plain;
    int owner = -1;          // -1 중립, 0 플레이어, 1 AI
    int weatherRegion = 0;
};

struct HexMap
{
    int cols = 0;
    int rows = 0;
    float hexDistance = 20.0f;
    std::vector<Hex> hexes;
};

bool InBounds(const HexMap& map, HexCoord coord);

int HexIndex(const HexMap& map, HexCoord coord);

HexCoord IndexToHex(const HexMap& map, int index);

bool IsPassable(const HexMap& map, const GameData& data, int index);

// 가상맵 생성. 북쪽 해안, 노이즈 지형, 강, 도시.
// shortcut: 절차 생성 가상맵만 지원, GeoJSON/QGIS 변환은 맵 마일스톤에서 추가.
void GenerateMap(HexMap& map, const GameData& data, int cols, int rows, std::uint32_t seed);

// 지도 전체 크기 (km).
float MapWidthKm(const HexMap& map);
float MapHeightKm(const HexMap& map);

// A* 경로. 시작 헥스는 빼고 목표 헥스는 포함한다. 갈 수 없으면 빈 벡터.
std::vector<int> FindPath(const HexMap& map, const GameData& data, int start, int goal);
