#include "sim/geojson_map.hpp"
#include "core/lua_read.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <numbers>
#include <span>
#include <utility>

using Json = nlohmann::json;

namespace
{
    constexpr float KmPerDegreeLat = 110.57f;
    constexpr float KmPerDegreeLonAtEquator = 111.32f;

    const Json& Field(const Json& object, const char* name)
    {
        static const Json missing;
        const auto field = object.find(name);
        return field == object.end() ? missing : *field;
    }

    std::string StringValue(const Json& value)
    {
        return value.is_string() ? value.get<std::string>() : std::string{};
    }

    bool ReadOptionalNumber(const Json& value, float& out)
    {
        if (value.is_null())
        {
            return true;
        }
        if (!value.is_number())
        {
            return false;
        }
        out = value.get<float>();
        return std::isfinite(out);
    }

    int NamedValue(const Json& value, std::span<const char* const> names, int defaultIndex = -1)
    {
        if (value.is_null())
        {
            return defaultIndex;
        }
        const std::string name = StringValue(value);
        const auto match = std::find(names.begin(), names.end(), name);
        return match == names.end() ? -1 : static_cast<int>(match - names.begin());
    }

    int FactionValue(const Json& value)
    {
        if (value == 0 || value == "0" || value == "blue")
        {
            return 0;
        }
        if (value == 1 || value == "1" || value == "red")
        {
            return 1;
        }
        return -1;
    }

