#include "ai/ai.hpp"
#include "core/templates.hpp"
#include "sim/components.hpp"
#include "sim/economy.hpp"
#include "sim/simulation.hpp"
#include "sim/systems.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <random>

// 판단 주기 관리와 HTN 원시 과업 실행. 실제 상태 변경은 모두 ApplyCommand로 한다.

namespace
{
    void Produce(World& world, const GameData& data, int self)
    {
        FactionState& state = world.factions[static_cast<std::size_t>(self)];
        while (state.queue.size() < 2)
        {
            std::vector<int> affordable;
            for (int i = 0; i < static_cast<int>(state.templates.size()); ++i)
            {
                if (ProductionBlocker(state, data, state.templates[static_cast<std::size_t>(i)]).empty())
                {
                    affordable.push_back(i);
                }
            }
            if (affordable.empty())
            {
                return;
            }
            std::uniform_int_distribution<std::size_t> pick(0, affordable.size() - 1);
            ApplyCommand(world, data, Command{ .type = CommandType::Produce, .faction = self, .templateIndex = affordable[pick(world.rng)] });
        }
    }

    void ChooseRally(const World& world, int self, AiBlackboard& board)
    {
        board.rallyHex = -1;
        float best = std::numeric_limits<float>::max();
        for (const auto [entity, base, faction, position] : world.registry.view<Base, Faction, Position>().each())
        {
            const float distance = Distance(position.value, board.attackPosition);
            if (faction.id == self && distance < best)
            {
                best = distance;
                board.rallyHex = base.hex;
                board.rallyPosition = position.value;
            }
        }
    }

    void ExecutePlan(World& world, const GameData& data, int self, AiBlackboard& board)
    {
        for (const AiTask& task : board.plan)
        {
            switch (task.type)
            {
            case AiTaskType::Produce:
                Produce(world, data, self);
                break;
            case AiTaskType::Conscript:
                ApplyCommand(world, data, Command{ .type = CommandType::Conscript, .faction = self });
                break;
            case AiTaskType::BuildFactory:
                ApplyCommand(world, data, Command{ .type = CommandType::BuildFactory, .faction = self, .factoryType = task.factoryType });
                break;
            case AiTaskType::Defend:
                for (AiThreat& threat : board.threats)
                {
                    threat.requested = threat.requested || threat.baseHex == task.hex;
                }
                break;
            case AiTaskType::ChooseObjective:
                for (const AiObjective& objective : board.objectives)
                {
                    if (objective.hex == task.hex)
                    {
                        board.attackHex = objective.hex;
                        board.attackPosition = objective.position;
                    }
                }
                break;
            case AiTaskType::SetRally:
                ChooseRally(world, self, board);
                break;
            case AiTaskType::SetPosture:
                board.posture = task.posture;
                break;
            }
        }
        if (board.posture == AiPosture::Regroup && board.rallyHex < 0)
        {
            board.posture = AiPosture::Hold;   // 집결할 기지가 없으면 수세
        }
    }

}

void UpdateAI(World& world, const GameData& data, float dt)
{
    for (int self = 0; self < FactionCount; ++self)
    {
        FactionState& state = world.factions[static_cast<std::size_t>(self)];
        if (!state.aiControlled)
        {
            continue;
        }
        state.aiThinkTimer -= dt;
        if (state.aiThinkTimer > 0.0f)
        {
            continue;
        }
        const DifficultyDef& difficulty = data.difficulties[static_cast<std::size_t>(state.difficulty)];
        state.aiThinkTimer = difficulty.thinkSeconds;

        Profiler& profiler = world.profiler;
        const auto thinkStart = std::chrono::steady_clock::now();
        AiProfile& stats = state.aiProfile;
        ++stats.thinks;

        AiBlackboard& board = state.blackboard;
        { ProfileScope scope(profiler, ProfileSection::AiBlackboard); BuildBlackboard(world, data, self, board); }
        { ProfileScope scope(profiler, ProfileSection::AiHtn); board.plan = PlanCommand(board, difficulty); }
        { ProfileScope scope(profiler, ProfileSection::AiExecute); ExecutePlan(world, data, self, board); }

        {
            ProfileScope scope(profiler, ProfileSection::AiUtility);
            std::vector<entt::entity> divisions;
            for (const auto [entity, division, faction] : world.registry.view<Division, Faction>().each())
            {
                if (faction.id == self && !world.registry.all_of<Retreating>(entity))
                {
                    divisions.push_back(entity);
                }
            }
            for (const entt::entity entity : divisions)
            {
                const AiDecision decision = ChooseDivisionAction(world, board, self, entity);
                ++stats.actions[static_cast<std::size_t>(decision.action)];
                if (decision.threatIndex >= 0)
                {
                    board.threats[static_cast<std::size_t>(decision.threatIndex)].assigned += world.registry.get<Division>(entity).hp;
                }
                if (decision.hex >= 0 && decision.hex != CurrentGoal(world, entity))
                {
                    ApplyCommand(world, data, Command{ .type = CommandType::Move, .entity = entity, .targetHex = decision.hex, .faction = self });
                    ++stats.moveOrders;
                }
            }
        }

        if (profiler.enabled)
        {
            stats.lastThinkMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - thinkStart).count();
            stats.maxThinkMs = std::max(stats.maxThinkMs, stats.lastThinkMs);
        }
    }
}
