#pragma once

#include "core/game_data.hpp"
#include "sim/hex.hpp"

#include <cstdint>
#include <string>
#include <vector>

// EnTT 컴포넌트. 데이터만 담는다.

struct Position
{
    Vec2 value;
};

struct Faction
{
    int id = 0;
};

// 대대 AI 임무 (src/ai/battalion_ai.cpp).
enum class BattalionTask : std::uint8_t { Hold, Cover, Engage, Support, Screen, Withdraw };
constexpr const char* BattalionTaskNames[] = { "hold", "cover", "engage", "support", "screen", "withdraw" };

// 사단 안의 대대 하나. 사단 담당 범위(frontageKm 반경) 안에서 스스로 위치를 고른다.
// 피해와 hp는 사단 단위로 합쳐서 계산한다.
struct BattalionUnit
{
    Vec2 position;                 // 월드 km
    Vec2 goal;                     // AI가 고른 목표 위치
    BattalionTask task = BattalionTask::Hold;
    float thinkTimer = 0.0f;       // 다음 판단까지 남은 초
};

struct Division
{
    std::string name;
    std::vector<int> battalions;   // GameData::battalions 인덱스
    std::vector<BattalionUnit> units;   // battalions와 같은 순서
    float hp = 0.0f;
    float maxHp = 0.0f;
    float speed = 0.0f;            // 가장 느린 대대, km/초
    float defenseAvg = 0.0f;
    std::uint32_t effects = 0;     // EffectBit 묶음, 같은 효과는 1회
    float lastCombatTime = -1000.0f;
    float frontageKm = 30.0f;      // 담당 전선 길이. 적이 이 거리 안에 들면 교전 (GUI에서 조절)
    bool hasTarget = false;        // 이번 틱에 쏜 대상 (교전선 표시용)
    Vec2 targetPosition;
};

// 남은 이동 경로 (헥스 인덱스). 다 가면 컴포넌트를 뗀다.
struct MovePath
{
    std::vector<int> hexes;
};

// 후퇴 중 표시. 후퇴 중에는 공격하지 않고 영토를 점령하지 않는다.
struct Retreating
{
};

struct Base
{
    std::string name;
    BaseType type = BaseType::Division;
    int originalOwner = 0;
    int hex = 0;
    float hp = 0.0f;
    float maxHp = 0.0f;
};
