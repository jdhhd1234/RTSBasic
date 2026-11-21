#include "ai/ai.hpp"

#include <algorithm>
#include <limits>

// HTN: 복합 과업 하나 = 함수 하나. 전제조건이 맞는 메서드의 원시 과업을 plan에 붙인다.
// shortcut: 백트래킹 없이 메서드 전제조건을 순서대로 검사. 다단계 작전(우회·양동)이 필요하면 계획 탐색으로 확장.

namespace
{
    // 방어보다 공격을 시작하는 전력비. 난이도 group이 클수록 공격적.
    float AttackRatio(const AiBlackboard& board, const DifficultyDef& difficulty)
    {
        const float ratio = 1.3f - 0.15f * static_cast<float>(difficulty.group);
        return board.timeLeftRatio < 0.4f ? ratio - 0.2f : ratio;   // 시간이 얼마 안 남으면 더 공세적으로
    }

    // 경제: 인력 확보 -> 공장 건설 -> 생산. 각각 독립 메서드라 한 주기에 여러 개가 나올 수 있다.
    void DecomposeEconomy(const AiBlackboard& board, std::vector<AiTask>& plan)
    {
        if (board.needManpower && board.canConscript)
        {
            plan.push_back({ .type = AiTaskType::Conscript });
        }
        if (board.canBuildFactory)
        {
            plan.push_back({ .type = AiTaskType::BuildFactory, .factoryType = board.factoryToBuild });
        }
        // 석유 수지가 적자면 사단을 늘리지 않는다 (유지비가 사단 수에 따라 커진다).
        if (board.canProduce && board.canAffordUpkeep)
        {
            plan.push_back({ .type = AiTaskType::Produce });
        }
    }

    void DecomposeDefense(const AiBlackboard& board, std::vector<AiTask>& plan)
    {
        for (const AiThreat& threat : board.threats)
        {
            // 아군이 이미 충분히 있거나, 본부가 아닌데 적이 약하면 사단 판단에 맡긴다.
            if (threat.enemyStrength > threat.ownStrength || threat.hq)
            {
                plan.push_back({ .type = AiTaskType::Defend, .hex = threat.baseHex });
            }
        }
    }

    int PickObjective(const AiBlackboard& board)
    {
        // 기존 목표가 아직 유효하면 유지한다.
        for (const AiObjective& objective : board.objectives)
        {
            if (objective.hex == board.attackHex)
            {
                return objective.hex;
            }
        }
        int best = -1;
        float bestCost = std::numeric_limits<float>::max();
        for (const AiObjective& objective : board.objectives)
        {
            const float cost = Distance(board.center, objective.position) / 300.0f
                + objective.guardStrength / (board.ownStrength + 1.0f) * 3.0f
                - (objective.hq ? 0.5f : 0.0f);
            if (cost < bestCost)
            {
                bestCost = cost;
                best = objective.hex;
            }
        }
        return best;
    }

    void DecomposeOffense(const AiBlackboard& board, const DifficultyDef& difficulty, std::vector<AiTask>& plan)
    {
        const int objective = PickObjective(board);
        const float ratio = board.ownStrength / std::max(1.0f, board.enemyStrength);
        const bool gathered = board.rallyHex >= 0 && board.rallyGathered >= 0.6f * board.ownStrength;

        // 메서드 1: 총공격 — 전력 우세이거나 집결을 마쳤을 때
        if (objective >= 0 && (ratio >= AttackRatio(board, difficulty) || (gathered && ratio >= 0.6f)))
        {
            plan.push_back({ .type = AiTaskType::ChooseObjective, .hex = objective });
            plan.push_back({ .type = AiTaskType::SetPosture, .posture = AiPosture::Attack });
            return;
        }
        // 메서드 2: 집결 — 목표에 가장 가까운 아군 기지에 모은다
        if (objective >= 0 && ratio >= 0.6f)
        {
            plan.push_back({ .type = AiTaskType::ChooseObjective, .hex = objective });
            plan.push_back({ .type = AiTaskType::SetRally });   // hex는 실행 단계에서 목표 기준으로 고른다
            plan.push_back({ .type = AiTaskType::SetPosture, .posture = AiPosture::Regroup });
            return;
        }
        // 메서드 3: 수세
        plan.push_back({ .type = AiTaskType::SetPosture, .posture = AiPosture::Hold });
    }
}

std::vector<AiTask> PlanCommand(const AiBlackboard& board, const DifficultyDef& difficulty)
{
    // 루트 과업 "총사령부 지휘" = 경제 -> 방어 -> 공세
    std::vector<AiTask> plan;
    DecomposeEconomy(board, plan);
    DecomposeDefense(board, plan);
    DecomposeOffense(board, difficulty, plan);
    return plan;
}
