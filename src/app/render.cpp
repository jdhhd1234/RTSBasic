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
    // 사단의 대표 병과: 지원이 아닌 첫 대대.
    const BattalionDef* PrimaryBattalion(const Division& division, const GameData& data)
    {
        const BattalionDef* first = nullptr;
        for (const int b : division.battalions)
        {
            const BattalionDef& def = data.battalions[static_cast<std::size_t>(b)];
            if (def.unitClass != UnitClass::Support)
            {
                return &def;
            }
            if (first == nullptr)
            {
                first = &def;
            }
        }
        return first;
    }

    void DrawCenteredText(const char* text, Vector2 center, float size, Color color)
    {
        const Font font = GetFontDefault();
        const float spacing = size * 0.1f;
        const Vector2 measure = MeasureTextEx(font, text, size, spacing);
        DrawTextEx(font, text, { center.x - measure.x / 2.0f, center.y - measure.y / 2.0f }, size, spacing, color);
    }

    constexpr std::array<const char*, ResourceCount> ResourceShort = { "Oil", "Fe", "Ni", "Qz", "RE", "Mn" };

    // 광산: 자원 색 원 + 약자. 공장과 끊긴 광산은 흐리게.
    void DrawMines(const World& world, float zoom, float detail)
    {
        const float radius = std::lerp(3.5f, 6.0f, detail) / zoom;
        for (const Mine& mine : world.mines)
        {
            const Vector2 p = ToRay(HexCenter(IndexToHex(world.map, mine.hex), world.map.hexDistance));
            const int owner = world.map.hexes[static_cast<std::size_t>(mine.hex)].owner;
            const float alpha = mine.connected ? 1.0f : 0.45f;
            DrawCircleV(p, radius, Fade(ResourceColor(mine.resource), alpha));
            DrawCircleLinesV(p, radius, owner >= 0 ? FactionColor(owner) : BLACK);
            if (detail > 0.0f)
            {
                const char* label = ResourceShort[ToIndex(mine.resource)];
                DrawTextEx(GetFontDefault(), label, { p.x + radius * 1.2f, p.y - radius }, 10.0f / zoom, 1.0f / zoom,
                           Fade(BLACK, alpha * detail));
            }
        }
    }
}

