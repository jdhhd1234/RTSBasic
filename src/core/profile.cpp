#include "core/profile.hpp"
#include "core/templates.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>

// 파일 형식 (한 줄에 하나):
//   cleared 1
//   language ko
//   template Infantry Div|inf,inf,arty,eng

namespace
{
    bool ParseTemplateLine(const std::string& body, const GameData& data, TemplateDef& out)
    {
        const std::size_t bar = body.find('|');
        if (bar == std::string::npos)
        {
            return false;
        }

        TemplateDef templ;
        templ.name = body.substr(0, bar);
        std::istringstream ids(body.substr(bar + 1));
        std::string id;
        while (std::getline(ids, id, ','))
        {
            const int index = BattalionIndex(data, id);
            if (index < 0)
            {
                return false;
            }
            templ.battalions.push_back(index);
        }

        if (templ.name.empty() || !ValidateTemplate(templ, data).empty())
        {
            return false;
        }
        out = std::move(templ);
        return true;
    }
}

Profile LoadProfile(const std::string& path, const GameData& data)
{
    Profile profile;
    profile.templates = data.defaultTemplates;

    std::ifstream file(path);
    if (!file)
    {
        return profile;
    }

    std::vector<TemplateDef> loaded;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.starts_with("cleared "))
        {
            int value = 0;
            const char* begin = line.data() + 8;
            const auto result = std::from_chars(begin, line.data() + line.size(), value);
            if (result.ec == std::errc())
            {
                profile.clearedStages = std::clamp(value, 0, static_cast<int>(data.stages.size()));
            }
        }
        else if (line.starts_with("language ") && line.size() > 9)
        {
            profile.language = line.substr(9);
        }
        else if (line.starts_with("template "))
        {
            TemplateDef templ;
            if (ParseTemplateLine(line.substr(9), data, templ) && static_cast<int>(loaded.size()) < data.config.maxTemplates)
            {
                loaded.push_back(std::move(templ));
            }
        }
    }

    if (!loaded.empty())
    {
        profile.templates = std::move(loaded);
    }
    return profile;
}

bool SaveProfile(const std::string& path, const Profile& profile, const GameData& data)
{
    std::ofstream file(path, std::ios::trunc);
    if (!file)
    {
        return false;
    }

    file << "cleared " << profile.clearedStages << '\n';
    file << "language " << profile.language << '\n';
    for (const TemplateDef& templ : profile.templates)
    {
        file << "template " << templ.name << '|';
        for (std::size_t i = 0; i < templ.battalions.size(); ++i)
        {
            if (i > 0)
            {
                file << ',';
            }
            file << data.battalions[static_cast<std::size_t>(templ.battalions[i])].id;
        }
        file << '\n';
    }
    return static_cast<bool>(file);
}
