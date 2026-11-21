#pragma once

#include "sim/hex.hpp"

#include <cstdint>
#include <vector>

// AI 3계층 데이터.
//  1) Blackboard (총사령부 지휘정보망): 판단 주기마다 전장 상황을 모아 두는 공유 메모.
//  2) HTN (작전 흐름): Blackboard를 보고 "총사령부 지휘" 과업을 원시 과업 목록으로 분해한다.
//  3) Utility AI (사단 단위): 각 사단이 Blackboard의 지시를 참고해 행동 점수를 매기고 최고점을 고른다.

enum class AiPosture : std::uint8_t { Hold, Regroup, Attack };
constexpr const char* AiPostureNames[] = { "hold", "regroup", "attack" };

enum class AiTaskType : std::uint8_t
{
    Produce,          // 생산 대기열 채우기
    Conscript,        // 징집
    BuildFactory,     // factoryType 공장 건설
    Defend,           // hex의 아군 기지 방어 요청
    ChooseObjective,  // hex의 적 기지를 공격 목표로 지정
    SetRally,         // hex를 집결지로 지정
    SetPosture,       // 작전 태세 변경
};
constexpr const char* AiTaskNames[] = { "produce", "conscript", "build_factory", "defend", "choose_objective", "set_rally", "set_posture" };

// HTN 원시 과업.
struct AiTask
{
    AiTaskType type = AiTaskType::Produce;
    int hex = -1;
    AiPosture posture = AiPosture::Hold;
    int factoryType = -1;
};

// 적이 접근한 아군 기지.
struct AiThreat
{
    int baseHex = -1;
    Vec2 position;
    float enemyStrength = 0.0f;   // 반경 안 적 사단 HP 합
    float ownStrength = 0.0f;     // 반경 안 아군 사단 HP 합
    bool hq = false;
    bool requested = false;       // HTN이 방어를 지시했는가
    float assigned = 0.0f;        // 이번 주기 Utility가 배정한 아군 HP
};

// 공격 후보 적 기지.
struct AiObjective
{
    int hex = -1;
    Vec2 position;
    float guardStrength = 0.0f;   // 반경 안 적 사단 HP 합
    bool hq = false;
};

struct AiBlackboard
{
    // 감지 (BuildBlackboard가 매 주기 다시 채움)
    float ownStrength = 0.0f;
    float enemyStrength = 0.0f;
    float timeLeftRatio = 1.0f;   // 남은 경기 시간 비율
    bool canProduce = false;      // 대기열 여유 + 생산 가능한 편제가 있음
    bool needManpower = false;    // 가장 싼 편제도 인력이 모자람
    bool canConscript = false;    // 징집해도 압력이 안전선 안
    bool canAffordUpkeep = true;  // 석유 수지가 흑자거나 비축이 넉넉함
    int factoryToBuild = -1;      // 지을 공장 종류 (없으면 -1)
    bool canBuildFactory = false; // factoryToBuild 비용이 있음
    Vec2 center;                  // 아군 HQ 위치 (없으면 아군 사단 중심)
    std::vector<AiThreat> threats;
    std::vector<AiObjective> objectives;
    float rallyGathered = 0.0f;   // 집결지 반경 안 아군 HP 합

    // 지휘 결정 (HTN이 쓰고 다음 주기까지 유지)
    AiPosture posture = AiPosture::Hold;
    int attackHex = -1;
    Vec2 attackPosition;
    int rallyHex = -1;
    Vec2 rallyPosition;
    std::vector<AiTask> plan;     // 마지막 HTN 계획
};

enum class AiAction : std::uint8_t { Hold, Defend, Attack, Rally, Recover, Raid };
constexpr const char* AiActionNames[] = { "hold", "defend", "attack", "rally", "recover", "raid" };

// Utility AI가 고른 사단 행동.
struct AiDecision
{
    AiAction action = AiAction::Hold;
    int hex = -1;                 // 이동 목표, Hold면 -1
    float score = 0.0f;
    int threatIndex = -1;         // Defend일 때 AiBlackboard::threats 인덱스
};
