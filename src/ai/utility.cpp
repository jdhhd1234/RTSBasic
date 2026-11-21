#include "ai/ai.hpp"
#include "sim/components.hpp"
#include "sim/systems.hpp"

#include <algorithm>
#include <limits>

// Utility AI: 사단마다 행동 후보에 점수를 매겨 최고점을 고른다. 지휘 태세·목표는 Blackboard에서 읽는다.

namespace
{
    constexpr float RallyRadiusKm = 60.0f;
    constexpr float RaidRadiusKm = 150.0f;
    constexpr float InertiaBonus = 0.1f;   // 사단이 매 주기 목표를 바꾸며 흔들리지 않도록

    float Proximity(float distanceKm, float scaleKm)
    {
        return 1.0f / (1.0f + distanceKm / scaleKm);
    }

    void Consider(AiDecision& best, AiAction action, int hex, float score, int currentGoal, int threatIndex = -1)
    {
        if (hex >= 0 && hex == currentGoal)
        {
            score += InertiaBonus;
        }
        if (score > best.score)
        {
            best = { .action = action, .hex = hex, .score = score, .threatIndex = threatIndex };
        }
    }
}

// 사단이 지금 향하는 헥스 (이동 중이 아니면 현재 헥스).
int CurrentGoal(const World& world, entt::entity entity)
{
    const MovePath* path = world.registry.try_get<MovePath>(entity);
    if (path != nullptr && !path->hexes.empty())
    {
        return path->hexes.back();
    }
    return HexAt(world, world.registry.get<Position>(entity).value);
}


AiDecision ChooseDivisionAction(const World& world, const AiBlackboard& board, int self, entt::entity entity)
{
    const Division& division = world.registry.get<Division>(entity);
    const Vec2 position = world.registry.get<Position>(entity).value;
    const float health = division.maxHp > 0.0f ? division.hp / division.maxHp : 0.0f;
    const int goal = CurrentGoal(world, entity);

    AiDecision best;
    best.score = 0.15f;   // Hold 기본 점수: 할 일이 마땅치 않으면 제자리

    // 방어: 위협 크기 × 거리 × HTN 지시 여부 × 아직 부족한 병력
    for (int i = 0; i < static_cast<int>(board.threats.size()); ++i)
    {
        const AiThreat& threat = board.threats[static_cast<std::size_t>(i)];
        const float need = threat.enemyStrength * 1.2f;
        const float unmet = std::clamp(1.0f - (threat.ownStrength + threat.assigned) / std::max(1.0f, need), 0.0f, 1.0f);
        const float urgency = std::clamp(threat.enemyStrength / (threat.ownStrength + 1.0f), 0.0f, 1.0f);
        const float score = (threat.requested ? 0.9f : 0.4f) * (threat.hq ? 1.2f : 1.0f) * urgency * std::max(unmet, 0.2f)
            * Proximity(Distance(position, threat.position), 200.0f) * 2.0f;
        Consider(best, AiAction::Defend, threat.baseHex, score, goal, i);
    }

    // 공격 / 집결: HTN 태세를 따른다
    if (board.posture == AiPosture::Attack && board.attackHex >= 0)
    {
        Consider(best, AiAction::Attack, board.attackHex, 0.7f * (0.4f + 0.6f * health), goal);
    }
    if (board.posture == AiPosture::Regroup && board.rallyHex >= 0)
    {
        const bool there = Distance(position, board.rallyPosition) <= RallyRadiusKm;
        Consider(best, AiAction::Rally, board.rallyHex, there ? 0.1f : 0.55f, goal);
    }

    // 기습: 가까운 적 기지가 거의 비어 있으면 태세와 관계없이 점령하러 간다
    for (const AiObjective& objective : board.objectives)
    {
        const float distance = Distance(position, objective.position);
        if (distance <= RaidRadiusKm && objective.guardStrength < division.hp * 0.5f)
        {
            Consider(best, AiAction::Raid, objective.hex, 0.6f * health * Proximity(distance, 60.0f) * 1.6f, goal);
        }
    }

    // 회복: 많이 다쳤으면 가장 가까운 아군 기지로
    if (health < 0.4f)
    {
        int nearestHex = -1;
        float nearest = std::numeric_limits<float>::max();
        for (const auto [baseEntity, base, faction, basePosition] : world.registry.view<Base, Faction, Position>().each())
        {
            const float distance = Distance(position, basePosition.value);
            if (faction.id == self && distance < nearest)
            {
                nearest = distance;
                nearestHex = base.hex;
            }
        }
        if (nearestHex >= 0)
        {
            Consider(best, AiAction::Recover, nearestHex, (1.0f - health) * 1.1f, goal);
        }
    }
    return best;
}

