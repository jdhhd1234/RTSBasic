#include "sim/scenario.hpp"
#include "core/lua_loader.hpp"
#include "core/lua_read.hpp"
#include "core/templates.hpp"
#include "sim/geojson_map.hpp"

#include <lua.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
    int ReadDifficulty(lua_State* L, int table, const GameData& data)
    {
        lua_getfield(L, table, "difficulty");
        int result = 1;
        if (lua_type(L, -1) == LUA_TSTRING)
        {
            const std::string name = lua_tostring(L, -1);
            const auto it = std::find_if(data.difficulties.begin(), data.difficulties.end(), [&](const DifficultyDef& d) { return d.name == name; });
            if (it == data.difficulties.end())
            {
                LuaFail("unknown difficulty '" + name + "'");
            }
            result = static_cast<int>(it - data.difficulties.begin());
        }
        else if (lua_isnumber(L, -1))
        {
            result = std::clamp(static_cast<int>(lua_tointeger(L, -1)) - 1, 0, static_cast<int>(data.difficulties.size()) - 1);
        }
        lua_pop(L, 1);
        return result;
    }

    // divisions = 3 (템플릿 순환) 또는 { "템플릿 이름", ... }
    std::vector<int> ReadInitialDivisions(lua_State* L, int table, const std::vector<TemplateDef>& templates, const std::string& context)
    {
        std::vector<int> result;
        lua_getfield(L, table, "divisions");
        const int value = lua_gettop(L);
        if (lua_isnumber(L, value))
        {
            const int count = std::clamp(static_cast<int>(lua_tointeger(L, value)), 0, 200);
            for (int i = 0; i < count && !templates.empty(); ++i)
            {
                result.push_back(static_cast<int>(static_cast<std::size_t>(i) % templates.size()));
            }
        }
        else if (lua_istable(L, value))
        {
            LuaForEachItem(L, value, [&](int item)
            {
                const std::string name = LuaToString(L, item);
                const auto it = std::find_if(templates.begin(), templates.end(), [&](const TemplateDef& t) { return t.name == name; });
                if (it == templates.end())
                {
                    LuaFail(context + ".divisions: unknown template '" + name + "'");
                }
                result.push_back(static_cast<int>(it - templates.begin()));
            });
        }
        lua_pop(L, 1);
        return result;
    }

    void ReadSide(lua_State* L, int item, int faction, const GameData& data, const std::vector<TemplateDef>& playerTemplates, SideSetup& side)
    {
        const std::string context = faction == PlayerFaction ? "sides[1]" : "sides[2]";
        LuaRequireTable(L, item, context);
        side.name = LuaReadString(L, item, "name", faction == PlayerFaction ? "Blue" : "Red");
        side.ai = LuaReadBool(L, item, "ai", faction != PlayerFaction);
        side.difficulty = ReadDifficulty(L, item, data);
        side.incomeMultiplier = std::max(0.0f, LuaReadFloat(L, item, "income", side.ai ? data.difficulties[static_cast<std::size_t>(side.difficulty)].aiIncome : 1.0f));

        const int templates = LuaPushOptionalTable(L, item, "templates");
        if (templates != 0)
        {
            side.templates = LuaReadTemplateList(L, templates, data, context + ".templates");
            LuaPop(L);
        }
        if (side.templates.empty())
        {
            side.templates = faction == PlayerFaction ? playerTemplates : data.aiTemplates;
        }
        if (static_cast<int>(side.templates.size()) > data.config.maxTemplates)
        {
            LuaFail(context + ": max " + std::to_string(data.config.maxTemplates) + " templates");
        }
        side.initialDivisions = ReadInitialDivisions(L, item, side.templates, context);
    }

    void ReadMap(lua_State* L, int root, const std::string& scenarioPath, Scenario& scenario)
    {
        MatchSetup& setup = scenario.setup;
        const GameConfig& c = scenario.data.config;
        setup.mapSeed = c.mapSeed;
        setup.mapCols = c.mapCols;
        setup.mapRows = c.mapRows;
        scenario.mapLabel = "random 1200km";

        const int map = LuaPushOptionalTable(L, root, "map");
        if (map == 0)
        {
            return;
        }
        const std::string source = LuaReadString(L, map, "source", "random");
        if (source == "geojson")
        {
            const std::string relative = LuaReadString(L, map, "path", "");
            const std::filesystem::path file = std::filesystem::path(scenarioPath).parent_path() / relative;
            std::string error;
            if (relative.empty() || !LoadGeoJsonMap(file.string(), scenario.data, setup.map, setup.layers, error))
            {
                LuaFail("map: " + (relative.empty() ? std::string("geojson path is missing") : error));
            }
            if (!error.empty())
            {
                std::clog << error << '\n';
            }
            setup.customMap = true;
            scenario.mapLabel = "geojson " + file.filename().string();
        }
        else if (source == "random")
        {
            setup.mapSeed = LuaReadInt(L, map, "seed", c.mapSeed);
            const float width = LuaReadFloat(L, map, "width_km", c.mapMaxKm);
            const float height = LuaReadFloat(L, map, "height_km", c.mapMaxKm);
            SetGeneratedMapSize(setup, c, width, height);
            scenario.mapLabel = "random " + std::to_string(setup.mapCols) + "x" + std::to_string(setup.mapRows) + " seed " + std::to_string(setup.mapSeed);
        }
        else
        {
            LuaFail("map.source must be 'random' or 'geojson'");
        }
        LuaPop(L);
    }

    void ReadBasePlacements(lua_State* L, int root, MatchSetup& setup)
    {
        const int bases = LuaPushOptionalTable(L, root, "bases");
        if (bases == 0)
        {
            return;
        }
        LuaForEachItem(L, bases, [&](int item)
        {
            LuaRequireTable(L, item, "bases entry");
            BasePlacement placement;
            placement.faction = std::clamp(LuaReadInt(L, item, "side", 1), 1, FactionCount) - 1;
            const std::string type = LuaReadString(L, item, "type", "division");
            const int index = FindName(BaseTypeNames, type);
            if (index < 0)
            {
                LuaFail("bases: unknown type '" + type + "'");
            }
            placement.type = static_cast<BaseType>(index);
            placement.coord = { LuaReadInt(L, item, "col", 0), LuaReadInt(L, item, "row", 0) };
            setup.bases.push_back(placement);
        });
        LuaPop(L);

        for (int f = 0; f < FactionCount; ++f)
        {
            const bool hasHq = std::any_of(setup.bases.begin(), setup.bases.end(), [f](const BasePlacement& b) { return b.faction == f && b.type == BaseType::Hq; });
            if (!hasHq)
            {
                LuaFail("bases: each side needs one hq");
            }
        }
    }
}

