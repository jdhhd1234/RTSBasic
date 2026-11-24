#include "sim/match_script.hpp"
#include "sim/components.hpp"
#include "sim/map.hpp"
#include "sim/simulation.hpp"
#include "sim/world.hpp"

#include <lua.hpp>

#include <cmath>
#include <utility>

namespace
{
    constexpr const char* OutputKey = "minrts.output";
    constexpr int InstructionLimit = 10'000'000;   // 한 번 호출에 이보다 오래 돌면 무한 루프로 보고 끊는다

    void SetNumber(lua_State* L, const char* key, double value)
    {
        lua_pushnumber(L, std::isfinite(value) ? value : 0.0);
        lua_setfield(L, -2, key);
    }

    void SetInteger(lua_State* L, const char* key, long long value)
    {
        lua_pushinteger(L, value);
        lua_setfield(L, -2, key);
    }

    void SetBool(lua_State* L, const char* key, bool value)
    {
        lua_pushboolean(L, value ? 1 : 0);
        lua_setfield(L, -2, key);
    }

    void SetString(lua_State* L, const char* key, const std::string& value)
    {
        lua_pushlstring(L, value.data(), value.size());
        lua_setfield(L, -2, key);
    }

    int ReadOnlyError(lua_State* L)
    {
        return luaL_error(L, "game is read-only");
    }

    // 프록시용 next: 원본 표(1번 인자)를 그대로 순회한다.
    int RawNext(lua_State* L)
    {
        lua_settop(L, 2);
        if (lua_next(L, 1) != 0)
        {
            return 2;
        }
        lua_pushnil(L);
        return 1;
    }

    int ProxyPairs(lua_State* L)
    {
        lua_pushcfunction(L, RawNext);
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_pushnil(L);
        return 3;
    }

    int ProxyLen(lua_State* L)
    {
        lua_pushinteger(L, static_cast<lua_Integer>(lua_rawlen(L, lua_upvalueindex(1))));
        return 1;
    }

