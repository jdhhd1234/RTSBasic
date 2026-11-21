#pragma once

#include "core/game_data.hpp"
#include "sim/map_features.hpp"
#include "sim/world.hpp"

#include <raylib.h>

#include <cstdint>
#include <vector>

// 전략 / 작전 2중 지도. 헥스 폴리곤 대신 래스터 바탕 + 베지어/스플라인 곡선으로 그린다.
//  전략: 전역 한눈에. 전선, 해안선, 사단·군단 기호, 보급망(HQ → 군단 → 사단 기지 → 사단).
//  작전: 선택 지역 확대. 도로, 하천, 거점, 부대 배치, 교전 구역.
enum class MapMode : std::uint8_t { Strategic, Operational };

// 그리기 상태. 지형/영토 텍스처를 소유한다 (RAII, 이동만 가능).
// 빈 MapView(기본 생성)는 첫 Refresh에서 텍스처를 만든다. 새 경기는 GameView{}로 새 MapView를 받는다.
class MapView
{
public:
    MapView() = default;
    ~MapView();

    MapView(const MapView&) = delete;
    MapView& operator=(const MapView&) = delete;
    MapView(MapView&& other) noexcept;
    MapView& operator=(MapView&& other) noexcept;

    // 매 프레임 그리기 전에 호출. 지형 텍스처는 처음 한 번, 영토 텍스처와 전선 곡선은 일정 간격으로 갱신.
    void Refresh(const World& world, const GameData& data);

    // BeginMode2D 안에서 호출.
    void DrawMap(const World& world, const GameData& data, MapMode mode, float zoom) const;
    void DrawUnits(const World& world, const GameData& data, MapMode mode, float zoom, const std::vector<entt::entity>& selected) const;

private:
    void Release();

    Texture2D terrain{};             // 헥스당 2텍셀 가로 (odd-r 오프셋 반영), 바이리니어
    Texture2D territory{};
    Image territoryPixels{};         // 영토 텍스처 갱신용 CPU 버퍼
    std::vector<std::vector<Vec2>> coastCurves;
    std::vector<std::vector<Vec2>> riverCurves;
    std::vector<std::vector<Vec2>> roadCurves;
    std::vector<std::vector<Vec2>> frontCurves;   // 샘플링된 전선 곡선
    std::vector<int> lastOwners;     // 영토 변화 감지
    float lastRefresh = -1000.0f;    // world.time 기준
};
