#pragma once

#include "core/game_data.hpp"
#include "sim/map.hpp"

#include <functional>
#include <vector>

// 헥스 맵에서 뽑아낸 벡터 지형. 그리기는 이 제어점들을 spline.hpp로 곡선화한다.
// 시뮬레이션 판정(이동, 전투, 영토)은 그대로 헥스로 한다.

// 제어점 체인 하나 (월드 km). closed면 마지막 점이 첫 점과 이어진다.
struct MapChain
{
    std::vector<Vec2> points;
    bool closed = false;
};

struct MapRoad
{
    std::vector<int> hexes;          // 지나는 헥스 (양 끝 거점 포함)
    std::vector<Vec2> points;        // 단순화한 제어점
};

// 맵 생성 직후 한 번 만드는 정적 지형.
struct MapFeatures
{
    std::vector<MapChain> coastlines;   // 바다 / 육지 경계
    std::vector<MapChain> rivers;       // 강 헥스 중심을 잇는 체인
    std::vector<MapRoad> roads;         // 거점 사이 도로
    std::vector<int> sites;             // 거점 헥스 (기지 + 도시)
};

// 경계 체인 추출. isBoundary(a, b)가 참인 인접 헥스 a, b 사이 변을 모아 이어진 체인으로 만든다.
// 각 체인의 점은 변의 중점이다 (지그재그가 덜해 스플라인 제어점으로 쓰기 좋다).
// isBoundary는 (a, b)와 (b, a) 둘 다 불릴 수 있으며 한 변은 한 번만 들어간다.
std::vector<MapChain> BoundaryChains(const HexMap& map, const std::function<bool(int a, int b)>& isBoundary);

// 전선: 서로 다른 진영이 소유한 인접 헥스 사이 경계 (중립 제외).
std::vector<MapChain> FrontLines(const HexMap& map);

// 해안선, 강, 도로를 만든다. baseHexes는 기지 헥스 (도로 거점).
// 도로: 거점(기지 + 도시)마다 가장 가까운 거점 2곳과 FindPath로 잇고, 중복 경로는 하나로 친다.
void BuildMapFeatures(const HexMap& map, const GameData& data, const std::vector<int>& baseHexes, MapFeatures& out);
