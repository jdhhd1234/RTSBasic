#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class Terrain : std::uint8_t { Sea, Plain, Forest, Mountain, River, City, Swamp, Desert, Plateau, Count };
enum class Weather : std::uint8_t { Clear, Rain, Snow, Fog, Count };
enum class BaseType : std::uint8_t { Hq, Corps, Division, Count };
enum class UnitClass : std::uint8_t { Inf, Mech, Armor, Arty, Aa, At, Air, Support, Count };
// AllTerrain, Regeneration은 샌드박스 커스텀 대대용 효과.
enum class Effect : std::uint8_t { None, Engineer, Recon, Signals, Medical, Transport, Maintenance, Cbrn, Mountain, Police, Ew, AllTerrain, Regeneration, Count };
// 광산에서 캐는 자원. 석유는 모든 곳에 쓰이고, 희토류는 전자전 장비, 망간은 고급 장갑에 필수.
enum class Resource : std::uint8_t { Oil, Iron, Nickel, Quartz, RareEarth, Manganese, Count };

template <typename E>
constexpr std::size_t ToIndex(E value)
{
    return static_cast<std::size_t>(value);
}

constexpr std::size_t TerrainCount = ToIndex(Terrain::Count);
constexpr std::size_t WeatherCount = ToIndex(Weather::Count);
constexpr std::size_t BaseTypeCount = ToIndex(BaseType::Count);
constexpr std::size_t UnitClassCount = ToIndex(UnitClass::Count);
constexpr std::size_t EffectCount = ToIndex(Effect::Count);
constexpr std::size_t ResourceCount = ToIndex(Resource::Count);

using ResourceAmounts = std::array<float, ResourceCount>;

// Lua 데이터의 id 문자열. enum 순서와 같아야 한다.
constexpr std::array<const char*, TerrainCount> TerrainNames = { "sea", "plain", "forest", "mountain", "river", "city", "swamp", "desert", "plateau" };
constexpr std::array<const char*, WeatherCount> WeatherNames = { "clear", "rain", "snow", "fog" };
constexpr std::array<const char*, BaseTypeCount> BaseTypeNames = { "hq", "corps", "division" };
constexpr std::array<const char*, UnitClassCount> UnitClassNames = { "inf", "mech", "armor", "arty", "aa", "at", "air", "support" };
constexpr std::array<const char*, EffectCount> EffectNames = { "none", "engineer", "recon", "signals", "medical", "transport", "maintenance", "cbrn", "mountain", "police", "ew", "all_terrain", "regen" };
constexpr std::array<const char*, ResourceCount> ResourceNames = { "oil", "iron", "nickel", "quartz", "rare_earth", "manganese" };

constexpr std::uint32_t EffectBit(Effect effect)
{
    return 1u << ToIndex(effect);
}

constexpr bool HasEffect(std::uint32_t mask, Effect effect)
{
    return (mask & EffectBit(effect)) != 0;
}

struct BattalionDef
{
    std::string id;
    std::string name;
    std::string app6;
    UnitClass unitClass = UnitClass::Inf;
    Effect effect = Effect::None;
    float attack = 0.0f;
    float defense = 0.0f;
    float speed = 0.0f;   // km/초
    float range = 0.0f;   // km
    float hp = 0.0f;
    int factory = 0;               // GameData::factories 인덱스. 이 공장이 있어야 생산 가능
    ResourceAmounts materials{};   // 생산 1회에 드는 자원
    float manpower = 0.0f;         // 생산 1회에 드는 인력
};

// 공장 종류. 건설에 build_days(게임 일수)가 걸린다.
struct FactoryDef
{
    std::string id;
    std::string name;
    float buildDays = 1460.0f;
    ResourceAmounts cost{};        // 건설 비용
    ResourceAmounts input{};       // 가동 중 초당 소모 (군수품 공장)
    float munitions = 0.0f;        // 가동 중 초당 군수품 생산
};

// 광산 배치 규칙: 어떤 지형에 어떤 자원이 얼마나 나오는가.
struct MineRuleDef
{
    Resource resource = Resource::Oil;
    std::uint32_t terrainMask = 0;   // 1 << Terrain
    float rate = 0.0f;               // 초당 채굴량
    float weight = 1.0f;             // 배치 가중치 (낮을수록 희귀)
};

struct TerrainDef
{
    float move = 1.0f;
    float defense = 1.0f;
    float armorAttack = 1.0f;
    std::array<unsigned char, 3> color{};
};

struct WeatherDef
{
    float move = 1.0f;
    float range = 1.0f;
    float attack = 1.0f;
    float variance = 0.0f;
    float weight = 1.0f;
};

struct BaseDef
{
    float hp = 0.0f;
    float defense = 0.0f;
    float attack = 0.0f;
    float penalty = 1.0f;
};

