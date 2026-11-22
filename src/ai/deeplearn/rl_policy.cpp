#include "ai/deeplearn/rl_policy.hpp"

#include <torch/cuda.h>
#include <torch/nn/modules/activation.h>
#include <torch/nn/modules/linear.h>
#include <torch/serialize.h>

#include <algorithm>
#include <fstream>
#include <random>
#include <stdexcept>

RlPolicy::RlPolicy() : RlPolicy(torch::cuda::is_available() ? torch::kCUDA : torch::kCPU)
{
}

RlPolicy::RlPolicy(torch::Device device)
    : device_(device),
      actor_(torch::nn::Linear(RlInputFeatures, 512), torch::nn::ReLU(),
             torch::nn::Linear(512, 512), torch::nn::ReLU(),
             torch::nn::Linear(512, 256), torch::nn::ReLU(), torch::nn::Linear(256, 1)),
      critic_(torch::nn::Linear(RlGlobalFeatures, 512), torch::nn::ReLU(),
              torch::nn::Linear(512, 512), torch::nn::ReLU(), torch::nn::Linear(512, 1))
{
    const torch::NoGradGuard noGrad;
    auto& output = actor_->at<torch::nn::LinearImpl>(6);
    output.weight.zero_();
    output.bias.zero_();
    actor_->to(device_);
    critic_->to(device_);
}

void RlPolicy::Load(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("cannot open checkpoint: " + path.string());
    }
    torch::serialize::InputArchive archive;
    archive.load_from(file, device_);
    torch::serialize::InputArchive actor;
    torch::serialize::InputArchive critic;
    archive.read("actor", actor);
    archive.read("critic", critic);
    actor_->load(actor);
    critic_->load(critic);
}

void RlPolicy::Save(const std::filesystem::path& path) const
{
    if (path.has_parent_path())
    {
        std::filesystem::create_directories(path.parent_path());
    }
    std::filesystem::path temporary = path;
    std::random_device random;
    temporary += "." + std::to_string(random()) + "." + std::to_string(random()) + ".tmp";
    torch::serialize::OutputArchive archive;
    torch::serialize::OutputArchive actor(archive.compilation_unit());
    torch::serialize::OutputArchive critic(archive.compilation_unit());
    actor_->save(actor);
    critic_->save(critic);
    archive.write("actor", actor);
    archive.write("critic", critic);
    try
    {
        std::ofstream file(temporary, std::ios::binary);
        file.exceptions(std::ios::failbit | std::ios::badbit);
        archive.save_to(file);
        file.close();
        std::filesystem::rename(temporary, path);
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

torch::Tensor RlPolicy::Logits(const std::vector<RlInput>& candidates, const std::vector<float>& utility)
{
    if (candidates.empty() || candidates.size() != utility.size())
    {
        throw std::invalid_argument("candidate and utility lists must have the same nonzero size");
    }
    const auto count = static_cast<std::int64_t>(candidates.size());
    const torch::Tensor inputs = torch::empty({count, RlInputFeatures}, torch::kFloat32);
    float* row = inputs.data_ptr<float>();
    for (const RlInput& candidate : candidates)
    {
        row = std::ranges::copy(candidate, row).out;
    }
    return Logits(inputs.to(device_), torch::tensor(utility).to(device_));
}

torch::Tensor RlPolicy::Logits(const torch::Tensor& candidates, const torch::Tensor& utility)
{
    return utility / RlUtilityTemperature + actor_->forward(candidates).squeeze(-1);
}

float RlPolicy::Value(const RlGlobal& global)
{
    return Value(torch::tensor(torch::ArrayRef<float>(global)).to(device_)).item<float>();
}

torch::Tensor RlPolicy::Value(const torch::Tensor& global)
{
    return critic_->forward(global).squeeze(-1);
}

RlPolicy RlPolicy::CloneToCpu() const
{
    const torch::NoGradGuard noGrad;
    RlPolicy copy(torch::kCPU);
    const auto original = Parameters();
    const auto cloned = copy.Parameters();
    for (std::size_t i = 0; i < original.size(); ++i)
    {
        cloned[i].copy_(original[i]);
    }
    copy.actor_->eval();
    copy.critic_->eval();
    return copy;
}

std::vector<torch::Tensor> RlPolicy::Parameters() const
{
    auto parameters = actor_->parameters();
    const auto critic = critic_->parameters();
    parameters.insert(parameters.end(), critic.begin(), critic.end());
    return parameters;
}

std::int64_t RlPolicy::ParameterCount() const
{
    std::int64_t count = 0;
    for (const auto& parameter : Parameters())
    {
        count += parameter.numel();
    }
    return count;
}

torch::Device RlPolicy::Device() const
{
    return device_;
}
