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

struct Division
{
    std::string name;
    std::vector<int> battalions;   // GameData::battalions 인덱스
    float hp = 0.0f;
    float maxHp = 0.0f;
    float speed = 0.0f;            // 가장 느린 대대, km/초
    float defenseAvg = 0.0f;
    std::uint32_t effects = 0;     // EffectBit 묶음, 같은 효과는 1회
    float lastCombatTime = -1000.0f;
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
