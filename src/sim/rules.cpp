#include "sim/rules.hpp"

#include <algorithm>

float SupplyFactor(float distanceKm, bool transport, const GameConfig& config)
{
    const float distance = transport ? distanceKm / config.transportDivisor : distanceKm;
    if (distance <= config.supplyFullKm)
    {
        return 1.0f;
    }
    if (distance >= config.supplyMinKm)
    {
        return config.supplyMin;
    }
    const float t = (distance - config.supplyFullKm) / (config.supplyMinKm - config.supplyFullKm);
    return 1.0f + (config.supplyMin - 1.0f) * t;
}

float TerrainMoveMultiplier(Terrain terrain, std::uint32_t effects, const GameData& data)
{
    const float move = data.terrain[ToIndex(terrain)].move;
    if (move <= 0.0f)
    {
        return 0.0f;
    }
    if (HasEffect(effects, Effect::AllTerrain))
    {
        return 1.0f;
    }
    if (HasEffect(effects, Effect::Mountain) && (terrain == Terrain::Mountain || terrain == Terrain::Plateau))
    {
        return 1.0f;
    }
    if (HasEffect(effects, Effect::Engineer) && move < 1.0f)
    {
        return 1.0f - (1.0f - move) * 0.5f;
    }
    return move;
}

float WeatherMultiplier(float multiplier, std::uint32_t effects)
{
    if (HasEffect(effects, Effect::Cbrn) && multiplier < 1.0f)
    {
        return 1.0f - (1.0f - multiplier) * 0.5f;
    }
    return multiplier;
}

float MatchupAgainst(UnitClass attacker, const std::vector<int>& targetBattalions, const GameData& data)
{
    if (targetBattalions.empty())
    {
        return 1.0f;
    }
    float sum = 0.0f;
    for (const int b : targetBattalions)
    {
        const UnitClass target = data.battalions[static_cast<std::size_t>(b)].unitClass;
        sum += data.matchup[ToIndex(attacker)][ToIndex(target)];
    }
    return sum / static_cast<float>(targetBattalions.size());
}

float RetreatThreshold(std::uint32_t effects, const GameConfig& config)
{
    return HasEffect(effects, Effect::Police) ? config.retreatThresholdPolice : config.retreatThreshold;
}

float HpFactor(float hp, float maxHp, const GameConfig& config)
{
    if (maxHp <= 0.0f)
    {
        return config.hpFactorMin;
    }
    const float ratio = std::clamp(hp / maxHp, 0.0f, 1.0f);
    return config.hpFactorMin + (1.0f - config.hpFactorMin) * ratio;
}
