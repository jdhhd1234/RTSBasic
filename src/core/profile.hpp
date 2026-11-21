#pragma once

#include "core/game_data.hpp"

#include <string>
#include <vector>

// 로컬에 저장하는 플레이어 진행 상태.
struct Profile
{
    int clearedStages = 0;
    std::string language = "en";   // UI 언어 코드 (data/lang/<code>.lua)
    std::vector<TemplateDef> templates;
};

// 파일이 없거나 깨졌으면 기본 템플릿으로 채운다.
Profile LoadProfile(const std::string& path, const GameData& data);

bool SaveProfile(const std::string& path, const Profile& profile, const GameData& data);
