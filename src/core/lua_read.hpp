#pragma once

#include "core/game_data.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>

struct lua_State;

// Lua 테이블 읽기 헬퍼. 잘못된 데이터는 LuaFail로 std::runtime_error를 던진다.
// table 인자는 항상 절대 스택 인덱스다.

[[noreturn]] void LuaFail(const std::string& message);

float LuaReadFloat(lua_State* L, int table, const char* key, float fallback);
int LuaReadInt(lua_State* L, int table, const char* key, int fallback);
bool LuaReadBool(lua_State* L, int table, const char* key, bool fallback);
float LuaReadRequired(lua_State* L, int table, const char* key, const std::string& context);
std::string LuaReadString(lua_State* L, int table, const char* key, const std::string& fallback);

// table[key]를 스택에 올리고 절대 인덱스를 돌려준다. 테이블이 아니면 실패. 호출자가 pop한다.
int LuaPushTable(lua_State* L, int table, const char* key);

// table[key]가 테이블이면 올리고 인덱스를, 아니면 아무것도 남기지 않고 0을 돌려준다.
int LuaPushOptionalTable(lua_State* L, int table, const char* key);

void LuaPop(lua_State* L);

void LuaRequireTable(lua_State* L, int index, const std::string& context);

// 배열 테이블의 각 항목을 스택에 올려 fn(항목 인덱스)를 부른다.
void LuaForEachItem(lua_State* L, int table, const std::function<void(int)>& fn);

// 스택 값이 문자열이면 그 값, 아니면 빈 문자열.
std::string LuaToString(lua_State* L, int index);

bool LuaIsNumber(lua_State* L, int index);
int LuaToInt(lua_State* L, int index);

// factory는 factories의 id여야 한다. materials/manpower는 생략 가능.
BattalionDef LuaReadBattalion(lua_State* L, int item, const std::vector<FactoryDef>& factories);

// table[key] = { oil = 10, iron = 5, ... } 를 읽는다. 없으면 0. 모르는 자원 이름이나 음수는 실패.
ResourceAmounts LuaReadResources(lua_State* L, int table, const char* key, const std::string& context);

// {name=..., battalions={"inf",...}} 목록. 대대 id는 data에서 찾는다.
std::vector<TemplateDef> LuaReadTemplateList(lua_State* L, int table, const GameData& data, const std::string& context);

template <std::size_t N>
int FindName(const std::array<const char*, N>& names, const std::string& value)
{
    for (std::size_t i = 0; i < N; ++i)
    {
        if (value == names[i])
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}
