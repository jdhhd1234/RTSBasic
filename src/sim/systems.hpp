#pragma once

#include "core/game_data.hpp"
#include "sim/world.hpp"

#include <cstdint>

// 전투 판정용 대상 목록 항목.
struct CombatTarget
{
    entt::entity entity = entt::null;
    int faction = 0;
    Vec2 position;
    int hex = 0;
    bool isBase = false;
};

// 시스템 (simulation.cpp의 StepWorld가 순서대로 호출)
void RebuildOccupancy(World& world);
void UpdateWeather(World& world, const GameData& data, float dt);
void UpdateAI(World& world, const GameData& data, float dt);
void UpdateMovement(World& world, const GameData& data, float dt);
void UpdateCombat(World& world, const GameData& data, float dt);
void UpdateTerritory(World& world, const GameData& data);
void CheckVictory(World& world, const GameData& data);

// 공용 헬퍼
void RollWeather(World& world, const GameData& data, WeatherRegion& region);
void StartRetreat(World& world, const GameData& data, entt::entity entity);
void CaptureBase(World& world, const GameData& data, entt::entity base, int captor);
int HexAt(const World& world, Vec2 position);
bool IsHexBlockedFor(const World& world, int hex, int faction);
float DivisionSupply(const World& world, const GameData& data, Vec2 position, int faction, std::uint32_t effects);
Weather WeatherAtHex(const World& world, int hex);

