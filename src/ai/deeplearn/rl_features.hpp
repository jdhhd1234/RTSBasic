#pragma once

#include "ai/deeplearn/rl_types.hpp"
#include "core/game_data.hpp"
#include "sim/world.hpp"

#include <vector>

// rl_features.cpp: 관측 특징과 보상. 모든 값은 대략 [-1, 1] 또는 [0, 1]로 정규화한다.

void BuildGlobalFeatures(const World& world, const GameData& data, const AiBlackboard& board, int self, RlGlobal& out);

// 후보마다 global + division + candidate 특징을 이어 붙인다. out.size() == candidates.size().
void BuildCandidateFeatures(const World& world, const GameData& data, const AiBlackboard& board, int self, entt::entity division,
                            const RlGlobal& global, const std::vector<AiDecision>& candidates, std::vector<RlInput>& out);

RlRewardSnapshot TakeRewardSnapshot(const World& world, int self);

// before -> after 사이 보상. 영토·적 피해·기지 변화 + 경기 끝이면 승패 보너스.
float ComputeReward(const RlRewardSnapshot& before, const RlRewardSnapshot& after, const World& world, int self);
