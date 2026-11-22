#include "ai/deeplearn/rl_agent.hpp"

#include "ai/ai.hpp"
#include "ai/deeplearn/rl_policy.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

namespace
{
    void CloseStep(RlRecorder& recorder, const World& world, int self, const RlRewardSnapshot& after)
    {
        if (!recorder.steps.empty())
        {
            RlStep& step = recorder.steps.back();
            step.reward = ComputeReward(recorder.before, after, world, self);
            step.done = world.over;
        }
    }

    RlDecisionRecord RecordCandidates(const World& world, const GameData& data, const AiBlackboard& board, int self,
                                      entt::entity division, const RlGlobal& global, const std::vector<AiDecision>& candidates)
    {
        RlDecisionRecord record;
        BuildCandidateFeatures(world, data, board, self, division, global, candidates, record.candidates);
        record.utility.reserve(candidates.size());
        for (const AiDecision& candidate : candidates)
        {
            record.utility.push_back(candidate.score);
        }
        return record;
    }
}

AiPolicyHook MakeRlHook(const GameData& data, RlPolicy& policy, RlRecorder& recorder)
{
    return {
        .beginThink = [&data, &policy, &recorder](const World& world, const AiBlackboard& board, int self)
        {
            const auto snapshot = TakeRewardSnapshot(world, self);
            CloseStep(recorder, world, self, snapshot);
            recorder.before = snapshot;
            RlStep& step = recorder.steps.emplace_back();
            BuildGlobalFeatures(world, data, board, self, step.global);
            const torch::NoGradGuard noGrad;
            step.value = policy.Value(step.global);
        },
        .chooseDivision = [&data, &policy, &recorder](const World& world, const AiBlackboard& board, int self,
                                                      entt::entity division, const std::vector<AiDecision>& candidates)
        {
            RlStep& step = recorder.steps.back();
            auto record = RecordCandidates(world, data, board, self, division, step.global, candidates);
            const torch::NoGradGuard noGrad;
            const auto logits = policy.Logits(record.candidates, record.utility);
            if (recorder.verifyUtility)
            {
                if (static_cast<std::size_t>(logits.argmax().item<std::int64_t>()) != BestCandidate(candidates))
                {
                    throw std::logic_error("zero-initialized policy argmax differs from BestCandidate");
                }
                ++recorder.verifiedDecisions;
            }

            const auto logProbs = logits.log_softmax(0).cpu();
            const auto probabilities = logProbs.exp();
            const float* weights = probabilities.data_ptr<float>();
            std::discrete_distribution<int> sample(weights, weights + candidates.size());
            record.chosen = sample(recorder.rng);
            record.logProb = logProbs.data_ptr<float>()[record.chosen];
            const auto chosen = static_cast<std::size_t>(record.chosen);
            step.decisions.push_back(std::move(record));
            return chosen;
        }
    };
}

void FinishRlEpisode(RlRecorder& recorder, const World& world, int self)
{
    CloseStep(recorder, world, self, TakeRewardSnapshot(world, self));
}

AiPolicyHook MakeRlInferenceHook(const GameData& data, const std::filesystem::path& checkpoint)
{
    auto policy = std::make_shared<RlPolicy>(torch::kCPU);
    policy->Load(checkpoint);
    auto global = std::make_shared<RlGlobal>();
    return {
        .beginThink = [&data, global](const World& world, const AiBlackboard& board, int self)
        {
            BuildGlobalFeatures(world, data, board, self, *global);
        },
        .chooseDivision = [&data, global, policy = std::move(policy)](const World& world, const AiBlackboard& board, int self,
                                                                     entt::entity division, const std::vector<AiDecision>& candidates)
        {
            if (candidates.size() == 1)
            {
                return std::size_t{0};
            }
            const torch::NoGradGuard noGrad;
            const auto record = RecordCandidates(world, data, board, self, division, *global, candidates);
            return static_cast<std::size_t>(policy->Logits(record.candidates, record.utility).argmax().item<std::int64_t>());
        }
    };
}