    // 스택 맨 위 표를 읽기 전용 프록시로 바꾼다. 안쪽 표도 모두.
    void Freeze(lua_State* L)
    {
        const int raw = lua_gettop(L);
        lua_pushnil(L);
        while (lua_next(L, raw) != 0)
        {
            if (lua_istable(L, -1))
            {
                Freeze(L);                 // key, proxy
                lua_pushvalue(L, -2);      // key, proxy, key
                lua_insert(L, -2);         // key, key, proxy
                lua_rawset(L, raw);        // key (기존 키 값만 바꾸므로 순회 중에도 안전)
            }
            else
            {
                lua_pop(L, 1);
            }
        }

        lua_newtable(L);                   // proxy
        lua_newtable(L);                   // metatable
        lua_pushvalue(L, raw);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, ReadOnlyError);
        lua_setfield(L, -2, "__newindex");
        lua_pushvalue(L, raw);
        lua_pushcclosure(L, ProxyLen, 1);
        lua_setfield(L, -2, "__len");
        lua_pushvalue(L, raw);
        lua_pushcclosure(L, ProxyPairs, 1);
        lua_setfield(L, -2, "__pairs");
        lua_pushstring(L, "read-only");
        lua_setfield(L, -2, "__metatable");   // getmetatable/setmetatable로 못 뜯게
        lua_setmetatable(L, -2);
        lua_replace(L, raw);
        lua_settop(L, raw);
    }

    void PushFaction(lua_State* L, const World& world, int id)
    {
        const FactionState& f = world.factions[static_cast<std::size_t>(id)];
        lua_newtable(L);
        SetInteger(L, "id", id);
        SetString(L, "name", f.name);
        SetBool(L, "ai", f.aiControlled);
        SetNumber(L, "manpower", f.manpower);
        SetNumber(L, "munitions", f.munitions);
        SetNumber(L, "quality", f.quality);
        SetNumber(L, "draft_pressure", f.draftPressure);
        SetNumber(L, "upkeep", f.upkeep);
        SetBool(L, "fuel_shortage", f.fuelShortage);
        SetInteger(L, "owned_hexes", f.ownedHexes);
        SetInteger(L, "factories", static_cast<long long>(f.factories.size()));
        SetInteger(L, "queue", static_cast<long long>(f.queue.size()));
        SetString(L, "posture", AiPostureNames[static_cast<std::size_t>(f.blackboard.posture)]);
        lua_newtable(L);
        for (std::size_t r = 0; r < ResourceNames.size(); ++r)
        {
            SetNumber(L, ResourceNames[r], f.stock[r]);
        }
        lua_setfield(L, -2, "stock");
    }

    void PushDivisions(lua_State* L, const World& world, const GameData& data)
    {
        lua_newtable(L);
        lua_Integer n = 0;
        for (const auto [entity, division, position, faction] : world.registry.view<Division, Position, Faction>().each())
        {
            lua_newtable(L);
            SetInteger(L, "id", static_cast<long long>(entt::to_integral(entity)));
            SetString(L, "name", division.name);
            SetInteger(L, "faction", faction.id);
            SetNumber(L, "x", position.value.x);
            SetNumber(L, "y", position.value.y);
            SetNumber(L, "hp", division.hp);
            SetNumber(L, "max_hp", division.maxHp);
            SetNumber(L, "frontage_km", division.frontageKm);
            SetBool(L, "retreating", world.registry.all_of<Retreating>(entity));
            SetBool(L, "in_combat", world.time - division.lastCombatTime < 10.0f);
            lua_newtable(L);
            for (std::size_t i = 0; i < division.battalions.size(); ++i)
            {
                lua_newtable(L);
                SetString(L, "type", data.battalions[static_cast<std::size_t>(division.battalions[i])].id);
                if (i < division.units.size())
                {
                    const BattalionUnit& unit = division.units[i];
                    SetString(L, "task", BattalionTaskNames[static_cast<std::size_t>(unit.task)]);
                    SetNumber(L, "x", unit.position.x);
                    SetNumber(L, "y", unit.position.y);
                }
                lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
            }
            lua_setfield(L, -2, "battalions");
            lua_rawseti(L, -2, ++n);
        }
    }

    void PushBases(lua_State* L, const World& world)
    {
        lua_newtable(L);
        lua_Integer n = 0;
        for (const auto [entity, base, position, faction] : world.registry.view<Base, Position, Faction>().each())
        {
            lua_newtable(L);
            SetString(L, "name", base.name);
            SetString(L, "type", BaseTypeNames[ToIndex(base.type)]);
            SetInteger(L, "faction", faction.id);
            SetInteger(L, "hex", base.hex);
            SetNumber(L, "x", position.value.x);
            SetNumber(L, "y", position.value.y);
            SetNumber(L, "hp", base.hp);
            SetNumber(L, "max_hp", base.maxHp);
            lua_rawseti(L, -2, ++n);
        }
    }

    // game 표를 만들어 읽기 전용으로 올린다. 값은 전부 복사본이다.
    void PushGame(lua_State* L, const World& world, const GameData& data)
    {
        lua_newtable(L);
        SetNumber(L, "time", world.time);
        SetNumber(L, "match_seconds", world.matchSeconds);
        SetBool(L, "unlimited", world.matchSeconds <= 0.0f);
        SetBool(L, "over", world.over);
        SetInteger(L, "winner", world.winner);
        SetString(L, "title", world.title);

        lua_newtable(L);
        for (int id = 0; id < FactionCount; ++id)
        {
            PushFaction(L, world, id);
            lua_rawseti(L, -2, id + 1);   // Lua 배열은 1부터: factions[1] = 청군(id 0), factions[2] = 적군(id 1)
        }
        lua_setfield(L, -2, "factions");
        PushDivisions(L, world, data);
        lua_setfield(L, -2, "divisions");
        PushBases(L, world);
        lua_setfield(L, -2, "bases");

        lua_newtable(L);
        SetInteger(L, "cols", world.map.cols);
        SetInteger(L, "rows", world.map.rows);
        SetNumber(L, "width_km", MapWidthKm(world.map));
        SetNumber(L, "height_km", MapHeightKm(world.map));
        SetNumber(L, "hex_km", world.map.hexDistance);
        lua_setfield(L, -2, "map");

        Freeze(L);
    }

    int Print(lua_State* L)
    {
        lua_getfield(L, LUA_REGISTRYINDEX, OutputKey);
        auto* output = static_cast<std::vector<std::string>*>(lua_touserdata(L, -1));
        lua_pop(L, 1);
        std::string line;
        const int count = lua_gettop(L);
        for (int i = 1; i <= count; ++i)
        {
            std::size_t length = 0;
            const char* text = luaL_tolstring(L, i, &length);
            line.append(i > 1 ? "\t" : "").append(text, length);
            lua_pop(L, 1);
        }
        if (output != nullptr)
        {
            output->push_back(std::move(line));
        }
        return 0;
    }

    void TooLong(lua_State* L, lua_Debug*)
    {
        luaL_error(L, "script took too long (%d instructions)", InstructionLimit);
    }

    // 파일·OS 접근 없는 표준 라이브러리만 연다.
    void OpenSafeLibs(lua_State* L)
    {
        luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
        luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
        luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
        luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
        lua_pop(L, 4);
        for (const char* name : { "dofile", "loadfile", "rawset" })
        {
            lua_pushnil(L);
            lua_setglobal(L, name);
        }
        lua_pushcfunction(L, Print);
        lua_setglobal(L, "print");
    }
}