bool LoadScenario(const std::string& path, const GameData& base, const std::vector<TemplateDef>& playerTemplates,
                  Scenario& out, std::string& error)
{
    LuaState lua;
    if (!LuaRunFile(lua, path, error))
    {
        return false;
    }
    lua_State* L = lua.Get();
    const int root = lua_gettop(L);

    try
    {
        Scenario scenario;
        scenario.path = path;
        scenario.data = base;
        scenario.name = LuaReadString(L, root, "name", std::filesystem::path(path).stem().string());
        scenario.description = LuaReadString(L, root, "description", "");

        // 커스텀 대대: 기본 20종 뒤에 붙인다 (프로필 템플릿 인덱스가 그대로 유효).
        const int battalions = LuaPushOptionalTable(L, root, "battalions");
        if (battalions != 0)
        {
            LuaForEachItem(L, battalions, [&](int item)
            {
                BattalionDef def = LuaReadBattalion(L, item, scenario.data.factories);
                if (BattalionIndex(scenario.data, def.id) >= 0)
                {
                    LuaFail("battalion id '" + def.id + "' already exists");
                }
                scenario.customBattalions.push_back(def.name);
                scenario.data.battalions.push_back(std::move(def));
            });
            LuaPop(L);
        }

        MatchSetup& setup = scenario.setup;
        setup.title = "Sandbox: " + scenario.name;
        setup.stage = -1;
        setup.matchSeconds = std::max(60.0f, LuaReadFloat(L, root, "match_seconds", base.config.matchSeconds));
        scenario.data.config.matchSeconds = setup.matchSeconds;

        ReadMap(L, root, path, scenario);
        ReadBasePlacements(L, root, setup);

        const int sides = LuaPushOptionalTable(L, root, "sides");
        for (int f = 0; f < FactionCount; ++f)
        {
            SideSetup& side = setup.sides[static_cast<std::size_t>(f)];
            if (sides != 0)
            {
                lua_rawgeti(L, sides, f + 1);
                if (lua_istable(L, -1))
                {
                    ReadSide(L, lua_gettop(L), f, scenario.data, playerTemplates, side);
                }
                lua_pop(L, 1);
            }
            if (side.templates.empty())   // sides 항목이 없으면 기본값
            {
                side.name = f == PlayerFaction ? "Blue" : "Red";
                side.ai = f != PlayerFaction;
                side.incomeMultiplier = 1.0f;
                side.templates = f == PlayerFaction ? playerTemplates : base.aiTemplates;
                for (int i = 0; i < 3 && !side.templates.empty(); ++i)
                {
                    side.initialDivisions.push_back(static_cast<int>(static_cast<std::size_t>(i) % side.templates.size()));
                }
            }
        }
        if (sides != 0)
        {
            LuaPop(L);
        }

        out = std::move(scenario);
        return true;
    }
    catch (const std::runtime_error& e)
    {
        error = path + ": " + e.what();
        return false;
    }
}
