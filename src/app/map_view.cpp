#include "app/map_view.hpp"
#include "app/render.hpp"
#include "app/ui.hpp"
#include "sim/components.hpp"
#include "sim/spline.hpp"

#include <algorithm>
#include <limits>
#include <numbers>
#include <utility>

namespace
{
    constexpr float TerritoryRefreshSeconds = 0.25f;
    constexpr int RasterScale = 4;
    constexpr Color CoastColor = { 41, 63, 76, 255 };
    constexpr Color RiverColor = { 50, 115, 168, 255 };
    constexpr Color RoadEdgeColor = { 103, 78, 55, 255 };
    constexpr Color RoadColor = { 199, 163, 111, 255 };
    constexpr Color EngagementColor = { 255, 120, 30, 255 };

    template <typename HexColor>
    void FillRaster(const Image& image, const HexMap& map, HexColor color)
    {
        Color* pixels = static_cast<Color*>(image.data);
        for (int row = 0; row < map.rows; ++row)
        {
            for (int x = 0; x < image.width; ++x)
            {
                // 행 양끝에서 남는 텍셀은 가장 가까운 헥스 색으로 채운다.
                const int col = std::clamp((x - (row & 1)) / 2, 0, map.cols - 1);
                const int index = HexIndex(map, { col, row });
                pixels[row * image.width + x] = color(map.hexes[static_cast<std::size_t>(index)]);
            }
        }
    }

    void DrawRaster(Texture2D texture, const HexMap& map)
    {
        const float texelWidth = map.hexDistance / 2.0f;
        const float texelHeight = map.hexDistance * (std::numbers::sqrt3_v<float> / 2.0f);
        const float width = static_cast<float>(2 * map.cols + 1);   // 원본 텍셀 수 (확대된 텍스처도 같은 영역)
        const float height = static_cast<float>(map.rows);

        // 두 텍셀 사이의 중앙과 각 행의 중앙을 헥스 중심에 맞춘다.
        DrawTexturePro(texture, { 0.0f, 0.0f, static_cast<float>(texture.width), static_cast<float>(texture.height) },
                       { -texelWidth, -texelHeight / 2.0f, width * texelWidth, height * texelHeight },
                       { 0.0f, 0.0f }, 0.0f, WHITE);
    }

    // 헥스 변 중점 체인은 2행 주기로 ±d/2 흔들리므로 그보다 큰 허용치로 단순화한 뒤 스플라인으로 편다.
    std::vector<std::vector<Vec2>> SampleChains(const std::vector<MapChain>& chains, float tolerance)
    {
        std::vector<std::vector<Vec2>> curves;
        curves.reserve(chains.size());
        for (const MapChain& chain : chains)
        {
            curves.push_back(SampleCatmullRom(SimplifyPolyline(chain.points, tolerance), 6, chain.closed));
        }
        return curves;
    }

