#include "sim/geojson_map.hpp"
#include "core/lua_read.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <numbers>
#include <sstream>

using Json = nlohmann::json;

namespace
{
    constexpr float KmPerDegreeLat = 110.57f;
    constexpr float KmPerDegreeLonAtEquator = 111.32f;

    bool ReadPoint(const Json& j, Vec2& out)
    {
        if (!j.is_array() || j.size() < 2 || !j[0].is_number() || !j[1].is_number())
        {
            return false;
        }
        out = { j[0].get<float>(), j[1].get<float>() };
        return true;
    }

    std::vector<Vec2> ReadLine(const Json& j)
    {
        std::vector<Vec2> line;
        if (!j.is_array())
        {
            return line;
        }
        for (const Json& p : j)
        {
            Vec2 point;
            if (ReadPoint(p, point))
            {
                line.push_back(point);
            }
        }
        return line;
    }

    void AddPolygon(const Json& coordinates, Terrain terrain, std::vector<GeoShape>& shapes)
    {
        GeoShape shape{ terrain, 2, {} };
        if (coordinates.is_array())
        {
            for (const Json& ring : coordinates)
            {
                shape.rings.push_back(ReadLine(ring));
            }
        }
        if (!shape.rings.empty() && shape.rings.front().size() >= 3)
        {
            shapes.push_back(std::move(shape));
        }
    }

    void AddGeometry(const Json& geometry, Terrain terrain, std::vector<GeoShape>& shapes)
    {
        if (!geometry.is_object())
        {
            return;
        }
        const std::string type = geometry.value("type", "");
        const Json empty = Json::array();
        const Json& coordinates = geometry.contains("coordinates") ? geometry["coordinates"] : empty;

        if (type == "Polygon")
        {
            AddPolygon(coordinates, terrain, shapes);
        }
        else if (type == "MultiPolygon" && coordinates.is_array())
        {
            for (const Json& polygon : coordinates)
            {
                AddPolygon(polygon, terrain, shapes);
            }
        }
        else if (type == "LineString")
        {
            shapes.push_back({ terrain, 1, { ReadLine(coordinates) } });
        }
        else if (type == "MultiLineString" && coordinates.is_array())
        {
            for (const Json& line : coordinates)
            {
                shapes.push_back({ terrain, 1, { ReadLine(line) } });
            }
        }
        else if (type == "Point")
        {
            Vec2 point;
            if (ReadPoint(coordinates, point))
            {
                shapes.push_back({ terrain, 0, { { point } } });
            }
        }
        else if (type == "MultiPoint" && coordinates.is_array())
        {
            for (const Json& p : coordinates)
            {
                Vec2 point;
                if (ReadPoint(p, point))
                {
                    shapes.push_back({ terrain, 0, { { point } } });
                }
            }
        }
        else if (type == "GeometryCollection" && geometry.contains("geometries") && geometry["geometries"].is_array())
        {
            for (const Json& child : geometry["geometries"])
            {
                AddGeometry(child, terrain, shapes);
            }
        }
    }

    int TerrainFromName(std::string name)
    {
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return FindName(TerrainNames, name);
    }

    void Paint(HexMap& map, int index, Terrain terrain)
    {
        map.hexes[static_cast<std::size_t>(index)].terrain = terrain;
    }
}

bool PolygonContains(const std::vector<std::vector<Vec2>>& rings, Vec2 point)
{
    const auto inside = [point](const std::vector<Vec2>& ring)
    {
        bool result = false;
        for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++)
        {
            const Vec2 a = ring[i];
            const Vec2 b = ring[j];
            if ((a.y > point.y) != (b.y > point.y) && point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
            {
                result = !result;
            }
        }
        return result;
    };

    if (rings.empty() || rings.front().size() < 3 || !inside(rings.front()))
    {
        return false;
    }
    for (std::size_t h = 1; h < rings.size(); ++h)
    {
        if (rings[h].size() >= 3 && inside(rings[h]))
        {
            return false;
        }
    }
    return true;
}

