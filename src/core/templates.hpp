#pragma once

#include "core/game_data.hpp"

#include <string>

struct TemplateSummary
{
    float attackSum = 0.0f;
    float defenseAvg = 0.0f;
    float speed = 0.0f;      // 가장 느린 대대
    float maxRange = 0.0f;
    float hp = 0.0f;
};

// 규칙 위반이면 이유를, 정상이면 빈 문자열을 돌려준다.
std::string ValidateTemplate(const TemplateDef& templ, const GameData& data);

TemplateSummary SummarizeTemplate(const TemplateDef& templ, const GameData& data);

// 사단 하나를 생산하는 데 필요한 것.
struct TemplateNeeds
{
    ResourceAmounts materials{};
    float manpower = 0.0f;
    std::vector<int> factories;   // 필요한 공장 종류 (중복 없음, 오름차순)
};

TemplateNeeds GetTemplateNeeds(const TemplateDef& templ, const GameData& data);

// "iron 40, oil 20" 같은 요약. 0인 자원은 뺀다.
std::string ResourceText(const ResourceAmounts& amounts);

// 대대 id로 인덱스를 찾는다. 없으면 -1.
int BattalionIndex(const GameData& data, const std::string& id);
