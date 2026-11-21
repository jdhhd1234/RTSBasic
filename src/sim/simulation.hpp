#pragma once

#include "core/game_data.hpp"
#include "sim/world.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum class CommandType : std::uint8_t { Move, Retreat, Produce, Surrender, Conscript, BuildFactory };

// 프론트엔드와 AI가 시뮬레이션 상태를 바꾸는 유일한 통로.
struct Command
{
    CommandType type = CommandType::Move;
    entt::entity entity = entt::null;
    int targetHex = -1;
    int faction = PlayerFaction;
    int templateIndex = -1;
    int factoryType = -1;            // BuildFactory: GameData::factories 인덱스
};

struct SideSetup
{
    std::string name;
    bool ai = false;
    int difficulty = 1;
    float incomeMultiplier = 1.0f;
    std::vector<TemplateDef> templates;
    std::vector<int> initialDivisions;   // templates 인덱스
};

struct BasePlacement
{
    int faction = 0;
    BaseType type = BaseType::Division;
    HexCoord coord;
};

// 한 판을 시작하는 데 필요한 모든 설정. 캠페인과 샌드박스가 같은 구조를 쓴다.
struct MatchSetup
{
    std::string title;
    int stage = -1;
    float matchSeconds = 1200.0f;
    bool customMap = false;          // true면 map을 그대로, false면 절차 생성
    HexMap map;
    int mapSeed = 1234;
    int mapCols = 60;
    int mapRows = 69;
    std::vector<BasePlacement> bases;   // 비어 있으면 기본 배치
    std::array<SideSetup, FactionCount> sides;
};

MatchSetup CampaignSetup(const GameData& data, int stage, int difficulty,
                         const std::vector<TemplateDef>& playerTemplates, bool botBattle);

// 절차 생성 맵 크기를 km로 지정 (mapMinKm~mapMaxKm로 제한).
void SetGeneratedMapSize(MatchSetup& setup, const GameConfig& config, float widthKm, float heightKm);

void InitWorld(World& world, const GameData& data, const MatchSetup& setup, std::uint32_t seed);

void ApplyCommand(World& world, const GameData& data, const Command& command);

// 고정 틱 하나 진행.
void StepWorld(World& world, const GameData& data, float dt);

entt::entity SpawnDivision(World& world, const GameData& data, int faction, const TemplateDef& templ, Vec2 position);

void PushEvent(World& world, std::string text, EventKind kind);
