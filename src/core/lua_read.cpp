#include "core/lua_read.hpp"
#include "core/templates.hpp"

#include <lua.hpp>

#include <stdexcept>

void LuaFail(const std::string& message)
{
    throw std::runtime_error(message);
}

float LuaReadFloat(lua_State* L, int table, const char* key, float fallback)
{
    lua_getfield(L, table, key);
    const float value = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : fallback;
    lua_pop(L, 1);
    return value;
}

int LuaReadInt(lua_State* L, int table, const char* key, int fallback)
{
    lua_getfield(L, table, key);
    const int value = lua_isnumber(L, -1) ? static_cast<int>(lua_tointeger(L, -1)) : fallback;
    lua_pop(L, 1);
    return value;
}

bool LuaReadBool(lua_State* L, int table, const char* key, bool fallback)
{
    lua_getfield(L, table, key);
    const bool value = lua_isboolean(L, -1) ? lua_toboolean(L, -1) != 0 : fallback;
    lua_pop(L, 1);
    return value;
}

float LuaReadRequired(lua_State* L, int table, const char* key, const std::string& context)
{
    lua_getfield(L, table, key);
    if (!lua_isnumber(L, -1))
    {
        LuaFail(context + "." + key + " must be a number");
    }
    const float value = static_cast<float>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    return value;
}

std::string LuaReadString(lua_State* L, int table, const char* key, const std::string& fallback)
{
    lua_getfield(L, table, key);
    std::string value = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : fallback;
    lua_pop(L, 1);
    return value;
}

int LuaPushTable(lua_State* L, int table, const char* key)
{
    lua_getfield(L, table, key);
    if (!lua_istable(L, -1))
    {
        LuaFail(std::string("table '") + key + "' is missing");
    }
    return lua_gettop(L);
}

int LuaPushOptionalTable(lua_State* L, int table, const char* key)
{
    lua_getfield(L, table, key);
    if (!lua_istable(L, -1))
    {
        lua_pop(L, 1);
        return 0;
    }
    return lua_gettop(L);
}

void LuaPop(lua_State* L)
{
    lua_pop(L, 1);
}

void LuaRequireTable(lua_State* L, int index, const std::string& context)
{
    if (!lua_istable(L, index))
    {
        LuaFail(context + " must be a table");
    }
}

void LuaForEachItem(lua_State* L, int table, const std::function<void(int)>& fn)
{
    const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, table));
    for (lua_Integer i = 1; i <= count; ++i)
    {
        lua_rawgeti(L, table, i);
        fn(lua_gettop(L));
        lua_pop(L, 1);
    }
}

std::string LuaToString(lua_State* L, int index)
{
    return lua_type(L, index) == LUA_TSTRING ? lua_tostring(L, index) : "";
}

bool LuaIsNumber(lua_State* L, int index)
{
    return lua_isnumber(L, index) != 0;
}

int LuaToInt(lua_State* L, int index)
{
    return static_cast<int>(lua_tointeger(L, index));
}

ResourceAmounts LuaReadResources(lua_State* L, int table, const char* key, const std::string& context)
{
    ResourceAmounts amounts{};
    const int t = LuaPushOptionalTable(L, table, key);
    if (t == 0)
    {
        return amounts;
    }
    lua_pushnil(L);
    while (lua_next(L, t) != 0)
    {
        const int index = lua_type(L, -2) == LUA_TSTRING ? FindName(ResourceNames, lua_tostring(L, -2)) : -1;
        if (index < 0 || !lua_isnumber(L, -1) || lua_tonumber(L, -1) < 0.0)
        {
            lua_pop(L, 2);
            LuaPop(L);
            LuaFail(context + "." + key + ": unknown resource or negative amount");
        }
        amounts[static_cast<std::size_t>(index)] = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
    }
    LuaPop(L);
    return amounts;
}

BattalionDef LuaReadBattalion(lua_State* L, int item, const std::vector<FactoryDef>& factories)
{
    LuaRequireTable(L, item, "battalion entry");
    BattalionDef def;
    def.id = LuaReadString(L, item, "id", "");
    if (def.id.empty())
    {
        LuaFail("battalion without id");
    }
    const std::string context = "battalions." + def.id;
    def.name = LuaReadString(L, item, "name", def.id);
    def.app6 = LuaReadString(L, item, "app6", "");

    const std::string unitClass = LuaReadString(L, item, "class", "");
    const int classIndex = FindName(UnitClassNames, unitClass);
    if (classIndex < 0)
    {
        LuaFail(context + ": unknown class '" + unitClass + "'");
    }
    def.unitClass = static_cast<UnitClass>(classIndex);

    const std::string effect = LuaReadString(L, item, "effect", "none");
    const int effectIndex = FindName(EffectNames, effect);
    if (effectIndex < 0)
    {
        LuaFail(context + ": unknown effect '" + effect + "'");
    }
    def.effect = static_cast<Effect>(effectIndex);

    def.attack = LuaReadRequired(L, item, "attack", context);
    def.defense = LuaReadRequired(L, item, "defense", context);
    def.speed = LuaReadRequired(L, item, "speed", context);
    def.range = LuaReadRequired(L, item, "range", context);
    def.hp = LuaReadRequired(L, item, "hp", context);
    const std::string factory = LuaReadString(L, item, "factory", "");
    def.factory = -1;
    for (std::size_t i = 0; i < factories.size(); ++i)
    {
        if (factories[i].id == factory)
        {
            def.factory = static_cast<int>(i);
        }
    }
    if (def.factory < 0)
    {
        LuaFail(context + ": unknown factory '" + factory + "'");
    }
    def.materials = LuaReadResources(L, item, "materials", context);
    def.manpower = LuaReadFloat(L, item, "manpower", 500.0f);
    if (def.speed <= 0.0f || def.hp <= 0.0f || def.attack < 0.0f || def.defense < 0.0f || def.range < 0.0f || def.manpower < 0.0f)
    {
        LuaFail(context + ": speed and hp must be positive, other stats non-negative");
    }
    return def;
}

std::vector<TemplateDef> LuaReadTemplateList(lua_State* L, int table, const GameData& data, const std::string& context)
{
    std::vector<TemplateDef> result;
    LuaForEachItem(L, table, [&](int item)
    {
        LuaRequireTable(L, item, context + " entry");
        TemplateDef templ;
        templ.name = LuaReadString(L, item, "name", "Division");
        const int list = LuaPushTable(L, item, "battalions");
        LuaForEachItem(L, list, [&](int entry)
        {
            const std::string id = LuaToString(L, entry);
            const int index = BattalionIndex(data, id);
            if (index < 0)
            {
                LuaFail(context + ": unknown battalion '" + id + "'");
            }
            templ.battalions.push_back(index);
        });
        lua_pop(L, 1);

        const std::string problem = ValidateTemplate(templ, data);
        if (!problem.empty())
        {
            LuaFail(context + " '" + templ.name + "': " + problem);
        }
        result.push_back(std::move(templ));
    });
    return result;
}
