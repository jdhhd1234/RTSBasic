#pragma once

#include "core/game_data.hpp"
#include "sim/hex.hpp"
#include "sim/map.hpp"

#include <optional>
#include <string>
#include <vector>

// QGIS에서 내보낸 GeoJSON을 헥스 맵으로 바꾼다.
//
// 입력 규칙:
// - FeatureCollection의 properties.layer로 분류한다. terrain 속성만 있는 예전 파일도 읽는다.
// - 지형은 파일 순서대로 칠하고, 명시적 하천·도시 레이어를 위에 칠한다.
// - 좌표: 경위도(EPSG:4326)면 위도 보정해서 km로, 아니면 투영 좌표(m)로 보고 /1000.
// - 기본 지형은 최상위 "minrts_default_terrain" (없으면 plain).
// - 지형의 전체 범위는 config.mapMaxKm에서 자른다. 건너뛴 항목은 성공 시에도 error로 알린다.

struct GeoShape
{
    Terrain terrain = Terrain::Plain;
    int dimension = 2;                           // 0 점, 1 선, 2 면
    std::vector<std::vector<Vec2>> rings;        // 면: [외곽, 구멍...], 선: [선], 점: [[점]]
    std::optional<float> elevation;
};

bool LoadGeoJsonMap(const std::string& path, const GameData& data, HexMap& out, MapLayers& layers, std::string& error);

// 레이 캐스팅 point-in-polygon. 외곽 안이고 모든 구멍 밖이면 true.
bool PolygonContains(const std::vector<std::vector<Vec2>>& rings, Vec2 point);