MatchScript::~MatchScript()
{
    Unload();
}

MatchScript::MatchScript(MatchScript&& other) noexcept
    : state(std::exchange(other.state, nullptr)), interval(other.interval), timer(other.timer)
{
}

MatchScript& MatchScript::operator=(MatchScript&& other) noexcept
{
    if (this != &other)
    {
        Unload();
        state = std::exchange(other.state, nullptr);
        interval = other.interval;
        timer = other.timer;
    }
    return *this;
}

bool MatchScript::Load(const std::string& path, std::string& error)
{
    Unload();
    state = luaL_newstate();
    if (state == nullptr)
    {
        error = "cannot create Lua state";
        return false;
    }
    OpenSafeLibs(state);
    if (luaL_loadfile(state, path.c_str()) != LUA_OK || lua_pcall(state, 0, 0, 0) != LUA_OK)
    {
        error = lua_tostring(state, -1) != nullptr ? lua_tostring(state, -1) : "unknown Lua error";
        Unload();
        return false;
    }
    lua_getglobal(state, "interval");
    const double seconds = lua_isnumber(state, -1) ? lua_tonumber(state, -1) : 1.0;
    lua_pop(state, 1);
    interval = std::isfinite(seconds) && seconds >= 0.1 ? static_cast<float>(seconds) : 1.0f;
    timer = interval;
    return true;
}

void MatchScript::Unload()
{
    if (state != nullptr)
    {
        lua_close(state);
        state = nullptr;
    }
}

bool MatchScript::Loaded() const
{
    return state != nullptr;
}

bool MatchScript::Due(float dt)
{
    timer -= dt;
    if (timer > 0.0f)
    {
        return false;
    }
    timer += interval;
    return true;
}

bool MatchScript::Call(const char* name, const World& world, const GameData& data, std::vector<std::string>& output, std::string& error)
{
    if (state == nullptr)
    {
        return true;
    }
    lua_getglobal(state, name);
    if (!lua_isfunction(state, -1))
    {
        lua_pop(state, 1);
        return true;
    }
    PushGame(state, world, data);
    lua_pushlightuserdata(state, &output);
    lua_setfield(state, LUA_REGISTRYINDEX, OutputKey);
    lua_sethook(state, TooLong, LUA_MASKCOUNT, InstructionLimit);
    const bool ok = lua_pcall(state, 1, 0, 0) == LUA_OK;
    lua_sethook(state, nullptr, 0, 0);
    lua_pushnil(state);
    lua_setfield(state, LUA_REGISTRYINDEX, OutputKey);
    if (!ok)
    {
        error = lua_tostring(state, -1) != nullptr ? lua_tostring(state, -1) : "unknown Lua error";
        lua_pop(state, 1);
    }
    return ok;
}

namespace
{
    void RunScript(World& world, const GameData& data, const char* name)
    {
        std::vector<std::string> output;
        std::string error;
        const bool ok = world.script.Call(name, world, data, output, error);
        for (std::string& line : output)
        {
            PushEvent(world, "[script] " + line, EventKind::Info);
        }
        if (!ok)
        {
            world.script.Unload();
            PushEvent(world, "Script error: " + error, EventKind::Bad);
        }
    }
}

void StartMatchScript(World& world, const GameData& data, const std::string& path)
{
    world.script.Unload();
    if (path.empty())
    {
        return;
    }
    std::string error;
    if (!world.script.Load(path, error))
    {
        PushEvent(world, "Script error: " + error, EventKind::Bad);
        return;
    }
    RunScript(world, data, "on_start");
}

void UpdateMatchScript(World& world, const GameData& data, float dt)
{
    if (world.script.Loaded() && world.script.Due(dt))
    {
        RunScript(world, data, "on_tick");
    }
}
