#pragma once

#include "core/game_data.hpp"
#include "sim/hex.hpp"

#include <cstdint>
#include <string>
#include <vector>

// QGIS에서 관리하는 지도 레이어 (GeoJSON → MinRTS). 데이터만 담는다.
// QGIS 담당: 실제 형태와 위치, 기본 속성. MinRTS 담당: 그 위의 규칙 (이동, 도하, 생산, 채굴, 점령).
// 좌표는 모두 월드 km (geojson_map.cpp가 투영 변환까지 끝낸 값).

enum class RoadTier : std::uint8_t { Highway, Major, Minor, None };
enum class CityTier : std::uint8_t { Metro, City, Town };

constexpr const char* RoadTierNames[] = { "highway", "major", "minor" };
constexpr const char* CityTierNames[] = { "metro", "city", "town" };

struct GeoRoad
{
    std::vector<Vec2> points;
    RoadTier tier = RoadTier::Minor;
};

struct GeoCity
{
    Vec2 position;
    std::string name;
    CityTier tier = CityTier::Town;
    int population = 0;              // 명
};

struct GeoMine
{
    Vec2 position;
    Resource resource = Resource::Oil;
    float reserve = -1.0f;           // 매장량 (자원 단위), 0 이하 = 무한
};

struct GeoUnit
{
    Vec2 position;
    int faction = 0;                 // 0 청군, 1 적군
    std::string templateName;        // 편제 이름 (진영 템플릿에서 찾는다)
};

// 초기 영토 (선택). 면 안 헥스가 그 진영 소유로 시작한다.
struct GeoTerritory
{
    int faction = 0;
    std::vector<std::vector<Vec2>> rings;   // [외곽, 구멍...]
};

struct MapLayers
{
    std::vector<GeoRoad> roads;
    std::vector<std::vector<Vec2>> rivers;  // 하천 실제 형태 (선)
    std::vector<GeoCity> cities;
    std::vector<GeoMine> mines;
    std::vector<GeoUnit> units;
    std::vector<GeoTerritory> territory;
};
