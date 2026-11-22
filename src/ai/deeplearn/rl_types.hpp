#pragma once

#include "ai/ai_types.hpp"

#include <array>
#include <vector>

// 하이브리드 RL (Utility AI + 신경망 보정).
//  전통 AI(ListDivisionCandidates)가 사단 행동 후보와 점수를 만든다. 신경망은 그 후보 안에서만 고른다.
//  후보 logit = utility 점수 / RlUtilityTemperature + net(특징)
//  net 마지막 층을 0으로 시작하면 학습 전 정책 = 기존 Utility AI의 softmax. 학습은 여기서 보정만 배운다.
//  torch에 의존하지 않는다 (minrts_core에 들어감). torch 쪽은 rl_policy.hpp.

constexpr int RlGlobalFeatures = 24;      // 진영 전체: 경제, 병력비, 영토비, 남은 시간, 태세
constexpr int RlDivisionFeatures = 12;    // 판단하는 사단: hp, 위치, 전선, 교전 여부
constexpr int RlCandidateFeatures = 16;   // 후보: 행동 one-hot(6), 목표 거리, 목표 정보, utility 점수
constexpr int RlInputFeatures = RlGlobalFeatures + RlDivisionFeatures + RlCandidateFeatures;
constexpr float RlUtilityTemperature = 0.1f;

using RlGlobal = std::array<float, RlGlobalFeatures>;
using RlInput = std::array<float, RlInputFeatures>;

// 사단 하나의 판단 (학습 기록).
struct RlDecisionRecord
{
    std::vector<RlInput> candidates;   // 후보별 특징 (global + division + candidate)
    std::vector<float> utility;        // 후보별 utility 점수
    int chosen = 0;
    float logProb = 0.0f;              // 고를 때 정책의 log 확률
};

// 진영 하나의 판단 주기 하나. 보상은 다음 판단 주기 시작(또는 경기 끝)에 붙는다.
struct RlStep
{
    RlGlobal global{};
    std::vector<RlDecisionRecord> decisions;
    float value = 0.0f;                // critic(global)
    float reward = 0.0f;
    bool done = false;
};

// 보상 계산용 진영 상태 요약.
struct RlRewardSnapshot
{
    int ownHexes = 0;
    int enemyHexes = 0;
    float ownHp = 0.0f;                // 사단 hp 합
    float enemyHp = 0.0f;
    int ownBases = 0;
    int enemyBases = 0;
};
