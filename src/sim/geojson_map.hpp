#pragma once

#include "core/game_data.hpp"
#include "sim/hex.hpp"
#include "sim/map.hpp"

#include <string>
#include <vector>

// QGIS에서 내보낸 GeoJSON을 헥스 맵으로 바꾼다.
//
// 입력 규칙:
// - FeatureCollection. 각 Feature의 properties.terrain = "sea"|"plain"|"forest"|... (main_real.md 지형 id)
// - Polygon / MultiPolygon: 안에 중심이 든 헥스를 칠한다 (구멍 지원).
// - LineString / MultiLineString: 선이 지나는 헥스를 칠한다 (강 레이어용).
// - Point / MultiPoint: 그 헥스를 칠한다 (도시 레이어용).
// - 파일 순서대로 칠하므로 뒤 Feature가 앞을 덮는다.
// - 좌표: 경위도(EPSG:4326)면 위도 보정해서 km로, 아니면 투영 좌표(m)로 보고 /1000.
// - 기본 지형은 최상위 "minrts_default_terrain" (없으면 plain).
// - 전체 범위는 config.mapMaxKm(1200km)에서 자른다.

struct GeoShape
{
    Terrain terrain = Terrain::Plain;
    int dimension = 2;                           // 0 점, 1 선, 2 면
    std::vector<std::vector<Vec2>> rings;        // 면: [외곽, 구멍...], 선: [선], 점: [[점]]
};

bool LoadGeoJsonMap(const std::string& path, const GameData& data, HexMap& out, std::string& error);

// 레이 캐스팅 point-in-polygon. 외곽 안이고 모든 구멍 밖이면 true.
bool PolygonContains(const std::vector<std::vector<Vec2>>& rings, Vec2 point);
