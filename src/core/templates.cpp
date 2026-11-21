#include "core/templates.hpp"

#include <algorithm>

std::string ValidateTemplate(const TemplateDef& templ, const GameData& data)
{
    const GameConfig& c = data.config;
    if (templ.battalions.empty())
    {
        return "needs at least 1 battalion";
    }
    if (static_cast<int>(templ.battalions.size()) > c.maxBattalions)
    {
        return "max " + std::to_string(c.maxBattalions) + " battalions";
    }

    std::vector<int> counts(data.battalions.size(), 0);
    for (const int b : templ.battalions)
    {
        if (b < 0 || b >= static_cast<int>(data.battalions.size()))
        {
            return "unknown battalion";
        }
        if (++counts[static_cast<std::size_t>(b)] > c.maxSameBattalion)
        {
            return "max " + std::to_string(c.maxSameBattalion) + " of the same battalion";
        }
    }
    return {};
}

TemplateSummary SummarizeTemplate(const TemplateDef& templ, const GameData& data)
{
    TemplateSummary s;
    if (templ.battalions.empty())
    {
        return s;
    }

    float defenseSum = 0.0f;
    s.speed = data.battalions[static_cast<std::size_t>(templ.battalions.front())].speed;
    for (const int b : templ.battalions)
    {
        const BattalionDef& def = data.battalions[static_cast<std::size_t>(b)];
        s.attackSum += def.attack;
        defenseSum += def.defense;
        s.speed = std::min(s.speed, def.speed);
        s.maxRange = std::max(s.maxRange, def.range);
        s.hp += def.hp;
    }
    s.defenseAvg = defenseSum / static_cast<float>(templ.battalions.size());
    return s;
}

TemplateNeeds GetTemplateNeeds(const TemplateDef& templ, const GameData& data)
{
    TemplateNeeds needs;
    for (const int b : templ.battalions)
    {
        const BattalionDef& def = data.battalions[static_cast<std::size_t>(b)];
        for (std::size_t r = 0; r < ResourceCount; ++r)
        {
            needs.materials[r] += def.materials[r];
        }
        needs.manpower += def.manpower;
        needs.factories.push_back(def.factory);
    }
    std::sort(needs.factories.begin(), needs.factories.end());
    needs.factories.erase(std::unique(needs.factories.begin(), needs.factories.end()), needs.factories.end());
    return needs;
}

std::string ResourceText(const ResourceAmounts& amounts)
{
    std::string text;
    for (std::size_t r = 0; r < ResourceCount; ++r)
    {
        if (amounts[r] > 0.0f)
        {
            text += (text.empty() ? "" : ", ") + std::string(ResourceNames[r]) + " " + std::to_string(static_cast<int>(amounts[r] + 0.5f));
        }
    }
    return text.empty() ? "-" : text;
}

int BattalionIndex(const GameData& data, const std::string& id)
{
    for (std::size_t i = 0; i < data.battalions.size(); ++i)
    {
        if (data.battalions[i].id == id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}
