#include "app/screens.hpp"
#include "app/ui.hpp"
#include "core/templates.hpp"

#include <algorithm>
#include <string>

namespace
{
    void EditName(std::string& name)
    {
        int key = GetCharPressed();
        while (key > 0)
        {
            if (key >= 32 && key <= 126 && key != '|' && name.size() < 24)
            {
                name.push_back(static_cast<char>(key));
            }
            key = GetCharPressed();
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !name.empty())
        {
            name.pop_back();
        }
    }

    bool CanAdd(const TemplateDef& templ, int battalion, const GameConfig& config)
    {
        if (static_cast<int>(templ.battalions.size()) >= config.maxBattalions)
        {
            return false;
        }
        return std::count(templ.battalions.begin(), templ.battalions.end(), battalion) < config.maxSameBattalion;
    }
}

void DrawTemplates(AppState& app, const GameData& data)
{
    TemplateEditor& editor = app.editor;
    const GameConfig& config = data.config;
    editor.current = std::clamp(editor.current, 0, std::max(0, static_cast<int>(editor.templates.size()) - 1));

    TextDraw("Division Templates", 40, 30, 36, RAYWHITE);
    TextDraw("Pick battalions before the battle. 1-4 battalions, max 2 of the same.", 40, 74, 18, LIGHTGRAY);

    // 왼쪽: 템플릿 목록
    float y = 120.0f;
    for (int i = 0; i < static_cast<int>(editor.templates.size()); ++i)
    {
        const TemplateDef& templ = editor.templates[static_cast<std::size_t>(i)];
        const std::string label = templ.name;
        if (UiButton({ 40.0f, y, 300.0f, 40.0f }, label.c_str(), true, editor.current == i))
        {
            editor.current = i;
        }
        y += 46.0f;
    }
    if (UiButton({ 40.0f, y + 10.0f, 146.0f, 40.0f }, "New", static_cast<int>(editor.templates.size()) < config.maxTemplates))
    {
        editor.templates.push_back({ "New Division", {} });
        editor.current = static_cast<int>(editor.templates.size()) - 1;
    }
    if (UiButton({ 194.0f, y + 10.0f, 146.0f, 40.0f }, "Delete", editor.templates.size() > 1))
    {
        editor.templates.erase(editor.templates.begin() + editor.current);
        editor.current = std::max(0, editor.current - 1);
    }

    if (editor.templates.empty())
    {
        return;
    }
    TemplateDef& templ = editor.templates[static_cast<std::size_t>(editor.current)];

    // 가운데: 이름, 편성, 요약
    const float mx = 380.0f;
    EditName(templ.name);
    UiPanel({ mx, 120.0f, 380.0f, 40.0f });
    const std::string nameLine = Translate("Name:") + " " + templ.name + "_";   // 입력 중인 이름은 번역하지 않는다
    TextDrawRaw(nameLine.c_str(), static_cast<int>(mx) + 10, 130, 20, RAYWHITE);

    y = 176.0f;
    TextDraw("Battalions (click to remove)", static_cast<int>(mx), static_cast<int>(y), 18, LIGHTGRAY);
    y += 28.0f;
    for (int slot = 0; slot < config.maxBattalions; ++slot)
    {
        const Rectangle rect = { mx, y, 380.0f, 40.0f };
        if (slot < static_cast<int>(templ.battalions.size()))
        {
            const BattalionDef& def = data.battalions[static_cast<std::size_t>(templ.battalions[static_cast<std::size_t>(slot)])];
            if (UiButton(rect, def.name.c_str()))
            {
                templ.battalions.erase(templ.battalions.begin() + slot);
            }
        }
        else
        {
            UiButton(rect, "- empty -", false);
        }
        y += 46.0f;
    }

    const TemplateSummary summary = SummarizeTemplate(templ, data);
    y += 10.0f;
    const TemplateNeeds needs = GetTemplateNeeds(templ, data);
    std::string factories;
    for (const int type : needs.factories)
    {
        factories += (factories.empty() ? "" : ", ") + data.factories[static_cast<std::size_t>(type)].name;
    }
    UiPanel({ mx, y, 400.0f, 200.0f });
    const std::string lines[] = {
        "Manpower: " + std::to_string(static_cast<int>(needs.manpower)),
        "Materials: " + ResourceText(needs.materials),
        "Factories: " + (factories.empty() ? std::string("-") : factories),
        "Attack sum: " + std::to_string(static_cast<int>(summary.attackSum)),
        "Defense avg: " + std::to_string(summary.defenseAvg).substr(0, 5),
        "Speed: " + std::to_string(summary.speed).substr(0, 4) + " km/s   Range: " + std::to_string(static_cast<int>(summary.maxRange)) + " km",
        "HP: " + std::to_string(static_cast<int>(summary.hp)),
    };
    float ly = y + 10.0f;
    for (const std::string& line : lines)
    {
        TextDraw(line.c_str(), static_cast<int>(mx) + 10, static_cast<int>(ly), 18, RAYWHITE);
        ly += 23.0f;
    }

    // 오른쪽: 대대 20종
    const float rx = 800.0f;
    TextDraw("Add battalion", static_cast<int>(rx), 120, 18, LIGHTGRAY);
    const int columns = 4;
    const float bw = 180.0f;
    const float bh = 44.0f;
    int hovered = -1;
    for (int i = 0; i < static_cast<int>(data.battalions.size()); ++i)
    {
        const BattalionDef& def = data.battalions[static_cast<std::size_t>(i)];
        const Rectangle rect = { rx + static_cast<float>(i % columns) * (bw + 8.0f), 148.0f + static_cast<float>(i / columns) * (bh + 8.0f), bw, bh };
        const std::string label = def.name;
        if (CheckCollisionPointRec(GetMousePosition(), rect))
        {
            hovered = i;
        }
        if (UiButton(rect, label.c_str(), CanAdd(templ, i, config)))
        {
            templ.battalions.push_back(i);
        }
    }

    if (hovered >= 0)
    {
        const BattalionDef& def = data.battalions[static_cast<std::size_t>(hovered)];
        const std::string info = def.name + " (" + UnitClassNames[ToIndex(def.unitClass)] + ")  ATK " + std::to_string(static_cast<int>(def.attack))
            + "  DEF " + std::to_string(static_cast<int>(def.defense)) + "  SPD " + std::to_string(def.speed).substr(0, 3)
            + "  RNG " + std::to_string(static_cast<int>(def.range)) + "  HP " + std::to_string(static_cast<int>(def.hp))
            + (def.effect != Effect::None ? std::string("  effect: ") + EffectNames[ToIndex(def.effect)] : std::string());
        TextDraw(info.c_str(), static_cast<int>(rx), 420, 18, UiColors::Accent);
        const std::string cost = "factory: " + data.factories[static_cast<std::size_t>(def.factory)].name + "   manpower "
            + std::to_string(static_cast<int>(def.manpower)) + "   " + ResourceText(def.materials);
        TextDraw(cost.c_str(), static_cast<int>(rx), 446, 18, UiColors::Accent);
    }

    // 하단: 검증, 저장
    std::string error;
    for (const TemplateDef& t : editor.templates)
    {
        const std::string problem = ValidateTemplate(t, data);
        if (!problem.empty())
        {
            error = t.name + ": " + problem;
            break;
        }
        if (t.name.empty())
        {
            error = "template name is empty";
            break;
        }
    }

    const float by = static_cast<float>(GetScreenHeight()) - 80.0f;
    if (!error.empty())
    {
        TextDraw(error.c_str(), 40, static_cast<int>(by) - 34, 20, UiColors::Ai);
    }
    if (UiButton({ 40.0f, by, 220.0f, 48.0f }, "Save & Back", error.empty()))
    {
        app.profile.templates = editor.templates;
        SaveProfile(app.profilePath, app.profile, data);
        app.screen = Screen::Menu;
    }
    if (UiButton({ 270.0f, by, 220.0f, 48.0f }, "Cancel"))
    {
        app.screen = Screen::Menu;
    }
}
