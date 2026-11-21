#pragma once

#include "core/game_data.hpp"
#include "sim/simulation.hpp"

#include <string>
#include <vector>

// 샌드박스 시나리오 (data/sandbox/*.lua). 형식은 data/sandbox/README.txt 참고.
struct Scenario
{
    std::string path;
    std::string name;
    std::string description;
    std::string mapLabel;
    std::vector<std::string> customBattalions;   // 시나리오가 추가한 대대 이름
    GameData data;                               // 기본 데이터 + 커스텀 대대
    MatchSetup setup;
};

// playerTemplates: 청군 templates가 비어 있을 때 쓰는 프로필 템플릿.
bool LoadScenario(const std::string& path, const GameData& base, const std::vector<TemplateDef>& playerTemplates,
                  Scenario& out, std::string& error);
