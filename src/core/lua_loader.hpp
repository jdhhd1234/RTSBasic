#pragma once

#include "core/game_data.hpp"

#include <string>
#include <utility>
#include <vector>

struct lua_State;

// lua_State 수명을 관리하는 RAII 래퍼.
class LuaState
{
public:
    LuaState();
    ~LuaState();

    LuaState(const LuaState&) = delete;
    LuaState& operator=(const LuaState&) = delete;
    LuaState(LuaState&&) = delete;
    LuaState& operator=(LuaState&&) = delete;

    lua_State* Get() const { return state; }

private:
    lua_State* state;
};

// path를 실행해서 반환 테이블을 스택 맨 위에 남긴다. 실패하면 false와 error.
bool LuaRunFile(LuaState& lua, const std::string& path, std::string& error);

// Lua 데이터 파일을 읽어 out에 채운다. 실패하면 false와 error 메시지.
bool LoadGameData(const std::string& path, GameData& out, std::string& error);

// 문자열 → 문자열 테이블을 반환하는 Lua 파일을 읽는다 (언어 팩 등). 문자열이 아닌 항목은 무시.
bool LoadStringPairs(const std::string& path, std::vector<std::pair<std::string, std::string>>& out, std::string& error);
