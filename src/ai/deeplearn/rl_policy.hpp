#pragma once

#include "ai/deeplearn/rl_types.hpp"

#include <torch/nn/modules/container/sequential.h>

#include <cstdint>
#include <filesystem>

class RlPolicy
{
public:
    RlPolicy();
    explicit RlPolicy(torch::Device device);
    RlPolicy(const RlPolicy&) = delete;
    RlPolicy& operator=(const RlPolicy&) = delete;
    RlPolicy(RlPolicy&&) = default;
    RlPolicy& operator=(RlPolicy&&) = default;

    void Load(const std::filesystem::path& path);
    void Save(const std::filesystem::path& path) const;
    torch::Tensor Logits(const std::vector<RlInput>& candidates, const std::vector<float>& utility);
    torch::Tensor Logits(const torch::Tensor& candidates, const torch::Tensor& utility);
    float Value(const RlGlobal& global);
    torch::Tensor Value(const torch::Tensor& global);
    RlPolicy CloneToCpu() const;
    std::vector<torch::Tensor> Parameters() const;
    std::int64_t ParameterCount() const;
    torch::Device Device() const;

private:
    torch::Device device_;
    torch::nn::Sequential actor_;
    torch::nn::Sequential critic_;
};
