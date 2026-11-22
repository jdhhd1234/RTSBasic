#pragma once

#include "ai/deeplearn/rl_features.hpp"

#include <filesystem>
#include <random>

class RlPolicy;

struct RlRecorder
{
    std::vector<RlStep> steps;
    RlRewardSnapshot before;
    std::mt19937 rng;
    bool verifyUtility = false;
    std::size_t verifiedDecisions = 0;
};

// data, policy and recorder must outlive the training hook.
AiPolicyHook MakeRlHook(const GameData& data, RlPolicy& policy, RlRecorder& recorder);
void FinishRlEpisode(RlRecorder& recorder, const World& world, int self);

// The hook owns its CPU policy. data must outlive the hook.
AiPolicyHook MakeRlInferenceHook(const GameData& data, const std::filesystem::path& checkpoint);

namespace rl_training
{
    struct Options
    {
        int iterations = 100;
        int episodesPerIteration = 8;
        int threads = 4;
        float seconds = 900.0f;
        unsigned seed = 1;
        std::filesystem::path out = "data/ai/rl_policy.pt";
        std::filesystem::path resume;
    };

    struct Episode
    {
        RlRecorder recorder;
        bool won = false;
        int self = 0;                  // RL이 맡은 진영 (맵이 한쪽에 유리할 수 있어 승률을 진영별로 본다)
    };

    struct Sample
    {
        const RlStep* step = nullptr;
        float oldLogProb = 0.0f;
        float advantage = 0.0f;
        float valueTarget = 0.0f;
    };

    struct UpdateStats
    {
        double entropy = 0.0;
        double approxKl = 0.0;
    };
}