    void DrawCurve(const std::vector<Vec2>& points, float width, Color color)
    {
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            DrawLineEx(ToRay(points[i - 1]), ToRay(points[i]), width, color);
        }
    }

    const Position* NearestOwnBase(const World& world, Vec2 from, int factionId, BaseType below)
    {
        const Position* nearest = nullptr;
        float nearestDistance = std::numeric_limits<float>::infinity();
        for (const auto [entity, base, position, faction] : world.registry.view<Base, Position, Faction>().each())
        {
            if (faction.id != factionId || ToIndex(base.type) >= ToIndex(below))
            {
                continue;
            }
            const float distance = Distance(from, position.value);
            if (distance < nearestDistance)
            {
                nearest = &position;
                nearestDistance = distance;
            }
        }
        return nearest;
    }

    void DrawSupplyNetwork(const World& world, bool strategic, float zoom)
    {
        // 사단과 가장 가까운 아군 기지를 잇고, 기지는 상위 기지에 연결한다.
        for (const auto [entity, division, position, faction] : world.registry.view<Division, Position, Faction>().each())
        {
            const Position* base = NearestOwnBase(world, position.value, faction.id, BaseType::Count);
            if (base != nullptr)
            {
                DrawCurve(SampleArc(position.value, base->value, 0.15f, 12), 1.0f / zoom,
                          Fade(FactionColor(faction.id), strategic ? 0.35f : 0.10f));
            }
        }
        for (const auto [entity, base, position, faction] : world.registry.view<Base, Position, Faction>().each())
        {
            if (base.type == BaseType::Hq)
            {
                continue;
            }
            const Position* parent = NearestOwnBase(world, position.value, faction.id, base.type);
            if (parent != nullptr)
            {
                DrawCurve(SampleArc(parent->value, position.value, 0.15f, 12), 2.0f / zoom,
                          Fade(FactionColor(faction.id), strategic ? 0.60f : 0.18f));
            }
        }
    }

    void DrawCitySites(const World& world, float zoom)
    {
        const float half = 4.5f / zoom;
        for (const int index : world.features.sites)
        {
            const auto hex = static_cast<std::size_t>(index);
            if (world.map.hexes[hex].terrain != Terrain::City || world.baseAtHex[hex] != entt::null)
            {
                continue;
            }
            const Vector2 p = ToRay(HexCenter(IndexToHex(world.map, index), world.map.hexDistance));
            DrawRectangleLinesEx({ p.x - half, p.y - half, 2.0f * half, 2.0f * half }, 1.25f / zoom, BLACK);
            DrawTextEx(GetFontDefault(), "City", { p.x + 1.5f * half, p.y - half }, 10.0f / zoom, 1.0f / zoom, BLACK);
        }
    }

    void DrawEngagementZones(const World& world, float radius)
    {
        for (const auto [entity, division, position] : world.registry.view<Division, Position>().each())
        {
            if (world.time - division.lastCombatTime < 1.0f)
            {
                const Vector2 center = ToRay(position.value);
                DrawCircleV(center, radius, Fade(EngagementColor, 0.15f));
                DrawCircleLinesV(center, radius, Fade(EngagementColor, 0.65f));
            }
        }
    }

    void DrawStrategicSymbols(const World& world, float zoom, const std::vector<entt::entity>& selected)
    {
        for (const Mine& mine : world.mines)
        {
            const Vector2 p = ToRay(HexCenter(IndexToHex(world.map, mine.hex), world.map.hexDistance));
            const int owner = world.map.hexes[static_cast<std::size_t>(mine.hex)].owner;
            const float radius = 3.5f / zoom;
            DrawCircleV(p, radius, Fade(ResourceColor(mine.resource), mine.connected ? 1.0f : 0.45f));
            DrawCircleLinesV(p, radius, owner >= 0 ? FactionColor(owner) : BLACK);
        }

        for (const auto [entity, base, position, faction] : world.registry.view<Base, Position, Faction>().each())
        {
            const float half = (base.type == BaseType::Hq ? 9.0f : (base.type == BaseType::Corps ? 7.5f : 6.0f)) / zoom;
            const Vector2 p = ToRay(position.value);
            const Rectangle box = { p.x - half, p.y - half, 2.0f * half, 2.0f * half };
            DrawRectangleRec(box, FactionColor(faction.id));
            DrawRectangleLinesEx(box, 1.25f / zoom, BLACK);

            const char* label = base.type == BaseType::Hq ? "HQ" : (base.type == BaseType::Corps ? "XXX" : "XX");
            const float size = half;
            const float spacing = 0.5f / zoom;
            const Vector2 measure = MeasureTextEx(GetFontDefault(), label, size, spacing);
            DrawTextEx(GetFontDefault(), label, { p.x - measure.x / 2.0f, p.y - measure.y / 2.0f }, size, spacing, BLACK);
        }

        const float radius = CounterHalfSize(zoom).y * 0.7f;
        for (const auto [entity, division, position, faction] : world.registry.view<Division, Position, Faction>().each())
        {
            const Vector2 p = ToRay(position.value);
            DrawCircleV(p, radius, FactionColor(faction.id));
            DrawCircleLinesV(p, radius, BLACK);
        }

        // 겹친 기호 위에서도 선택 표시가 보이게 마지막에 그린다.
        for (const entt::entity entity : selected)
        {
            const Position* position = world.registry.try_get<Position>(entity);
            if (position != nullptr)
            {
                DrawCircleLinesV(ToRay(position->value), radius + 1.5f / zoom, UiColors::Accent);
            }
            // 이동 경로도 헥스 꺾은선 대신 스플라인으로.
            const MovePath* path = world.registry.try_get<MovePath>(entity);
            if (position != nullptr && path != nullptr)
            {
                std::vector<Vec2> points = { position->value };
                for (const int hex : path->hexes)
                {
                    points.push_back(HexCenter(IndexToHex(world.map, hex), world.map.hexDistance));
                }
                DrawCurve(SampleCatmullRom(points, 4, false), 2.0f / zoom, Fade(UiColors::Accent, 0.8f));
            }
        }
    }
}

MapView::~MapView()
{
    Release();
}

MapView::MapView(MapView&& other) noexcept
{
    *this = std::move(other);
}

MapView& MapView::operator=(MapView&& other) noexcept
{
    if (this != &other)
    {
        Release();
        terrain = std::exchange(other.terrain, {});
        territory = std::exchange(other.territory, {});
        territoryPixels = std::exchange(other.territoryPixels, {});
        coastCurves = std::move(other.coastCurves);
        riverCurves = std::move(other.riverCurves);
        roadCurves = std::move(other.roadCurves);
        frontCurves = std::move(other.frontCurves);
        lastOwners = std::move(other.lastOwners);
        lastRefresh = std::exchange(other.lastRefresh, -1000.0f);
    }
    return *this;
}

