-- QGIS에서 내보낸 GeoJSON 지형을 쓰는 예시 시나리오.
return {
    name = "Rhineland (QGIS map)",
    description = "GeoJSON terrain: Rhine, Ardennes, Black Forest, Alps. Standard battalions.",
    match_seconds = 1200,

    map = { source = "geojson", path = "../maps/rhineland.geojson" },

    sides = {
        { name = "Blue", ai = false, divisions = 3 },          -- templates 생략: 프로필 편제 사용
        { name = "Red",  ai = true, difficulty = "Normal", divisions = 4 },
    },
}
