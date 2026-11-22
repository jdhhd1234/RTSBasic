#include "app/map_view.hpp"
#include "ai/ai.hpp"
#include "app/render.hpp"
#include "app/ui.hpp"
#include "sim/components.hpp"
#include "sim/spline.hpp"

#include <rlgl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <numbers>
#include <string>
#include <utility>

namespace
{
    constexpr float TerritoryRefreshSeconds = 0.25f;
    constexpr int RasterScale = 4;
    constexpr float DivisionZoom = 1.0f;
    constexpr float BrigadeZoom = 2.4f;
    constexpr float BattalionZoom = 4.5f;
    constexpr float CityZoom = 0.85f;
    constexpr float SectorIntervalKm = 120.0f;
    constexpr float SectorRadiusKm = 80.0f;
    constexpr Color CoastColor = { 41, 63, 76, 255 };
    constexpr Color RiverColor = { 50, 115, 168, 255 };
    constexpr Color RoadEdgeColor = { 103, 78, 55, 255 };
    constexpr Color HighwayColor = { 240, 197, 107, 255 };
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

    float ZoomBlend(float zoom, float threshold)
    {
        const float t = std::clamp((zoom - threshold * 0.8f) / (threshold * 0.4f), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    float TacticalDetail(float zoom)
    {
        return ZoomBlend(zoom, BrigadeZoom);
    }

    const Position* NearestOwnBase(const World& world, Vec2 from, int factionId, BaseType first, BaseType last)
    {
        const Position* nearest = nullptr;
        float nearestDistance = std::numeric_limits<float>::infinity();
        for (const auto [entity, base, position, faction] : world.registry.view<Base, Position, Faction>().each())
        {
            if (faction.id != factionId || base.type < first || base.type > last)
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

    void DrawSupplyNetwork(const World& world, float zoom, float detail)
    {
        if (detail < 1.0f)
        {
            for (const auto [entity, base, position, faction] : world.registry.view<Base, Position, Faction>().each())
            {
                if (base.type == BaseType::Hq)
                {
                    continue;
                }
                const BaseType parentType = base.type == BaseType::Division ? BaseType::Corps : BaseType::Hq;
                const Position* parent = NearestOwnBase(world, position.value, faction.id, parentType, parentType);
                if (parent == nullptr && parentType == BaseType::Corps)
                {
                    parent = NearestOwnBase(world, position.value, faction.id, BaseType::Hq, BaseType::Hq);
                }
                if (parent != nullptr)
                {
                    DrawCurve(SampleArc(parent->value, position.value, 0.15f, 12), 2.0f / zoom,
                              Fade(FactionColor(faction.id), 0.60f * (1.0f - detail)));
                }
            }
        }
        if (detail > 0.0f)
        {
            for (const auto [entity, division, position, faction] : world.registry.view<Division, Position, Faction>().each())
            {
                const Position* base = NearestOwnBase(world, position.value, faction.id, BaseType::Hq, BaseType::Division);
                if (base != nullptr)
                {
                    DrawCurve(SampleArc(position.value, base->value, 0.15f, 12), 1.25f / zoom,
                              Fade(FactionColor(faction.id), 0.45f * detail));
                }
            }
        }
    }

    std::vector<map_view_detail::FrontSector> SampleFrontSectors(const std::vector<std::vector<Vec2>>& curves)
    {
        std::vector<map_view_detail::FrontSector> sectors;
        for (const auto& curve : curves)
        {
            float length = 0.0f;
            for (std::size_t i = 1; i < curve.size(); ++i)
            {
                length += Distance(curve[i - 1], curve[i]);
            }
            if (length <= 0.0f)
            {
                continue;
            }

            const float interval = length / std::max(1.0f, std::round(length / SectorIntervalKm));
            float next = interval * 0.5f;
            float walked = 0.0f;
            for (std::size_t i = 1; i < curve.size(); ++i)
            {
                const Vec2 a = curve[i - 1];
                const Vec2 b = curve[i];
                const float span = Distance(a, b);
                if (span <= 0.0f)
                {
                    continue;
                }
                while (next <= walked + span)
                {
                    const float t = (next - walked) / span;
                    Vec2 normal = { (a.y - b.y) / span, (b.x - a.x) / span };
                    if (normal.y > 0.0f)
                    {
                        normal = { -normal.x, -normal.y };
                    }
                    sectors.push_back({ { std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t) }, normal });
                    next += interval;
                }
                walked += span;
            }
        }
        return sectors;
    }

    bool ReserveLabel(Rectangle box, std::vector<Rectangle>& labels)
    {
        for (const Rectangle used : labels)
        {
            if (CheckCollisionRecs(box, used))
            {
                return false;
            }
        }
        labels.push_back(box);
        return true;
    }

    void DrawFrontPower(const World& world, const std::vector<map_view_detail::FrontSector>& sectors, float zoom, float alpha,
                        std::vector<Rectangle>& labels)
    {
        const auto divisions = world.registry.view<Division, Position, Faction>();
        for (const auto& [position, normal] : sectors)
        {
            std::array<float, FactionCount> hp{};
            for (const auto [entity, division, unit, faction] : divisions.each())
            {
                const float dx = unit.value.x - position.x;
                const float dy = unit.value.y - position.y;
                if (dx * dx + dy * dy <= SectorRadiusKm * SectorRadiusKm)
                {
                    hp[static_cast<std::size_t>(faction.id)] += division.hp;
                }
            }
            const float total = hp[PlayerFaction] + hp[AiFaction];
            if (total <= 0.0f)
            {
                continue;
            }

            const std::string blue = std::to_string(static_cast<int>(std::round(hp[PlayerFaction])));
            const std::string red = std::to_string(static_cast<int>(std::round(hp[AiFaction])));
            const Font font = GetFontDefault();
            const float size = 11.0f / zoom;
            const float spacing = 1.0f / zoom;
            const float blueWidth = MeasureTextEx(font, blue.c_str(), size, spacing).x;
            const float redWidth = MeasureTextEx(font, red.c_str(), size, spacing).x;
            const float halfWidth = (blueWidth + redWidth + 18.0f / zoom) * 0.5f;
            const float halfHeight = 10.0f / zoom;
            const float offset = std::abs(normal.x) * halfWidth + std::abs(normal.y) * halfHeight + 5.0f / zoom;
            const Vector2 center = { position.x + normal.x * offset, position.y + normal.y * offset };
            const Rectangle badge = { center.x - halfWidth, center.y - halfHeight, 2.0f * halfWidth, 2.0f * halfHeight };
            if (!ReserveLabel(badge, labels))
            {
                continue;
            }

            DrawLineEx(ToRay(position), center, 1.0f / zoom, Fade(BLACK, alpha * 0.55f));
            DrawRectangleRec(badge, Fade(Color{ 246, 240, 222, 255 }, alpha * 0.93f));
            DrawRectangleLinesEx(badge, 1.0f / zoom, Fade(BLACK, alpha * 0.6f));
            const float left = badge.x + 4.0f / zoom;
            const float y = badge.y + 2.0f / zoom;
            DrawTextEx(font, blue.c_str(), { left, y }, size, spacing, Fade(Color{ 26, 66, 143, 255 }, alpha));
            DrawTextEx(font, red.c_str(), { badge.x + badge.width - redWidth - 4.0f / zoom, y },
                       size, spacing, Fade(Color{ 145, 38, 32, 255 }, alpha));
            const float barY = badge.y + 14.0f / zoom;
            const float width = badge.width - 8.0f / zoom;
            const float blueBar = width * hp[PlayerFaction] / total;
            DrawRectangleRec({ left, barY, blueBar, 3.0f / zoom }, Fade(FactionColor(PlayerFaction), alpha));
            DrawRectangleRec({ left + blueBar, barY, width - blueBar, 3.0f / zoom }, Fade(FactionColor(AiFaction), alpha));
        }
    }

    void DrawCityLabel(const World& world, int hex, const std::string& name, CityTier tier, float zoom, float alpha,
                       std::vector<Rectangle>& labels)
    {
        if (hex < 0 || static_cast<std::size_t>(hex) >= world.map.hexes.size())
        {
            return;
        }
        const Vec2 position = HexCenter(IndexToHex(world.map, hex), world.map.hexDistance);
        const float radius = (tier == CityTier::Metro ? 4.5f : (tier == CityTier::City ? 3.0f : 2.0f)) / zoom;
        const auto index = static_cast<std::size_t>(hex);
        const bool hasBase = index < world.baseAtHex.size() && world.baseAtHex[index] != entt::null;
        if (!hasBase)
        {
            DrawCircleV(ToRay(position), radius, Fade(Color{ 58, 46, 37, 255 }, alpha));
        }

        const std::string label = name.empty() ? Translate("City") : name;
        const int pixels = tier == CityTier::Metro ? 15 : (tier == CityTier::City ? 13 : 11);
        const float size = static_cast<float>(pixels) / zoom;
        const float left = position.x + (hasBase ? 14.0f / zoom : radius + 4.0f / zoom);
        // shortcut: 비 ASCII 라벨 폭은 글자 수로 근사, 원문 폰트 측정 API 추가 시 교체.
        const bool ascii = std::all_of(label.begin(), label.end(), [](unsigned char ch) { return ch < 128; });
        const float width = ascii ? MeasureTextEx(GetFontDefault(), label.c_str(), size, size * 0.1f).x
                                  : static_cast<float>(GetCodepointCount(label.c_str())) * size;
        const Rectangle box = { left - 2.0f / zoom, position.y - size * 0.6f, width + 5.0f / zoom, size * 1.25f };
        if (!ReserveLabel(box, labels))
        {
            return;
        }

        DrawRectangleRec(box, Fade(Color{ 246, 240, 222, 255 }, alpha * 0.85f));
        // 화면 글꼴 크기는 유지하고 데이터 이름은 번역하지 않는다.
        rlPushMatrix();
        rlTranslatef(left, position.y - size * 0.5f, 0.0f);
        rlScalef(1.0f / zoom, 1.0f / zoom, 1.0f);
        TextDrawRaw(label.c_str(), 0, 0, pixels, Fade(Color{ 58, 46, 37, 255 }, alpha));
        rlPopMatrix();
    }

    void DrawCities(const World& world, float zoom, float detail, std::vector<Rectangle>& labels)
    {
        for (const CityTier tier : { CityTier::Metro, CityTier::City, CityTier::Town })
        {
            const float alpha = tier == CityTier::Metro ? 1.0f : (tier == CityTier::City ? ZoomBlend(zoom, CityZoom) : detail);
            if (alpha <= 0.0f)
            {
                continue;
            }
            for (const MapCity& city : world.features.cities)
            {
                if (city.tier == tier)
                {
                    DrawCityLabel(world, city.hex, city.name, tier, zoom, alpha, labels);
                }
            }
        }

        if (world.features.cities.empty() && detail > 0.0f)
        {
            for (const int index : world.features.sites)
            {
                if (index >= 0 && static_cast<std::size_t>(index) < world.map.hexes.size() &&
                    world.map.hexes[static_cast<std::size_t>(index)].terrain == Terrain::City)
                {
                    DrawCityLabel(world, index, {}, CityTier::Town, zoom, detail, labels);
                }
            }
        }
    }

    bool IsSelected(entt::entity entity, const std::vector<entt::entity>& selected)
    {
        return std::find(selected.begin(), selected.end(), entity) != selected.end();
    }

    void DrawCorps(const World& world, float zoom, const std::vector<entt::entity>& selected, float alpha)
    {
        std::vector<map_view_detail::CorpsGroup> groups;
        for (const auto [entity, division, position, faction] : world.registry.view<Division, Position, Faction>().each())
        {
            const Position* base = NearestOwnBase(world, position.value, faction.id, BaseType::Hq, BaseType::Corps);
            auto group = std::find_if(groups.begin(), groups.end(), [&](const auto& item)
            {
                return item.base == base && item.faction == faction.id;
            });
            if (group == groups.end())
            {
                groups.push_back({ .base = base, .faction = faction.id });
                group = std::prev(groups.end());
            }

            group->positionSum.x += position.value.x;
            group->positionSum.y += position.value.y;
            ++group->count;
            group->hp += division.hp;
            group->maxHp += division.maxHp;
            group->selected = group->selected || IsSelected(entity, selected);
        }

        // 상위 기지를 모두 잃은 진영도 기지 없는 군단 기호 하나로 표시한다.
        const Vector2 half = { 16.0f / zoom, 10.5f / zoom };
        for (const bool selectedPass : { false, true })
        {
            for (const auto& group : groups)
            {
                if (group.selected != selectedPass)
                {
                    continue;
                }
                const float count = static_cast<float>(group.count);
                const Vector2 p = { group.positionSum.x / count, group.positionSum.y / count };
                if (group.base != nullptr)
                {
                    DrawLineEx(p, ToRay(group.base->value), 1.5f / zoom, Fade(FactionColor(group.faction), 0.70f * alpha));
                }
                const Rectangle symbol = DrawUnitFrame(p, half, group.faction, group.selected, "XXX", zoom, alpha);
                const std::string label = std::to_string(group.count);
                const float size = 13.0f / zoom;
                const float spacing = 1.0f / zoom;
                const float width = MeasureTextEx(GetFontDefault(), label.c_str(), size, spacing).x;
                DrawTextEx(GetFontDefault(), label.c_str(), { symbol.x + (symbol.width - width) / 2.0f, p.y - size / 2.0f },
                           size, spacing, Fade(BLACK, alpha));
                DrawUnitHpBar({ p.x - half.x, p.y + half.y + 4.0f / zoom }, half.x * 2.0f, 3.0f / zoom,
                              group.hp, group.maxHp, alpha);
            }
        }
    }

    bool InCombat(const World& world, const Division& division)
    {
        return world.time - division.lastCombatTime < 1.0f;
    }

    void DrawTacticalLines(const World& world, float zoom, const std::vector<entt::entity>& selected, float alpha)
    {
        for (const auto [entity, division, position, faction] : world.registry.view<Division, Position, Faction>().each())
        {
            if (InCombat(world, division))
            {
                DrawCircleV(ToRay(position.value), division.frontageKm, Fade(EngagementColor, 0.11f * alpha));
                DrawCircleLinesV(ToRay(position.value), division.frontageKm, Fade(EngagementColor, 0.55f * alpha));
            }
            if (division.hasTarget)
            {
                DrawLineEx(ToRay(position.value), ToRay(division.targetPosition), 1.5f / zoom, Fade(FactionColor(faction.id), 0.85f * alpha));
            }
        }

        for (const entt::entity entity : selected)
        {
            const Position* position = world.registry.try_get<Position>(entity);
            const MovePath* path = world.registry.try_get<MovePath>(entity);
            if (position != nullptr && path != nullptr)
            {
                std::vector<Vec2> points = { position->value };
                points.reserve(path->hexes.size() + 1);
                for (const int hex : path->hexes)
                {
                    points.push_back(HexCenter(IndexToHex(world.map, hex), world.map.hexDistance));
                }
                DrawCurve(SampleCatmullRom(points, 4, false), 2.0f / zoom, Fade(UiColors::Accent, 0.85f * alpha));
            }
        }
    }

    void DrawSelectedFrontage(Vec2 position, Vec2 axis, float frontage, float zoom, float alpha)
    {
        const float half = frontage * 0.5f;
        const Vector2 a = { position.x - axis.x * half, position.y - axis.y * half };
        const Vector2 b = { position.x + axis.x * half, position.y + axis.y * half };
        DrawCircleV(ToRay(position), frontage, Fade(UiColors::Accent, 0.08f * alpha));
        DrawCircleLinesV(ToRay(position), frontage, Fade(UiColors::Accent, 0.40f * alpha));
        DrawLineEx(a, b, 2.5f / zoom, Fade(UiColors::Accent, 0.72f * alpha));
        DrawCircleLinesV(ToRay(position), 4.0f / zoom, Fade(UiColors::Accent, alpha));
    }

    void DrawSubunits(const Division& division, const GameData& data, Vec2 position, int faction, bool selected,
                      UnitEchelon echelon, float zoom, float alpha)
    {
        constexpr std::size_t BrigadeBattalions = 3;
        const bool brigade = echelon == UnitEchelon::Brigade;
        const std::size_t stride = brigade ? BrigadeBattalions : 1;
        const std::size_t count = (division.battalions.size() + stride - 1) / stride;
        if (count == 0)
        {
            return;
        }

        const float step = division.frontageKm / static_cast<float>(count);   // 기호 크기 계산용 평균 간격
        const Vector2 nominal = brigade ? Vector2{ 15.0f, 10.0f } : Vector2{ 12.0f, 8.0f };
        const float scale = count == 1 ? 1.0f : std::clamp(step * zoom / (2.0f * nominal.x + 2.0f), 0.7f, 1.0f);
        const Vector2 half = { nominal.x * scale / zoom, nominal.y * scale / zoom };
        for (std::size_t i = 0; i < count; ++i)
        {
            // 대대 AI가 고른 실제 위치. 여단은 소속 대대들의 중심.
            const std::size_t start = i * stride;
            const std::size_t end = std::min(start + stride, division.battalions.size());
            Vector2 p = { 0.0f, 0.0f };
            for (std::size_t b = start; b < end; ++b)
            {
                const Vec2 unit = b < division.units.size() ? division.units[b].position : position;
                p.x += unit.x / static_cast<float>(end - start);
                p.y += unit.y / static_cast<float>(end - start);
            }
            if (!brigade && start < division.units.size() && division.units[start].task == BattalionTask::Engage)
            {
                DrawLineEx(p, ToRay(division.units[start].goal), 1.0f / zoom, Fade(FactionColor(faction), 0.5f * alpha));
            }
            const Rectangle symbol = DrawUnitFrame(p, half, faction, selected, brigade ? "X" : "II", zoom, alpha);
            const BattalionDef* def = &data.battalions[static_cast<std::size_t>(division.battalions[start])];
            for (std::size_t b = start + 1; b < end && def->unitClass == UnitClass::Support; ++b)
            {
                def = &data.battalions[static_cast<std::size_t>(division.battalions[b])];
            }
            DrawUnitSymbol(symbol, def->unitClass, def->id, 1.25f * scale / zoom, alpha);
        }
    }

    void DrawTacticalUnits(const World& world, const GameData& data, float zoom, const std::vector<entt::entity>& selected,
                           float brigade, float battalion, float detail)
    {
        const auto divisions = world.registry.view<Division, Position, Faction>();
        for (const bool selectedPass : { false, true })
        {
            for (const auto [entity, division, position, faction] : divisions.each())
            {
                const bool isSelected = IsSelected(entity, selected);
                if (isSelected != selectedPass)
                {
                    continue;
                }
                const Vec2 facing = DivisionFacing(world, position.value, faction.id);
                const Vec2 axis = { -facing.y, facing.x };
                if (isSelected)
                {
                    DrawSelectedFrontage(position.value, axis, division.frontageKm, zoom, detail);
                }
                if (brigade > 0.0f)
                {
                    DrawSubunits(division, data, position.value, faction.id, isSelected, UnitEchelon::Brigade, zoom, brigade);
                }
                if (battalion > 0.0f)
                {
                    DrawSubunits(division, data, position.value, faction.id, isSelected, UnitEchelon::Battalion, zoom, battalion);
                }

                // 전투 상태는 여단/대대가 공유하는 사단 위치에 표시한다.
                const Vector2 badge = { position.value.x, position.value.y - 22.0f / zoom };
                if (world.registry.all_of<Retreating>(entity))
                {
                    DrawCircleV(badge, 6.0f / zoom, Fade(Color{ 34, 28, 23, 255 }, 0.85f * detail));
                    DrawTextEx(GetFontDefault(), "R", { badge.x - 3.0f / zoom, badge.y - 5.0f / zoom }, 10.0f / zoom, 1.0f / zoom,
                               Fade(UiColors::Accent, detail));
                }
                else if (InCombat(world, division))
                {
                    DrawCircleV(badge, 5.0f / zoom, Fade(BLACK, detail));
                    DrawCircleV(badge, 3.5f / zoom, Fade(EngagementColor, detail));
                }
            }
        }
    }
}

float EchelonAlpha(UnitEchelon echelon, float zoom)
{
    // 확인: 각 전환 중심에서 인접 두 단계가 0.5, 전환 양끝에서 1과 0을 맞바꾼다.
    switch (echelon)
    {
    case UnitEchelon::Corps:
        return 1.0f - ZoomBlend(zoom, DivisionZoom);
    case UnitEchelon::Division:
        return ZoomBlend(zoom, DivisionZoom) - ZoomBlend(zoom, BrigadeZoom);
    case UnitEchelon::Brigade:
        return ZoomBlend(zoom, BrigadeZoom) - ZoomBlend(zoom, BattalionZoom);
    case UnitEchelon::Battalion:
        return ZoomBlend(zoom, BattalionZoom);
    }
    return 0.0f;
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
        frontSectors = std::move(other.frontSectors);
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
    frontSectors = SampleFrontSectors(frontCurves);
}

void MapView::DrawMap(const World& world, const GameData&, float zoom) const
{
    if (terrain.id == 0)
    {
        return;
    }

    DrawRaster(terrain, world.map);
    DrawRaster(territory, world.map);

    const float detail = TacticalDetail(zoom);
    for (const auto& curve : coastCurves)
    {
        DrawCurve(curve, 2.0f / zoom, CoastColor);
    }
    for (const auto& curve : riverCurves)
    {
        DrawCurve(curve, std::lerp(1.25f, 3.0f, detail) / zoom, Fade(RiverColor, std::lerp(0.55f, 1.0f, detail)));
    }
    for (const RoadTier tier : { RoadTier::Minor, RoadTier::Major, RoadTier::Highway })
    {
        const bool minor = tier == RoadTier::Minor;
        const float alpha = minor ? detail : std::lerp(1.0f, 0.7f, detail);
        if (alpha <= 0.0f)
        {
            continue;
        }
        const float width = (tier == RoadTier::Highway ? 3.0f : (minor ? 1.25f : 2.0f)) / zoom;
        const Color color = tier == RoadTier::Highway ? HighwayColor : RoadColor;
        for (std::size_t i = 0; i < roadCurves.size(); ++i)
        {
            if (world.features.roads[i].tier == tier)
            {
                DrawCurve(roadCurves[i], width + 1.25f / zoom, Fade(RoadEdgeColor, alpha));
                DrawCurve(roadCurves[i], width, Fade(color, alpha));
            }
        }
    }

    DrawSupplyNetwork(world, zoom, detail);
    for (const auto& curve : frontCurves)
    {
        DrawCurve(curve, std::lerp(3.25f, 2.25f, detail) / zoom, BLACK);
    }

    std::vector<Rectangle> labels;
    DrawCities(world, zoom, detail, labels);
    if (detail < 1.0f)
    {
        DrawFrontPower(world, frontSectors, zoom, 1.0f - detail, labels);
    }
}

void MapView::DrawUnits(const World& world, const GameData& data, float zoom,
                        const std::vector<entt::entity>& selected) const
{
    const float corps = EchelonAlpha(UnitEchelon::Corps, zoom);
    const float division = EchelonAlpha(UnitEchelon::Division, zoom);
    const float brigade = EchelonAlpha(UnitEchelon::Brigade, zoom);
    const float battalion = EchelonAlpha(UnitEchelon::Battalion, zoom);
    const float detail = TacticalDetail(zoom);

    if (detail > 0.0f)
    {
        DrawTacticalLines(world, zoom, selected, detail);
    }
    DrawMapSites(world, zoom, detail);

    if (corps > 0.0f)
    {
        DrawCorps(world, zoom, selected, corps);
    }
    if (division > 0.0f)
    {
        DrawWorldUnits(world, data, zoom, selected, division);
    }
    if (detail > 0.0f)
    {
        DrawTacticalUnits(world, data, zoom, selected, brigade, battalion, detail);
    }
}