bool LoadGeoJsonMap(const std::string& path, const GameData& data, HexMap& out, std::string& error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = "cannot open " + path;
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    const Json root = Json::parse(buffer.str(), nullptr, false);
    if (root.is_discarded() || !root.is_object() || root.value("type", "") != "FeatureCollection" || !root.contains("features") || !root["features"].is_array())
    {
        error = path + ": not a GeoJSON FeatureCollection";
        return false;
    }

    Terrain defaultTerrain = Terrain::Plain;
    if (root.contains("minrts_default_terrain") && root["minrts_default_terrain"].is_string())
    {
        const int index = TerrainFromName(root["minrts_default_terrain"].get<std::string>());
        if (index < 0)
        {
            error = path + ": unknown minrts_default_terrain";
            return false;
        }
        defaultTerrain = static_cast<Terrain>(index);
    }

    std::vector<GeoShape> shapes;
    int skipped = 0;
    for (const Json& feature : root["features"])
    {
        if (!feature.is_object() || !feature.contains("properties") || !feature["properties"].is_object())
        {
            ++skipped;
            continue;
        }
        const Json& properties = feature["properties"];
        const int terrain = properties.contains("terrain") && properties["terrain"].is_string()
            ? TerrainFromName(properties["terrain"].get<std::string>()) : -1;
        if (terrain < 0 || !feature.contains("geometry"))
        {
            ++skipped;
            continue;
        }
        AddGeometry(feature["geometry"], static_cast<Terrain>(terrain), shapes);
    }
    if (shapes.empty())
    {
        error = path + ": no features with a valid properties.terrain (" + std::to_string(skipped) + " skipped)";
        return false;
    }

    // 범위와 좌표계 판별
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    for (const GeoShape& shape : shapes)
    {
        for (const auto& ring : shape.rings)
        {
            for (const Vec2 p : ring)
            {
                minX = std::min(minX, p.x);
                minY = std::min(minY, p.y);
                maxX = std::max(maxX, p.x);
                maxY = std::max(maxY, p.y);
            }
        }
    }
    const bool geographic = minX >= -180.0f && maxX <= 180.0f && minY >= -90.0f && maxY <= 90.0f;
    const float midLat = (minY + maxY) * 0.5f * std::numbers::pi_v<float> / 180.0f;
    const float kx = geographic ? KmPerDegreeLonAtEquator * std::cos(midLat) : 0.001f;
    const float ky = geographic ? KmPerDegreeLat : 0.001f;

    for (GeoShape& shape : shapes)
    {
        for (auto& ring : shape.rings)
        {
            for (Vec2& p : ring)
            {
                p = { (p.x - minX) * kx, (maxY - p.y) * ky };   // 북쪽이 위
            }
        }
    }

    const GameConfig& c = data.config;
    const float d = c.hexDistanceKm;
    const float rowStep = d * std::numbers::sqrt3_v<float> / 2.0f;
    const float widthKm = std::min((maxX - minX) * kx, c.mapMaxKm);
    const float heightKm = std::min((maxY - minY) * ky, c.mapMaxKm);
    if (widthKm < d * 8.0f || heightKm < rowStep * 8.0f)
    {
        error = path + ": map extent is too small (needs at least 8x8 hexes)";
        return false;
    }

    out.hexDistance = d;
    out.cols = std::clamp(static_cast<int>(std::lround(widthKm / d)), 8, static_cast<int>(c.mapMaxKm / d));
    out.rows = std::clamp(static_cast<int>(std::lround(heightKm / rowStep)), 8, static_cast<int>(c.mapMaxKm / rowStep));
    out.hexes.assign(static_cast<std::size_t>(out.cols * out.rows), Hex{ defaultTerrain, -1, 0 });

    // 헥스 중심(0부터 시작)을 GeoJSON km 좌표계로 옮기는 보정
    const Vec2 offset = { d * 0.5f, rowStep * 0.5f };
    const auto hexOf = [&](Vec2 p) { return WorldToHex({ p.x - offset.x, p.y - offset.y }, d); };

    for (const GeoShape& shape : shapes)
    {
        if (shape.dimension == 2)
        {
            float bx0 = std::numeric_limits<float>::max();
            float by0 = std::numeric_limits<float>::max();
            float bx1 = std::numeric_limits<float>::lowest();
            float by1 = std::numeric_limits<float>::lowest();
            for (const Vec2 p : shape.rings.front())
            {
                bx0 = std::min(bx0, p.x);
                by0 = std::min(by0, p.y);
                bx1 = std::max(bx1, p.x);
                by1 = std::max(by1, p.y);
            }
            for (int i = 0; i < static_cast<int>(out.hexes.size()); ++i)
            {
                const Vec2 center = HexCenter(IndexToHex(out, i), d);
                const Vec2 sample = { center.x + offset.x, center.y + offset.y };
                if (sample.x >= bx0 && sample.x <= bx1 && sample.y >= by0 && sample.y <= by1 && PolygonContains(shape.rings, sample))
                {
                    Paint(out, i, shape.terrain);
                }
            }
        }
        else if (shape.dimension == 1)
        {
            const std::vector<Vec2>& line = shape.rings.front();
            for (std::size_t s = 1; s < line.size(); ++s)
            {
                const Vec2 a = line[s - 1];
                const Vec2 b = line[s];
                const int steps = std::max(1, static_cast<int>(std::ceil(Distance(a, b) / (d * 0.25f))));
                for (int k = 0; k <= steps; ++k)
                {
                    const float t = static_cast<float>(k) / static_cast<float>(steps);
                    const HexCoord coord = hexOf({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t });
                    if (InBounds(out, coord))
                    {
                        Paint(out, HexIndex(out, coord), shape.terrain);
                    }
                }
            }
        }
        else if (!shape.rings.front().empty())
        {
            const HexCoord coord = hexOf(shape.rings.front().front());
            if (InBounds(out, coord))
            {
                Paint(out, HexIndex(out, coord), shape.terrain);
            }
        }
    }
    return true;
}