// shortcut: APP-6 선·도형 근사, 정식 기호 에셋 도입 시 교체.
void DrawUnitSymbol(Rectangle box, UnitClass cls, const std::string& fallbackId, float thick, float alpha)
{
    const Vector2 center = { box.x + box.width / 2.0f, box.y + box.height / 2.0f };
    const Color ink = Fade(BLACK, alpha);
    const auto cross = [&]()
    {
        DrawLineEx({ box.x, box.y }, { box.x + box.width, box.y + box.height }, thick, ink);
        DrawLineEx({ box.x, box.y + box.height }, { box.x + box.width, box.y }, thick, ink);
    };
    const auto oval = [&]()
    {
        constexpr int sides = 16;
        const float rx = box.width * 0.36f;
        const float ry = box.height * 0.30f;
        Vector2 last = { center.x + rx, center.y };
        for (int i = 1; i <= sides; ++i)
        {
            const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / sides;
            const Vector2 next = { center.x + rx * std::cos(angle), center.y + ry * std::sin(angle) };
            DrawLineEx(last, next, thick, ink);
            last = next;
        }
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
        const float width = MeasureTextEx(GetFontDefault(), label.c_str(), size, size * 0.1f).x;
        DrawCenteredText(label.c_str(), center, width > box.width ? size * box.width / width : size, ink);
        break;
    }
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

void DrawUnitHpBar(Vector2 topLeft, float width, float height, float hp, float maxHp, float alpha)
{
    const float ratio = maxHp > 0.0f ? std::clamp(hp / maxHp, 0.0f, 1.0f) : 0.0f;
    DrawRectangleV(topLeft, { width, height }, Fade(Color{ 30, 30, 30, 255 }, 0.86f * alpha));
    const Color fill = ratio > 0.6f ? Color{ 80, 200, 90, 255 } : (ratio > 0.3f ? Color{ 230, 190, 60, 255 } : Color{ 220, 70, 60, 255 });
    DrawRectangleV(topLeft, { width * ratio, height }, Fade(fill, alpha));
}

Rectangle DrawUnitFrame(Vector2 p, Vector2 half, int faction, bool selected, const char* echelon, float zoom, float alpha)
{
    const Color fill = Fade(FactionColor(faction), alpha);
    const Color ink = Fade(selected ? UiColors::Accent : BLACK, alpha);
    const float line = std::min((selected ? 2.5f : 1.5f) / zoom, half.y * 0.45f);
    const float labelSize = std::min(10.0f / zoom, half.y * 1.35f);
    const bool friendly = faction == PlayerFaction;
    const float radius = half.y * 1.35f;
    DrawCenteredText(echelon, { p.x, p.y - (friendly ? half.y : radius) - labelSize * 0.65f }, labelSize, Fade(BLACK, alpha));
    if (friendly)
    {
        const Rectangle rect = { p.x - half.x, p.y - half.y, half.x * 2.0f, half.y * 2.0f };
        DrawRectangleRec(rect, fill);
        DrawRectangleLinesEx(rect, line, ink);
        return { p.x - half.x * 0.75f, p.y - half.y * 0.75f, half.x * 1.5f, half.y * 1.5f };
    }

    DrawPoly(p, 4, radius, 0.0f, fill);
    DrawPolyLinesEx(p, 4, radius, 0.0f, line, ink);
    return { p.x - radius * 0.45f, p.y - radius * 0.30f, radius * 0.9f, radius * 0.6f };
}

void DrawMapSites(const World& world, float zoom, float detail)
{
    DrawMines(world, zoom, detail);

    for (const auto [entity, base, position, faction] : world.registry.view<Base, Position, Faction>().each())
    {
        const float half = (base.type == BaseType::Hq ? 9.0f + detail * 3.0f :
                           (base.type == BaseType::Corps ? 7.5f + detail * 2.5f : 6.0f + detail * 2.0f)) / zoom;
        const Vector2 p = ToRay(position.value);
        const Rectangle rect = { p.x - half, p.y - half, half * 2.0f, half * 2.0f };
        DrawRectangleRec(rect, FactionColor(faction.id));
        DrawRectangleLinesEx(rect, std::lerp(1.25f, 2.25f, detail) / zoom, BLACK);
        const char* label = base.type == BaseType::Hq ? "HQ" : (base.type == BaseType::Corps ? "XXX" : "XX");
        DrawCenteredText(label, p, half * 0.9f, BLACK);

        if (detail <= 0.0f)
        {
            continue;
        }
        DrawUnitHpBar({ rect.x, rect.y + rect.height + 2.0f / zoom }, rect.width, 3.0f / zoom, base.hp, base.maxHp, detail);
        int factories = 0;
        for (const FactionState& state : world.factions)
        {
            factories += static_cast<int>(std::count_if(state.factories.begin(), state.factories.end(), [&](const FactoryState& f) { return f.hex == base.hex; }));
        }
        if (factories > 0)
        {
            const std::string badge = "F" + std::to_string(factories);
            DrawTextEx(GetFontDefault(), badge.c_str(), { rect.x, rect.y + rect.height + 6.0f / zoom }, 11.0f / zoom, 1.0f / zoom,
                       Fade(BLACK, detail));
        }
    }
}

void DrawWorldUnits(const World& world, const GameData& data, float zoom, const std::vector<entt::entity>& selected, float alpha)
{
    const Vector2 half = CounterHalfSize(zoom);
    const auto divisions = world.registry.view<Division, Position, Faction>();
    for (const bool selectedPass : { false, true })
    {
        for (const auto [entity, division, position, faction] : divisions.each())
        {
            const bool isSelected = std::find(selected.begin(), selected.end(), entity) != selected.end();
            if (isSelected != selectedPass)
            {
                continue;
            }
            const Vector2 p = ToRay(position.value);
            const Rectangle symbol = DrawUnitFrame(p, half, faction.id, isSelected, "XX", zoom, alpha);
            const BattalionDef* def = PrimaryBattalion(division, data);
            if (def != nullptr)
            {
                DrawUnitSymbol(symbol, def->unitClass, def->id, 1.5f / zoom, alpha);
            }
            DrawUnitHpBar({ p.x - half.x, p.y + half.y + 3.0f / zoom }, half.x * 2.0f, 3.0f / zoom,
                          division.hp, division.maxHp, alpha);
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
