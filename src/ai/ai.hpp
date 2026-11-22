#pragma once

#include "ai/ai_types.hpp"
#include "core/game_data.hpp"
#include "sim/world.hpp"

#include <vector>

// AI = Blackboard(총사령부 지휘정보망) + HTN(작전 흐름) + Utility AI(사단 단위).
// UpdateAI가 판단 주기마다 Blackboard 갱신 -> HTN 계획 -> 계획 실행 -> 사단별 Utility 순으로 부른다.

// ai_system.cpp: StepWorld가 매 틱 부른다.
void UpdateAI(World& world, const GameData& data, float dt);

// blackboard.cpp: 전장·경제 상황을 board에 모은다. 지난 지휘 결정은 유효하면 유지한다.
void BuildBlackboard(const World& world, const GameData& data, int self, AiBlackboard& board);

// htn.cpp: 루트 과업 "총사령부 지휘"를 원시 과업 목록으로 분해한다.
std::vector<AiTask> PlanCommand(const AiBlackboard& board, const DifficultyDef& difficulty);

// utility.cpp: 사단 하나의 행동 후보 전부와 점수. 0번은 항상 Hold. RL은 이 목록 안에서만 고른다.
std::vector<AiDecision> ListDivisionCandidates(const World& world, const AiBlackboard& board, int self, entt::entity entity);

// utility.cpp: 점수 최고 후보 인덱스 (동점이면 앞쪽).
std::size_t BestCandidate(const std::vector<AiDecision>& candidates);

// utility.cpp: 사단 하나의 행동을 점수로 고른다 (= 후보 중 최고점).
AiDecision ChooseDivisionAction(const World& world, const AiBlackboard& board, int self, entt::entity entity);

// battalion_ai.cpp: 대대마다 사단 담당 범위 안에서 위치를 고르고 움직인다 (매 틱).
void UpdateBattalionAI(World& world, const GameData& data, float dt);

// battalion_ai.cpp: 사단이 바라보는 방향 (가장 가까운 적 사단 쪽, 없으면 지도 중앙). 단위 벡터.
Vec2 DivisionFacing(const World& world, Vec2 position, int faction);

// battalion_ai.cpp: 대대를 전선 배치 슬롯에 바로 놓는다 (생성, 순간 이동 시).
void ResetBattalionPositions(World& world, entt::entity division);

// utility.cpp: 사단이 지금 향하는 헥스 (이동 중이 아니면 현재 헥스).
int CurrentGoal(const World& world, entt::entity entity);