    bool ReadPoint(const Json& j, Vec2& out)
    {
        if (!j.is_array() || j.size() < 2 || !j[0].is_number() || !j[1].is_number())
        {
            return false;
        }
        out = { j[0].get<float>(), j[1].get<float>() };
        return std::isfinite(out.x) && std::isfinite(out.y);
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
            if (!ReadPoint(p, point))
            {
                return {};
            }
            line.push_back(point);
        }
        return line;
    }

    bool AddPolygon(const Json& coordinates, std::vector<GeoShape>& shapes)
    {
        if (!coordinates.is_array() || coordinates.empty())
        {
            return false;
        }
        GeoShape shape;
        for (const Json& ring : coordinates)
        {
            auto points = ReadLine(ring);
            if (points.size() < 3)
            {
                return false;
            }
            shape.rings.push_back(std::move(points));
        }
        shapes.push_back(std::move(shape));
        return true;
    }

    bool AddPoint(const Json& coordinates, std::vector<GeoShape>& shapes)
    {
        Vec2 point;
        if (!ReadPoint(coordinates, point))
        {
            return false;
        }
        shapes.push_back({ Terrain::Plain, 0, { { point } } });
        return true;
    }

    bool AddLine(const Json& coordinates, std::vector<GeoShape>& shapes)
    {
        auto points = ReadLine(coordinates);
        if (points.size() < 2)
        {
            return false;
        }
        shapes.push_back({ Terrain::Plain, 1, { std::move(points) } });
        return true;
    }

    bool AddGeometry(const Json& geometry, std::vector<GeoShape>& shapes)
    {
        const std::string type = StringValue(Field(geometry, "type"));
        const Json& coordinates = Field(geometry, "coordinates");
        if (type == "Polygon")
        {
            return AddPolygon(coordinates, shapes);
        }
        if (type == "LineString")
        {
            return AddLine(coordinates, shapes);
        }
        if (type == "Point")
        {
            return AddPoint(coordinates, shapes);
        }
        if (type == "GeometryCollection")
        {
            const Json& geometries = Field(geometry, "geometries");
            return geometries.is_array() && !geometries.empty() && std::all_of(geometries.begin(), geometries.end(), [&](const Json& child)
            {
                return AddGeometry(child, shapes);
            });
        }
        const auto addPart = type == "MultiPolygon" ? AddPolygon : type == "MultiLineString" ? AddLine :
                             type == "MultiPoint" ? AddPoint : nullptr;
        return addPart != nullptr && coordinates.is_array() && !coordinates.empty()
            && std::all_of(coordinates.begin(), coordinates.end(), [&](const Json& part) { return addPart(part, shapes); });
    }

    int TerrainFromName(std::string name)
    {
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return FindName(TerrainNames, name);
    }

    bool ReadFeature(const Json& feature, std::vector<GeoShape>& terrainShapes, MapLayers& layers)
    {
        const Json& properties = Field(feature, "properties");
        const Json& layerField = Field(properties, "layer");
        const std::string layer = layerField.is_null() ? "terrain" : StringValue(layerField);
        std::vector<GeoShape> shapes;
        if (!properties.is_object() || !AddGeometry(Field(feature, "geometry"), shapes))
        {
            return false;
        }
        if (layer == "terrain")
        {
            const int terrain = TerrainFromName(StringValue(Field(properties, "terrain")));
            const Json& height = Field(properties, "elevation");
            float elevation = 0.0f;
            if (terrain < 0 || !ReadOptionalNumber(height, elevation))
            {
                return false;
            }
            for (GeoShape& shape : shapes)
            {
                shape.terrain = static_cast<Terrain>(terrain);
                if (!height.is_null())
                {
                    shape.elevation = elevation;
                }
                terrainShapes.push_back(std::move(shape));
            }
            return true;
        }

        const int dimension = layer == "territory" ? 2 : (layer == "road" || layer == "river") ? 1 : 0;
        if (std::any_of(shapes.begin(), shapes.end(), [&](const GeoShape& shape) { return shape.dimension != dimension; }))
        {
            return false;
        }
        if (layer == "road")
        {
            const int tier = NamedValue(Field(properties, "tier"), RoadTierNames, static_cast<int>(RoadTier::Minor));
            if (tier < 0)
            {
                return false;
            }
            for (GeoShape& shape : shapes)
            {
                layers.roads.push_back({ std::move(shape.rings.front()), static_cast<RoadTier>(tier) });
            }
        }
        else if (layer == "river")
        {
            for (GeoShape& shape : shapes)
            {
                layers.rivers.push_back(std::move(shape.rings.front()));
            }
        }
        else if (layer == "city")
        {
            const std::string name = StringValue(Field(properties, "name"));
            const int tier = NamedValue(Field(properties, "tier"), CityTierNames, static_cast<int>(CityTier::Town));
            const Json& population = Field(properties, "population");
            if (name.empty() || tier < 0 || !population.is_number_integer() || population < 0 || population > std::numeric_limits<int>::max())
            {
                return false;
            }
            for (const GeoShape& shape : shapes)
            {
                layers.cities.push_back({ shape.rings.front().front(), name, static_cast<CityTier>(tier), population.get<int>() });
            }
        }
        else if (layer == "mine")
        {
            const int resource = NamedValue(Field(properties, "resource"), ResourceNames);
            float reserve = -1.0f;
            if (resource < 0 || !ReadOptionalNumber(Field(properties, "reserve"), reserve))
            {
                return false;
            }
            for (const GeoShape& shape : shapes)
            {
                layers.mines.push_back({ shape.rings.front().front(), static_cast<Resource>(resource), reserve });
            }
        }
        else if (layer == "unit")
        {
            const int faction = FactionValue(Field(properties, "faction"));
            const std::string name = StringValue(Field(properties, "template"));
            if (faction < 0 || name.empty())
            {
                return false;
            }
            for (const GeoShape& shape : shapes)
            {
                layers.units.push_back({ shape.rings.front().front(), faction, name });
            }
        }
        else if (layer == "territory")
        {
            const int faction = FactionValue(Field(properties, "owner"));
            if (faction < 0)
            {
                return false;
            }
            for (GeoShape& shape : shapes)
            {
                layers.territory.push_back({ faction, std::move(shape.rings) });
            }
        }
        else
        {
            return false;
        }
        return true;
    }

    void Paint(HexMap& map, int index, const GeoShape& shape)
    {
        Hex& hex = map.hexes[static_cast<std::size_t>(index)];
        hex.terrain = shape.terrain;
        hex.elevation = shape.elevation.value_or(hex.elevation);
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

bool LoadGeoJsonMap(const std::string& path, const GameData& data, HexMap& out, MapLayers& outLayers, std::string& error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = "cannot open " + path;
        return false;
    }
    const Json root = Json::parse(file, nullptr, false);
    const Json& features = Field(root, "features");
    if (Field(root, "type") != "FeatureCollection" || !features.is_array())
    {
        error = path + ": not a GeoJSON FeatureCollection";
        return false;
    }

    Terrain defaultTerrain = Terrain::Plain;
    int skipped = 0;
    const Json& fallback = Field(root, "minrts_default_terrain");
    if (!fallback.is_null())
    {
        const int index = TerrainFromName(StringValue(fallback));
        if (index < 0)
        {
            ++skipped;
        }
        else
        {
            defaultTerrain = static_cast<Terrain>(index);
        }
    }

    std::vector<GeoShape> shapes;
    MapLayers layers;
    for (const Json& feature : features)
    {
        if (!ReadFeature(feature, shapes, layers))
        {
            ++skipped;
        }
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
    // shortcut: 좌표 범위로 CRS를 추정한다. 좌표계 지원을 늘릴 때 CRS 선언도 읽는다.
    const bool geographic = minX >= -180.0f && maxX <= 180.0f && minY >= -90.0f && maxY <= 90.0f;
    const float midLat = (minY + maxY) * 0.5f * std::numbers::pi_v<float> / 180.0f;
    const float kx = geographic ? KmPerDegreeLonAtEquator * std::cos(midLat) : 0.001f;
    const float ky = geographic ? KmPerDegreeLat : 0.001f;

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

    HexMap map;
    map.hexDistance = d;
    map.cols = std::clamp(static_cast<int>(std::lround(widthKm / d)), 8, static_cast<int>(c.mapMaxKm / d));
    map.rows = std::clamp(static_cast<int>(std::lround(heightKm / rowStep)), 8, static_cast<int>(c.mapMaxKm / rowStep));
    map.hexes.assign(static_cast<std::size_t>(map.cols * map.rows), Hex{ defaultTerrain });

    // 북쪽이 위로 오도록 뒤집고 첫 헥스 중심을 원점으로 옮긴다.
    const auto project = [&](Vec2 p)
    {
        return Vec2{ (p.x - minX) * kx - d * 0.5f, (maxY - p.y) * ky - rowStep * 0.5f };
    };
    const auto projectLine = [&](std::vector<Vec2>& line)
    {
        for (Vec2& point : line)
        {
            point = project(point);
        }
    };
    const auto hexOf = [&](Vec2 point)
    {
        if (point.x >= -d && point.x <= MapWidthKm(map) && point.y >= -d && point.y <= MapHeightKm(map))
        {
            const HexCoord coord = WorldToHex(point, d);
            if (InBounds(map, coord))
            {
                return HexIndex(map, coord);
            }
        }
        return -1;
    };

    for (GeoShape& shape : shapes)
    {
        for (auto& ring : shape.rings)
        {
            projectLine(ring);
        }
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
            for (int i = 0; i < static_cast<int>(map.hexes.size()); ++i)
            {
                const Vec2 center = HexCenter(IndexToHex(map, i), d);
                if (center.x >= bx0 && center.x <= bx1 && center.y >= by0 && center.y <= by1 && PolygonContains(shape.rings, center))
                {
                    Paint(map, i, shape);
                }
            }
        }
        else if (shape.dimension == 1)
        {
            for (const int index : HexesAlongLine(map, shape.rings.front()))
            {
                Paint(map, index, shape);
            }
        }
        else if (const int index = hexOf(shape.rings.front().front()); index >= 0)
        {
            Paint(map, index, shape);
        }
    }

    for (GeoRoad& road : layers.roads)
    {
        projectLine(road.points);
    }
    for (auto& river : layers.rivers)
    {
        projectLine(river);
        for (const int index : HexesAlongLine(map, river))
        {
            map.hexes[static_cast<std::size_t>(index)].terrain = Terrain::River;
        }
    }
    for (GeoTerritory& territory : layers.territory)
    {
        for (auto& ring : territory.rings)
        {
            projectLine(ring);
        }
    }
    const auto projectPointLayer = [&](auto& points)
    {
        for (auto& item : points)
        {
            item.position = project(item.position);
        }
        skipped += static_cast<int>(std::erase_if(points, [&](const auto& item) { return hexOf(item.position) < 0; }));
    };
    projectPointLayer(layers.cities);
    projectPointLayer(layers.mines);
    projectPointLayer(layers.units);
    for (const GeoCity& city : layers.cities)
    {
        map.hexes[static_cast<std::size_t>(hexOf(city.position))].terrain = Terrain::City;
    }

    out = std::move(map);
    outLayers = std::move(layers);
    error = skipped > 0 ? path + ": " + std::to_string(skipped) + " invalid or out-of-bounds items skipped" : std::string{};
    return true;
}
