#pragma once

#include "core/game_data.hpp"
#include "ai/ai_types.hpp"
#include "sim/map.hpp"
#include "sim/map_features.hpp"
#include "sim/profiler.hpp"

#include <entt/entt.hpp>

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

constexpr int PlayerFaction = 0;   // 청군. AI vs AI에서는 청군도 AI가 조종한다.
constexpr int AiFaction = 1;       // 적군
constexpr int FactionCount = 2;

// 진영이 가진 공장 하나. daysLeft가 0 이하면 가동 중.
struct FactoryState
{
    int type = 0;                    // GameData::factories 인덱스
    int hex = 0;                     // 있는 기지 헥스. 기지를 잃으면 파괴된다
    float daysLeft = 0.0f;
};

// 지도 위 광산. 소유자는 헥스 소유자. 아군 영토로 공장까지 이어져야 채굴량이 들어온다.
struct Mine
{
    int hex = 0;
    Resource resource = Resource::Oil;
    float rate = 0.0f;               // 초당
    bool connected = false;          // 마지막 경제 틱에 공장과 이어졌는가
    float reserve = -1.0f;           // 남은 매장량, 0 이하로 시작하면 무한. 다 캐면 고갈
};

struct FactionState
{
    std::string name;
    bool aiControlled = false;
    int difficulty = 1;
    float incomeMultiplier = 1.0f;   // 광산 채굴량 배율 (난이도·시나리오)
    float aiThinkTimer = 0.0f;
    float productionSpeed = 1.0f;    // 대기열 맨 앞 사단 기준 (공장 수 × 잃은 기지 패널티)

    // 경제
    ResourceAmounts stock{};
    ResourceAmounts mineRate{};      // 마지막 틱 초당 채굴량 (공장과 이어진 광산만)
    float manpower = 0.0f;
    float munitions = 0.0f;
    float upkeep = 0.0f;             // 초당 석유 유지비
    float draftPressure = 0.0f;      // 징집 압력. pressureSafe를 넘으면 징집거부·탈영·군사질 하락
    float quality = 1.0f;            // 군사질: 공격력 배율
    bool fuelShortage = false;
    float economyTimer = 0.0f;
    std::vector<FactoryState> factories;
    std::vector<int> queue;          // templates 인덱스
    float queueProgress = 0.0f;      // 0~1
    std::vector<TemplateDef> templates;
    int ownedHexes = 0;
    int spawnedDivisions = 0;
    AiBlackboard blackboard;         // aiControlled일 때 AI 지휘정보망
    AiProfile aiProfile;             // 프로파일러 AI 통계
};

struct WeatherRegion
{
    Weather weather = Weather::Clear;
    float timer = 0.0f;
};

enum class EventKind : std::uint8_t { Info, Good, Bad };   // 청군 기준

struct GameEvent
{
    float time = 0.0f;
    std::string text;
    EventKind kind = EventKind::Info;
};

// 한 판의 전체 상태. 시뮬레이션 시스템 함수들이 읽고 쓴다.
struct World
{
    entt::registry registry;
    HexMap map;
    MapFeatures features;
    std::vector<WeatherRegion> weather;
    std::vector<std::uint8_t> occupancy;     // 헥스별 진영 비트 (1 << faction)
    std::vector<entt::entity> baseAtHex;     // 헥스별 기지, 없으면 entt::null
    std::vector<Mine> mines;
    std::array<FactionState, FactionCount> factions;
    std::vector<GameEvent> events;
    std::mt19937 rng;
    std::string title;
    float time = 0.0f;
    float matchSeconds = 1200.0f;
    int stage = -1;                          // 캠페인 스테이지 인덱스, 샌드박스는 -1
    bool over = false;
    int winner = -1;
    std::string endReason;
    Profiler profiler;                       // 샌드박스 프로파일러 (기본 꺼짐)
};
