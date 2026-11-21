#pragma once

#include "core/game_data.hpp"

#include <cstdint>
#include <vector>

// 상태 없는 판정 공식들. 테스트하기 쉽게 World와 분리했다.

// 기지 거리 → 보급 배율. 250km 이내 1.0, 650km 이상 0.5, 사이는 선형.
float SupplyFactor(float distanceKm, bool transport, const GameConfig& config);

// 지형 이동 배율. 공병은 패널티 절반, 산악은 산·고원 패널티 없음, all_terrain은 패널티 없음(바다는 여전히 불가).
float TerrainMoveMultiplier(Terrain terrain, std::uint32_t effects, const GameData& data);

// 날씨 배율. 화생방은 패널티 절반.
float WeatherMultiplier(float multiplier, std::uint32_t effects);

// 공격 클래스가 표적 사단(대대 목록)에 주는 상성 배율 평균.
float MatchupAgainst(UnitClass attacker, const std::vector<int>& targetBattalions, const GameData& data);

float RetreatThreshold(std::uint32_t effects, const GameConfig& config);

// 사단 체력비에 따른 공격 배율.
float HpFactor(float hp, float maxHp, const GameConfig& config);
