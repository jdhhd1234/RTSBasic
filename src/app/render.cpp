#include "app/render.hpp"
#include "app/ui.hpp"
#include "sim/components.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <numbers>
#include <string>

namespace
{
    Color TerrainColor(const GameData& data, Terrain terrain)
    {
        const auto& c = data.terrain[ToIndex(terrain)].color;
        return { c[0], c[1], c[2], 255 };
    }

    Color WeatherTint(Weather weather)
    {
        switch (weather)
        {
        case Weather::Rain: return { 40, 70, 140, 45 };
        case Weather::Snow: return { 245, 245, 255, 80 };
        case Weather::Fog: return { 150, 150, 150, 70 };
        default: return { 0, 0, 0, 0 };
        }
    }

    // 사단의 대표 병과: 지원이 아닌 첫 대대.
    UnitClass PrimaryClass(const Division& division, const GameData& data)
    {
        for (const int b : division.battalions)
        {
            const UnitClass cls = data.battalions[static_cast<std::size_t>(b)].unitClass;
            if (cls != UnitClass::Support)
            {
                return cls;
            }
        }
        return UnitClass::Support;
    }

    // shortcut: APP-6 기호를 선·도형으로 단순화해 그림. milsymbol PNG 텍스처는 에셋 단계에서 교체.
    void DrawUnitSymbol(Rectangle box, UnitClass cls, const std::string& fallbackId, float thick)
    {
        const Vector2 center = { box.x + box.width / 2.0f, box.y + box.height / 2.0f };
        const Color ink = BLACK;
        const auto cross = [&]()
        {
            DrawLineEx({ box.x, box.y }, { box.x + box.width, box.y + box.height }, thick, ink);
            DrawLineEx({ box.x, box.y + box.height }, { box.x + box.width, box.y }, thick, ink);
        };
        const auto oval = [&]()
        {
            DrawEllipseLines(static_cast<int>(center.x), static_cast<int>(center.y), box.width * 0.36f, box.height * 0.30f, ink);
            DrawEllipseLines(static_cast<int>(center.x), static_cast<int>(center.y), box.width * 0.36f - thick * 0.5f, box.height * 0.30f - thick * 0.5f, ink);
        };

        switch (cls)
        {
        case UnitClass::Inf:
            cross();
            break;
        case UnitClass::Mech:
            cross();
            oval();
            break;
        case UnitClass::Armor:
            oval();
            break;
        case UnitClass::Arty:
            DrawCircleV(center, box.height * 0.2f, ink);
            break;
        default:
        {
            std::string label = fallbackId;
            std::transform(label.begin(), label.end(), label.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
            const float size = box.height * 0.75f;
            const Vector2 measure = MeasureTextEx(GetFontDefault(), label.c_str(), size, size * 0.1f);
            DrawTextEx(GetFontDefault(), label.c_str(), { center.x - measure.x / 2.0f, center.y - measure.y / 2.0f }, size, size * 0.1f, ink);
            break;
        }
        }
    }

    constexpr std::array<const char*, ResourceCount> ResourceShort = { "Oil", "Fe", "Ni", "Qz", "RE", "Mn" };

    // 광산: 자원 색 원 + 약자. 공장과 끊긴 광산은 흐리게.
    void DrawMines(const World& world, float zoom)
    {
        const float radius = 6.0f / zoom;
        for (const Mine& mine : world.mines)
        {
            const Vector2 p = ToRay(HexCenter(IndexToHex(world.map, mine.hex), world.map.hexDistance));
            const int owner = world.map.hexes[static_cast<std::size_t>(mine.hex)].owner;
            const float alpha = mine.connected ? 1.0f : 0.45f;
            DrawCircleV(p, radius, Fade(ResourceColor(mine.resource), alpha));
            DrawCircleLinesV(p, radius, owner >= 0 ? FactionColor(owner) : BLACK);
            const char* label = ResourceShort[ToIndex(mine.resource)];
            DrawTextEx(GetFontDefault(), label, { p.x + radius * 1.2f, p.y - radius }, 10.0f / zoom, 1.0f / zoom, Fade(BLACK, alpha));
        }
    }

    void DrawHpBar(Vector2 topLeft, float width, float height, float ratio)
    {
        ratio = std::clamp(ratio, 0.0f, 1.0f);
        DrawRectangleV(topLeft, { width, height }, Color{ 30, 30, 30, 220 });
        const Color fill = ratio > 0.6f ? Color{ 80, 200, 90, 255 } : (ratio > 0.3f ? Color{ 230, 190, 60, 255 } : Color{ 220, 70, 60, 255 });
        DrawRectangleV(topLeft, { width * ratio, height }, fill);
    }
}

Color ResourceColor(Resource resource)
{
    constexpr std::array<Color, ResourceCount> colors = {
        Color{ 30, 30, 30, 255 }, Color{ 150, 90, 70, 255 }, Color{ 120, 160, 150, 255 },
        Color{ 230, 230, 245, 255 }, Color{ 200, 120, 220, 255 }, Color{ 110, 80, 140, 255 },
    };
    return colors[ToIndex(resource)];
}

Color FactionColor(int faction)
{
    return faction == PlayerFaction ? UiColors::Player : UiColors::Ai;
}

Vector2 CounterHalfSize(float zoom)
{
    return { 13.0f / zoom, 9.0f / zoom };
}

void DrawWorldMap(const World& world, const GameData& data, float zoom)
{
    const HexMap& map = world.map;
    const float radius = map.hexDistance / std::numbers::sqrt3_v<float>;

    for (int i = 0; i < static_cast<int>(map.hexes.size()); ++i)
    {
        const Hex& hex = map.hexes[static_cast<std::size_t>(i)];
        const Vector2 center = ToRay(HexCenter(IndexToHex(map, i), map.hexDistance));
        DrawPoly(center, 6, radius + 0.4f, 30.0f, TerrainColor(data, hex.terrain));
        if (hex.owner >= 0)
        {
            DrawPoly(center, 6, radius + 0.4f, 30.0f, Fade(FactionColor(hex.owner), 0.22f));
        }
        const Color tint = WeatherTint(world.weather[static_cast<std::size_t>(hex.weatherRegion)].weather);
        if (tint.a > 0)
        {
            DrawPoly(center, 6, radius + 0.4f, 30.0f, tint);
        }
        DrawPolyLines(center, 6, radius, 30.0f, Fade(BLACK, 0.10f));
    }

    // 전선: 소유 진영이 다른 인접 헥스 사이 변
    const float thick = 2.5f / zoom;
    for (int i = 0; i < static_cast<int>(map.hexes.size()); ++i)
    {
        const int owner = map.hexes[static_cast<std::size_t>(i)].owner;
        if (owner < 0)
        {
            continue;
        }
        const HexCoord coord = IndexToHex(map, i);
        const Vec2 a = HexCenter(coord, map.hexDistance);
        for (const HexCoord nb : HexNeighbors(coord))
        {
            if (!InBounds(map, nb))
            {
                continue;
            }
            const int j = HexIndex(map, nb);
            const int other = map.hexes[static_cast<std::size_t>(j)].owner;
            if (j < i || other < 0 || other == owner)
            {
                continue;
            }
            const Vec2 b = HexCenter(nb, map.hexDistance);
            const Vec2 mid = { (a.x + b.x) / 2.0f, (a.y + b.y) / 2.0f };
            const float length = Distance(a, b);
            const Vec2 perp = { -(b.y - a.y) / length * radius / 2.0f, (b.x - a.x) / length * radius / 2.0f };
            DrawLineEx({ mid.x + perp.x, mid.y + perp.y }, { mid.x - perp.x, mid.y - perp.y }, thick, Color{ 20, 20, 20, 230 });
        }
    }
}

void DrawWorldUnits(const World& world, const GameData& data, float zoom, const std::vector<entt::entity>& selected)
{
    const float line = 1.5f / zoom;

    // 선택한 사단의 이동 경로
    for (const entt::entity entity : selected)
    {
        if (!world.registry.valid(entity))
        {
            continue;
        }
        const MovePath* path = world.registry.try_get<MovePath>(entity);
        if (path == nullptr)
        {
            continue;
        }
        Vector2 from = ToRay(world.registry.get<Position>(entity).value);
        for (const int hex : path->hexes)
        {
            const Vector2 to = ToRay(HexCenter(IndexToHex(world.map, hex), world.map.hexDistance));
            DrawLineEx(from, to, line * 1.5f, Fade(UiColors::Accent, 0.8f));
            from = to;
        }
    }

    // 교전선: 사단 → 이번 틱 사격 대상
    for (const auto [entity, division, position, faction] : world.registry.view<Division, Position, Faction>().each())
    {
        if (division.hasTarget)
        {
            DrawLineEx(ToRay(position.value), ToRay(division.targetPosition), line, Fade(FactionColor(faction.id), 0.85f));
        }
    }

    DrawMines(world, zoom);

    // 기지 (아래에 공장 수)
    for (const auto [entity, base, position, faction] : world.registry.view<Base, Position, Faction>().each())
    {
        int factories = 0;
        for (const FactionState& state : world.factions)
        {
            factories += static_cast<int>(std::count_if(state.factories.begin(), state.factories.end(), [&](const FactoryState& f) { return f.hex == base.hex; }));
        }
        const float half = (base.type == BaseType::Hq ? 12.0f : (base.type == BaseType::Corps ? 10.0f : 8.0f)) / zoom;
        const Vector2 p = ToRay(position.value);
        const Rectangle rect = { p.x - half, p.y - half, half * 2.0f, half * 2.0f };
        DrawRectangleRec(rect, FactionColor(faction.id));
        DrawRectangleLinesEx(rect, line * 1.5f, BLACK);
        const char* label = base.type == BaseType::Hq ? "HQ" : (base.type == BaseType::Corps ? "XXX" : "XX");
        const float size = half * 0.9f;
        const Vector2 measure = MeasureTextEx(GetFontDefault(), label, size, size * 0.1f);
        DrawTextEx(GetFontDefault(), label, { p.x - measure.x / 2.0f, p.y - measure.y / 2.0f }, size, size * 0.1f, BLACK);
        DrawHpBar({ rect.x, rect.y + rect.height + 2.0f / zoom }, rect.width, 3.0f / zoom, base.hp / base.maxHp);
        if (factories > 0)
        {
            const std::string badge = "F" + std::to_string(factories);
            DrawTextEx(GetFontDefault(), badge.c_str(), { rect.x, rect.y + rect.height + 6.0f / zoom }, 11.0f / zoom, 1.0f / zoom, BLACK);
        }
    }

    // 사단: 아군 사각형, 적군 마름모 (APP-6 프레임)
    const Vector2 half = CounterHalfSize(zoom);
    for (const auto [entity, division, position, faction] : world.registry.view<Division, Position, Faction>().each())
    {
        const Vector2 p = ToRay(position.value);
        const Color color = FactionColor(faction.id);
        const bool isSelected = std::find(selected.begin(), selected.end(), entity) != selected.end();
        const bool retreating = world.registry.all_of<Retreating>(entity);
        const bool inCombat = world.time - division.lastCombatTime < 1.0f;

        Rectangle symbolBox;
        if (faction.id == PlayerFaction)
        {
            const Rectangle rect = { p.x - half.x, p.y - half.y, half.x * 2.0f, half.y * 2.0f };
            DrawRectangleRec(rect, color);
            DrawRectangleLinesEx(rect, isSelected ? line * 2.0f : line, isSelected ? UiColors::Accent : BLACK);
            symbolBox = { rect.x + half.x * 0.25f, rect.y + half.y * 0.25f, half.x * 1.5f, half.y * 1.5f };
        }
        else
        {
            const float r = half.y * 1.35f;
            DrawPoly(p, 4, r, 0.0f, color);
            DrawPolyLinesEx(p, 4, r, 0.0f, line, BLACK);
            symbolBox = { p.x - r * 0.45f, p.y - r * 0.3f, r * 0.9f, r * 0.6f };
        }

        const UnitClass cls = PrimaryClass(division, data);
        const std::string& id = data.battalions[static_cast<std::size_t>(division.battalions.front())].id;
        DrawUnitSymbol(symbolBox, cls, id, line);

        DrawHpBar({ p.x - half.x, p.y + half.y + 2.0f / zoom }, half.x * 2.0f, 3.0f / zoom, division.hp / division.maxHp);
        if (inCombat)
        {
            DrawCircleV({ p.x + half.x, p.y - half.y }, 3.0f / zoom, Color{ 255, 120, 30, 255 });
        }
        if (retreating)
        {
            DrawTextEx(GetFontDefault(), "R", { p.x - half.x - 8.0f / zoom, p.y - half.y }, 12.0f / zoom, 1.0f / zoom, UiColors::Accent);
        }
    }
}

void DrawWeatherLabels(const World& world, const Camera2D& camera)
{
    const int regions = static_cast<int>(std::sqrt(static_cast<float>(world.weather.size())));
    if (regions <= 0)
    {
        return;
    }
    const float width = MapWidthKm(world.map);
    const float height = MapHeightKm(world.map);
    for (int r = 0; r < regions; ++r)
    {
        for (int c = 0; c < regions; ++c)
        {
            const Weather weather = world.weather[static_cast<std::size_t>(r * regions + c)].weather;
            if (weather == Weather::Clear)
            {
                continue;
            }
            const Vector2 corner = { width * static_cast<float>(c) / static_cast<float>(regions) + 8.0f,
                                     height * static_cast<float>(r) / static_cast<float>(regions) + 8.0f };
            const Vector2 screen = GetWorldToScreen2D(corner, camera);
            TextDraw(WeatherNames[ToIndex(weather)], static_cast<int>(screen.x), static_cast<int>(screen.y), 18, Color{ 20, 30, 60, 220 });
        }
    }
}