void MapView::Release()
{
    if (terrain.id != 0)
    {
        UnloadTexture(terrain);
        terrain = {};
    }
    if (territory.id != 0)
    {
        UnloadTexture(territory);
        territory = {};
    }
    if (territoryPixels.data != nullptr)
    {
        UnloadImage(territoryPixels);
        territoryPixels = {};
    }
}

void MapView::Refresh(const World& world, const GameData& data)
{
    const HexMap& map = world.map;
    if (map.cols <= 0 || map.rows <= 0 || map.hexes.empty())
    {
        return;
    }

    const bool firstRefresh = terrain.id == 0;
    if (!firstRefresh && world.time - lastRefresh < TerritoryRefreshSeconds)
    {
        return;
    }
    lastRefresh = world.time;

    if (firstRefresh)
    {
        // 지형을 올린 뒤 같은 이미지 버퍼를 영토 갱신에 사용한다.
        territoryPixels = GenImageColor(2 * map.cols + 1, map.rows, BLANK);
        FillRaster(territoryPixels, map, [&](const Hex& hex)
        {
            const auto& color = data.terrain[ToIndex(hex.terrain)].color;
            return Color{ color[0], color[1], color[2], 255 };
        });
        // 4배 bicubic 확대로 텍셀 계단을 부드럽게 한다 (크기 비율은 같아서 DrawRaster 매핑은 그대로).
        Image smooth = ImageCopy(territoryPixels);
        ImageResize(&smooth, smooth.width * RasterScale, smooth.height * RasterScale);
        terrain = LoadTextureFromImage(smooth);
        UnloadImage(smooth);
        if (terrain.id == 0)
        {
            Release();
            return;
        }
        SetTextureFilter(terrain, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(terrain, TEXTURE_WRAP_CLAMP);
    }
    else if (std::equal(map.hexes.begin(), map.hexes.end(), lastOwners.begin(), lastOwners.end(),
                       [](const Hex& hex, int owner) { return hex.owner == owner; }))
    {
        return;
    }

    FillRaster(territoryPixels, map, [](const Hex& hex)
    {
        Color color = hex.owner >= 0 ? FactionColor(hex.owner) : BLANK;
        color.a = hex.owner >= 0 ? 60 : 0;
        return color;
    });
    if (firstRefresh)
    {
        territory = LoadTextureFromImage(territoryPixels);
        if (territory.id == 0)
        {
            Release();
            return;
        }
        SetTextureFilter(territory, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(territory, TEXTURE_WRAP_CLAMP);

        coastCurves = SampleChains(world.features.coastlines, map.hexDistance * 0.6f);
        riverCurves = SampleChains(world.features.rivers, map.hexDistance * 0.3f);
        roadCurves.clear();
        roadCurves.reserve(world.features.roads.size());
        for (const MapRoad& road : world.features.roads)
        {
            roadCurves.push_back(SampleCatmullRom(road.points, 4, false));
        }
    }
    else
    {
        UpdateTexture(territory, territoryPixels.data);
    }

    lastOwners.resize(map.hexes.size());
    std::transform(map.hexes.begin(), map.hexes.end(), lastOwners.begin(), [](const Hex& hex) { return hex.owner; });
    frontCurves = SampleChains(FrontLines(map), map.hexDistance * 0.6f);
}

void MapView::DrawMap(const World& world, const GameData&, MapMode mode, float zoom) const
{
    if (terrain.id == 0)
    {
        return;
    }

    DrawRaster(terrain, world.map);
    DrawRaster(territory, world.map);

    const bool strategic = mode == MapMode::Strategic;
    for (const auto& curve : coastCurves)
    {
        DrawCurve(curve, 2.0f / zoom, CoastColor);
    }
    for (const auto& curve : riverCurves)
    {
        DrawCurve(curve, (strategic ? 1.5f : 3.0f) / zoom, RiverColor);
    }
    if (!strategic)
    {
        for (const auto& curve : roadCurves)
        {
            DrawCurve(curve, 3.5f / zoom, RoadEdgeColor);
        }
        for (const auto& curve : roadCurves)
        {
            DrawCurve(curve, 2.0f / zoom, RoadColor);
        }
    }

    DrawSupplyNetwork(world, strategic, zoom);
    for (const auto& curve : frontCurves)
    {
        DrawCurve(curve, (strategic ? 4.0f : 3.0f) / zoom, BLACK);
    }
}

void MapView::DrawUnits(const World& world, const GameData& data, MapMode mode, float zoom,
                        const std::vector<entt::entity>& selected) const
{
    if (mode == MapMode::Strategic)
    {
        DrawStrategicSymbols(world, zoom, selected);
        return;
    }

    DrawEngagementZones(world, data.config.engageKm);
    DrawCitySites(world, zoom);
    DrawWorldUnits(world, data, zoom, selected);
}