struct StageDef
{
    std::string name;
    int playerDivisions = 0;
    int aiDivisions = 0;
    float aiIncome = 1.0f;
};

struct DifficultyDef
{
    std::string name;
    float aiIncome = 1.0f;
    float thinkSeconds = 5.0f;
    int group = 1;
};

// battalions에는 GameData::battalions의 인덱스가 들어간다.
struct TemplateDef
{
    std::string name;
    std::vector<int> battalions;
};

struct GameConfig
{
    float hexDistanceKm = 20.0f;
    int mapCols = 60;
    int mapRows = 69;
    int mapSeed = 1234;
    int mapRivers = 6;
    int mapCities = 24;
    float tickHz = 10.0f;
    float matchSeconds = 1200.0f;
    float engageKm = 20.0f;
    float territoryClearKm = 30.0f;
    int baseCaptureRadius = 2;
    // 경제 (problem.md 2026/11/21)
    float daysPerSecond = 3.0f;      // 게임 달력: 시뮬레이션 1초 = 3일
    int startYear = 2030;
    int minesPerSide = 12;
    float startManpower = 20000.0f;
    float startMunitions = 150.0f;
    float conscriptManpower = 12000.0f;
    float conscriptPressure = 0.3f;  // 징집 1회당 징집 압력 증가
    float pressureDecay = 0.002f;    // 초당 압력 감소
    float pressureSafe = 0.5f;       // 이 이상이면 징집거부·탈영·군사질 하락
    float refusalChance = 0.9f;      // 압력 초과분 1당 징집거부 확률
    float desertionChance = 0.08f;   // 압력 초과분 1당 초당 탈영 확률
    float qualityLoss = 0.06f;       // 압력 초과 상태 징집 1회당 군사질 하락
    float qualityRecover = 0.002f;   // 압력이 안전할 때 초당 군사질 회복
    float qualityMin = 0.5f;
    float upkeepOil = 0.02f;         // 사단 1개 기본 석유 유지비/초
    float upkeepGrowth = 0.05f;      // 사단 1개마다 유지비 배율 증가 (많을수록 비싸진다)
    int overstretchDivisions = 12;   // 이 수를 넘으면 지휘통제 사고가 생긴다
    float commandChance = 0.01f;     // 초과 사단 1개당 초당 사고 확률
    float combatMunitions = 0.05f;   // 교전 중 사단 1개 군수품 소모/초
    float noMunitionsAttack = 0.4f;  // 군수품이 없을 때 공격력 배율
    float noFuelAttack = 0.7f;       // 석유가 없을 때 공격력 배율
    float noFuelSpeed = 0.5f;        // 석유가 없을 때 이동속도 배율
    float productionSeconds = 15.0f;
    float retreatThreshold = 0.3f;
    float retreatThresholdPolice = 0.2f;
    float capturedBaseHp = 0.3f;
    float supplyFullKm = 250.0f;
    float supplyMinKm = 650.0f;
    float supplyMin = 0.5f;
    float transportDivisor = 1.5f;
    float damageScale = 0.15f;
    float defenseK = 20.0f;
    float hpFactorMin = 0.5f;
    int weatherRegions = 4;
    float weatherMinSeconds = 60.0f;
    float weatherMaxSeconds = 120.0f;
    int maxTemplates = 6;
    int maxBattalions = 4;
    int maxSameBattalion = 2;
    float recoverIdleSeconds = 1.0f;
    float medicalRate = 0.01f;
    float maintenanceRate = 0.005f;
    float reconAttack = 0.1f;
    float signalsBonus = 0.1f;
    float mountainAttack = 0.3f;
    float ewPenalty = 0.1f;
    float aiDefenseKm = 100.0f;
    float baseRegen = 1.0f;          // 적이 없을 때 기지 체력 회복/초
    float regenRate = 0.01f;         // regen 효과: 교전 중에도 최대 체력 비율/초
    float mapMinKm = 400.0f;
    float mapMaxKm = 1200.0f;
};

struct GameData
{
    GameConfig config;
    std::vector<BattalionDef> battalions;
    std::vector<FactoryDef> factories;
    std::vector<MineRuleDef> mineRules;
    ResourceAmounts startStock{};
    std::vector<int> startFactories;   // factories 인덱스 목록 (HQ·군단 기지에 배치)
    std::array<TerrainDef, TerrainCount> terrain{};
    std::array<WeatherDef, WeatherCount> weather{};
    std::array<BaseDef, BaseTypeCount> bases{};
    std::array<std::array<float, UnitClassCount>, UnitClassCount> matchup{};   // [공격][표적]
    std::vector<StageDef> stages;
    std::vector<DifficultyDef> difficulties;
    std::vector<TemplateDef> defaultTemplates;
    std::vector<TemplateDef> aiTemplates;
};
