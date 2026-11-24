#pragma once

#include <string>
#include <vector>

struct lua_State;
struct World;
struct GameData;

// 경기 중 Lua 스크립트 (시나리오의 script = "파일.lua").
// 게임 상태는 매 호출마다 복사한 읽기 전용 표(game)로만 넘긴다. Lua에서 C++ 값을 바꿀 수 없다.
// 스크립트가 할 수 있는 것: 읽고 계산해서 print로 이벤트 기록에 남기기.
//
//   interval = 5                    -- on_tick 호출 간격(초), 생략하면 1
//   function on_start(game) end     -- 경기 시작 직후 1회 (선택)
//   function on_tick(game) end      -- interval마다 (선택)
class MatchScript
{
public:
    MatchScript() = default;
    ~MatchScript();

    MatchScript(const MatchScript&) = delete;
    MatchScript& operator=(const MatchScript&) = delete;
    MatchScript(MatchScript&& other) noexcept;
    MatchScript& operator=(MatchScript&& other) noexcept;

    // 이전 스크립트를 닫고 새로 연다. 실패하면 error에 이유를 넣고 닫힌 상태로 둔다.
    bool Load(const std::string& path, std::string& error);
    void Unload();
    bool Loaded() const;

    // name 함수가 있으면 game 표를 넘겨 부른다. print 출력은 output에 쌓인다.
    // 실행 오류면 false (스크립트는 호출자가 Unload).
    bool Call(const char* name, const World& world, const GameData& data, std::vector<std::string>& output, std::string& error);

    // 남은 시간을 dt만큼 줄이고 interval이 지났으면 true.
    bool Due(float dt);

private:
    lua_State* state = nullptr;
    float interval = 1.0f;
    float timer = 0.0f;
};

// StepWorld가 매 틱 부른다. 스크립트 오류는 이벤트로 알리고 스크립트를 끈다.
void UpdateMatchScript(World& world, const GameData& data, float dt);

// InitWorld가 부른다. path가 비어 있으면 스크립트 없음.
void StartMatchScript(World& world, const GameData& data, const std::string& path);
