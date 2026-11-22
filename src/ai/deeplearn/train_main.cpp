#include "ai/deeplearn/rl_agent.hpp"
#include "ai/deeplearn/rl_policy.hpp"
#include "core/lua_loader.hpp"
#include "sim/scenario.hpp"
#include "sim/simulation.hpp"

#include <ATen/Parallel.h>
#include <torch/nn/utils/clip_grad.h>
#include <torch/optim/adam.h>
#include <torch/utils.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
    using namespace rl_training;

    constexpr float Gamma = 0.99f;
    constexpr float GaeLambda = 0.95f;
    constexpr float Clip = 0.2f;
    constexpr int Epochs = 4;
    constexpr std::size_t MinibatchSize = 64;

    template <typename Number>
    Number ParseNumber(std::string_view name, std::string_view value)
    {
        Number number{};
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
        if (error != std::errc{} || end != value.data() + value.size())
        {
            throw std::invalid_argument("invalid value for " + std::string(name));
        }
        return number;
    }

    Options ParseOptions(int argc, char** argv)
    {
        Options options;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view name = argv[i];
            if (i + 1 == argc)
            {
                throw std::invalid_argument("missing value for " + std::string(name));
            }
            const std::string_view value = argv[++i];
            if (name == "--iterations")
                options.iterations = ParseNumber<int>(name, value);
            else if (name == "--episodes-per-iter")
                options.episodesPerIteration = ParseNumber<int>(name, value);
            else if (name == "--threads")
                options.threads = ParseNumber<int>(name, value);
            else if (name == "--seconds")
                options.seconds = ParseNumber<float>(name, value);
            else if (name == "--seed")
                options.seed = ParseNumber<unsigned>(name, value);
            else if (name == "--out")
                options.out = value;
            else if (name == "--resume")
                options.resume = value;
            else
                throw std::invalid_argument("unknown option " + std::string(name));
        }
        if (options.iterations <= 0 || options.episodesPerIteration <= 0 || options.threads <= 0
            || !std::isfinite(options.seconds) || options.seconds <= 0.0f || options.out.empty())
        {
            throw std::invalid_argument("iterations, episodes, threads and seconds must be positive, and out must be a path");
        }
        return options;
    }

    std::vector<Episode> CollectEpisodes(const Scenario& scenario, const MatchSetup& setup, const Options& options,
                                         const RlPolicy& policy, int iteration)
    {
        const auto episodeCount = static_cast<std::size_t>(options.episodesPerIteration);
        const auto workerCount = static_cast<std::size_t>(std::min(options.threads, options.episodesPerIteration));
        const float dt = 1.0f / scenario.data.config.tickHz;
        std::vector<Episode> episodes(episodeCount);
        std::vector<std::exception_ptr> errors(workerCount);
        std::vector<std::jthread> workers;
        workers.reserve(workerCount);
        for (std::size_t worker = 0; worker < workerCount; ++worker)
        {
            workers.emplace_back([&, worker, local = policy.CloneToCpu()]() mutable
            {
                try
                {
                    const torch::NoGradGuard noGrad;
                    for (std::size_t i = worker; i < episodeCount; i += workerCount)
                    {
                        const auto episodeIndex = static_cast<std::uint64_t>(iteration) * episodeCount + i;
                        const int self = episodeIndex % 2 == 0 ? PlayerFaction : AiFaction;
                        const auto seed = options.seed + static_cast<unsigned>(episodeIndex);
                        Episode& episode = episodes[i];
                        episode.recorder.rng.seed(seed ^ 0x9e3779b9u);
                        episode.recorder.verifyUtility = iteration == 0 && options.resume.empty();
                        World world;
                        InitWorld(world, scenario.data, setup, seed);
                        world.factions[static_cast<std::size_t>(self)].policy = MakeRlHook(scenario.data, local, episode.recorder);
                        while (!world.over)
                        {
                            StepWorld(world, scenario.data, dt);
                        }
                        FinishRlEpisode(episode.recorder, world, self);
                        episode.won = world.winner == self;
                        episode.self = self;
                    }
                }
                catch (...)
                {
                    errors[worker] = std::current_exception();
                }
            });
        }
        for (auto& worker : workers)
        {
            worker.join();
        }
        for (const auto& error : errors)
        {
            if (error)
            {
                std::rethrow_exception(error);
            }
        }
        return episodes;
    }

    std::vector<Sample> BuildSamples(const std::vector<Episode>& episodes)
    {
        std::vector<Sample> samples;
        for (const Episode& episode : episodes)
        {
            const auto& steps = episode.recorder.steps;
            float nextValue = 0.0f;
            float advantage = 0.0f;
            for (auto it = steps.rbegin(); it != steps.rend(); ++it)
            {
                const RlStep& step = *it;
                const float discount = step.done ? 0.0f : Gamma;
                const float delta = step.reward + discount * nextValue - step.value;
                advantage = delta + discount * GaeLambda * advantage;
                nextValue = step.value;
                const float oldLogProb = std::accumulate(step.decisions.begin(), step.decisions.end(), 0.0f,
                    [](float sum, const RlDecisionRecord& decision) { return sum + decision.logProb; });
                samples.push_back({&step, oldLogProb, advantage, advantage + step.value});
            }
        }
        if (samples.empty())
        {
            throw std::runtime_error("the scenario produced no AI think steps");
        }
        const auto count = static_cast<double>(samples.size());
        const double mean = std::accumulate(samples.begin(), samples.end(), 0.0,
            [](double sum, const Sample& sample) { return sum + sample.advantage; }) / count;
        const double variance = std::accumulate(samples.begin(), samples.end(), 0.0,
            [mean](double sum, const Sample& sample)
            {
                const double delta = sample.advantage - mean;
                return sum + delta * delta;
            }) / count;
        const double scale = std::sqrt(variance) + 1e-8;
        for (Sample& sample : samples)
        {
            sample.advantage = static_cast<float>((sample.advantage - mean) / scale);
        }
        return samples;
    }

    std::pair<torch::Tensor, torch::Tensor> JointPolicyStats(RlPolicy& policy, std::span<const Sample> batch)
    {
        std::int64_t decisionCount = 0;
        std::size_t maxCandidates = 0;
        for (const Sample& sample : batch)
        {
            for (const RlDecisionRecord& decision : sample.step->decisions)
            {
                ++decisionCount;
                maxCandidates = std::max(maxCandidates, decision.candidates.size());
            }
        }
        const auto stepCount = static_cast<std::int64_t>(batch.size());
        const auto device = policy.Device();
        const auto zeros = torch::zeros({stepCount}, torch::TensorOptions().dtype(torch::kFloat32).device(device));
        if (decisionCount == 0)
        {
            return {zeros, zeros};
        }

        const auto width = static_cast<std::int64_t>(maxCandidates);
        const auto inputs = torch::zeros({decisionCount, width, RlInputFeatures}, torch::kFloat32);
        const auto utility = torch::zeros({decisionCount, width}, torch::kFloat32);
        const auto mask = torch::zeros({decisionCount, width}, torch::kBool);
        std::vector<std::int64_t> chosen;
        std::vector<std::int64_t> owners;
        chosen.reserve(static_cast<std::size_t>(decisionCount));
        owners.reserve(static_cast<std::size_t>(decisionCount));
        auto inputRows = inputs.accessor<float, 3>();
        auto utilityRows = utility.accessor<float, 2>();
        auto maskRows = mask.accessor<bool, 2>();
        std::int64_t row = 0;
        for (std::int64_t step = 0; step < stepCount; ++step)
        {
            for (const RlDecisionRecord& decision : batch[static_cast<std::size_t>(step)].step->decisions)
            {
                for (std::size_t i = 0; i < decision.candidates.size(); ++i)
                {
                    const auto col = static_cast<std::int64_t>(i);
                    std::ranges::copy(decision.candidates[i], inputRows[row][col].data());
                    utilityRows[row][col] = decision.utility[i];
                    maskRows[row][col] = true;
                }
                chosen.push_back(decision.chosen);
                owners.push_back(step);
                ++row;
            }
        }

        const auto padding = mask.to(device).logical_not();
        const auto logits = policy.Logits(inputs.to(device), utility.to(device));
        const auto logProbs = logits.masked_fill(padding, -std::numeric_limits<float>::infinity()).log_softmax(-1);
        const auto selected = logProbs.gather(1, torch::tensor(chosen).to(device).unsqueeze(1)).squeeze(1);
        const auto entropy = -(logProbs.exp() * logProbs.masked_fill(padding, 0.0f)).sum(-1);
        const auto stepIds = torch::tensor(owners).to(device);
        return {zeros.index_add(0, stepIds, selected), zeros.index_add(0, stepIds, entropy)};
    }

    UpdateStats UpdatePolicy(RlPolicy& policy, torch::optim::Adam& optimizer, std::vector<Sample>& samples, std::mt19937& rng)
    {
        UpdateStats stats;
        const auto parameters = policy.Parameters();
        const auto device = policy.Device();
        for (int epoch = 0; epoch < Epochs; ++epoch)
        {
            std::ranges::shuffle(samples, rng);
            for (std::size_t first = 0; first < samples.size(); first += MinibatchSize)
            {
                const auto count = std::min(MinibatchSize, samples.size() - first);
                const std::span<const Sample> batch(samples.data() + first, count);
                std::vector<float> global;
                std::vector<float> oldLogProbs;
                std::vector<float> advantages;
                std::vector<float> targets;
                global.reserve(count * RlGlobalFeatures);
                oldLogProbs.reserve(count);
                advantages.reserve(count);
                targets.reserve(count);
                for (const Sample& sample : batch)
                {
                    global.insert(global.end(), sample.step->global.begin(), sample.step->global.end());
                    oldLogProbs.push_back(sample.oldLogProb);
                    advantages.push_back(sample.advantage);
                    targets.push_back(sample.valueTarget);
                }
                const auto [logProbs, entropy] = JointPolicyStats(policy, batch);
                const auto logRatio = logProbs - torch::tensor(oldLogProbs).to(device);
                const auto ratio = logRatio.exp();
                const auto advantage = torch::tensor(advantages).to(device);
                const auto actorLoss = -torch::minimum(ratio * advantage,
                    ratio.clamp(1.0f - Clip, 1.0f + Clip) * advantage).mean();
                const auto values = policy.Value(torch::tensor(global).reshape({-1, RlGlobalFeatures}).to(device));
                const auto valueLoss = (values - torch::tensor(targets).to(device)).square().mean();
                const auto loss = actorLoss + 0.5f * valueLoss - 0.01f * entropy.mean();
                optimizer.zero_grad();
                loss.backward();
                torch::nn::utils::clip_grad_norm_(parameters, 0.5, 2.0, true);
                optimizer.step();

                const torch::NoGradGuard noGrad;
                stats.entropy += entropy.sum().item<double>();
                stats.approxKl += (logRatio.expm1() - logRatio).sum().item<double>();
            }
        }
        const auto count = static_cast<double>(samples.size()) * Epochs;
        return {stats.entropy / count, stats.approxKl / count};
    }

    void Train(const Options& options, const std::filesystem::path& dataDir)
    {
        at::set_num_threads(1);
        torch::manual_seed(options.seed);
        std::mt19937 rng(options.seed);
        GameData data;
        Scenario scenario;
        std::string error;
        if (!LoadGameData((dataDir / "game_data.lua").string(), data, error))
        {
            throw std::runtime_error("game data: " + error);
        }
        if (!LoadScenario((dataDir / "sandbox/bot_battle.lua").string(), data, data.defaultTemplates, scenario, error))
        {
            throw std::runtime_error("scenario: " + error);
        }
        MatchSetup setup = scenario.setup;
        setup.matchSeconds = options.seconds;
        for (auto& side : setup.sides)
        {
            side.ai = true;
        }

        RlPolicy policy;
        if (!options.resume.empty())
        {
            policy.Load(options.resume);
            std::cout << "resumed=" << options.resume.string() << std::endl;
        }
        torch::optim::Adam optimizer(policy.Parameters(), torch::optim::AdamOptions(3e-4));
        std::cout << "device=" << policy.Device() << " parameters=" << policy.ParameterCount() << std::endl;
        for (int iteration = 0; iteration < options.iterations; ++iteration)
        {
            const auto start = std::chrono::steady_clock::now();
            const auto episodes = CollectEpisodes(scenario, setup, options, policy, iteration);
            double reward = 0.0;
            std::array<int, FactionCount> wins{};
            std::array<int, FactionCount> games{};
            std::size_t verified = 0;
            for (const Episode& episode : episodes)
            {
                reward = std::accumulate(episode.recorder.steps.begin(), episode.recorder.steps.end(), reward,
                    [](double sum, const RlStep& step) { return sum + step.reward; });
                wins[static_cast<std::size_t>(episode.self)] += static_cast<int>(episode.won);
                ++games[static_cast<std::size_t>(episode.self)];
                verified += episode.recorder.verifiedDecisions;
            }
            if (iteration == 0 && options.resume.empty())
            {
                if (verified == 0)
                {
                    throw std::runtime_error("no division decisions for zero-init verification");
                }
                std::cout << "zero_init_argmax_matches=" << verified << std::endl;
            }
            auto samples = BuildSamples(episodes);
            const auto [entropy, approxKl] = UpdatePolicy(policy, optimizer, samples, rng);
            policy.Save(options.out);
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            const auto count = static_cast<double>(episodes.size());
            std::cout << std::fixed << std::setprecision(4)
                      << "iteration=" << iteration + 1 << " mean_reward=" << reward / count
                      << " win_blue=" << wins[PlayerFaction] << "/" << games[PlayerFaction]
                      << " win_red=" << wins[AiFaction] << "/" << games[AiFaction] << " mean_entropy=" << entropy
                      << " approx_kl=" << approxKl << " seconds=" << elapsed << std::endl;
        }
        std::cout << "final_checkpoint=" << options.out.string() << std::endl;
    }
}

int main(int argc, char** argv)
{
    constexpr std::string_view Usage = "usage: minrts_train [--iterations N] [--episodes-per-iter N] [--seconds N] "
                                       "[--threads N] [--out PATH] [--resume PATH] [--seed N]\n";
    if (argc == 2 && std::string_view(argv[1]) == "--help")
    {
        std::cout << Usage;
        return 0;
    }
    try
    {
        const auto options = ParseOptions(argc, argv);
        Train(options, std::filesystem::absolute(argv[0]).parent_path() / "data");
    }
    catch (const std::invalid_argument& error)
    {
        std::cerr << error.what() << '\n' << Usage;
        return 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "training failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
