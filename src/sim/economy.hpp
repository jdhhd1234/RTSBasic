#pragma once

#include "core/game_data.hpp"
#include "sim/world.hpp"

#include <string>

// 경제 (problem.md 2026/11/21): 인력·공장·유지비·광산.
//  - 인력: 징집으로 얻는다. 압력이 높을 때 징집하면 징집거부·탈영·군사질 하락.
//  - 공장: 건설에 게임 시간 4년. 대대마다 필요한 공장 종류가 있다.
//  - 유지: 사단이 많을수록 사단당 석유 유지비가 커지고, 일정 수를 넘으면 지휘통제 사고.
//  - 광산: 아군 영토로 공장까지 이어진 광산만 채굴량이 들어온다.

// InitWorld가 기지 배치 뒤에 부른다.
void PlaceMines(World& world, const GameData& data);
void SetupFactionEconomy(World& world, const GameData& data, int faction);

// 1초마다 경제 틱 (채굴, 군수품 공장, 유지비, 징집 압력, 사고 이벤트, 공장 건설) + 매 틱 생산 대기열.
void UpdateEconomy(World& world, const GameData& data, float dt);

// 생산 가능하면 빈 문자열, 아니면 이유.
std::string ProductionBlocker(const FactionState& state, const GameData& data, const TemplateDef& templ);
// 건설 가능하면 빈 문자열, 아니면 이유.
std::string FactoryBlocker(const World& world, const GameData& data, int faction, int type);

// 명령 처리 (ApplyCommand가 부른다). 조건이 안 맞으면 아무것도 하지 않는다.
void Conscript(World& world, const GameData& data, int faction);
void StartFactory(World& world, const GameData& data, int faction, int type);

// 기지를 잃으면 그 헥스의 공장이 파괴된다.
void DestroyFactoriesAt(World& world, int hex);

int ActiveFactories(const FactionState& state, int type);
int FactoriesUnderConstruction(const FactionState& state, int type);

// 사단 수에 따른 초당 석유 유지비. 사단이 많을수록 사단당 비용도 커진다.
float UpkeepPerSecond(int divisions, const GameConfig& config);

// 군사질·군수품·석유 상태를 반영한 공격력 / 이동속도 배율.
float FactionAttackMultiplier(const FactionState& state, const GameConfig& config);
float FactionSpeedMultiplier(const FactionState& state, const GameConfig& config);

// "2034-07" 형식 게임 날짜.
std::string GameDateText(float seconds, const GameConfig& config);
