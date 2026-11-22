#pragma once

#include <array>
#include <chrono>
#include <cstdint>

// 샌드박스 프로파일러. 시스템별 실행 시간과 AI 판단 통계를 모은다.
// enabled가 false면 ProfileScope는 시계를 읽지 않는다 (캠페인 비용 0에 가깝게).

enum class ProfileSection : std::uint8_t
{
    // 시뮬레이션 틱 (StepWorld)
    Occupancy, Weather, Ai, Movement, Battalions, Combat, Territory, Economy, Victory,
    // AI 내부 (Ai 안에 포함)
    AiBlackboard, AiHtn, AiExecute, AiUtility,
    // 화면 (프레임)
    DrawMap, DrawUnits, DrawUi,
    Count
};

constexpr std::size_t ProfileSectionCount = static_cast<std::size_t>(ProfileSection::Count);

constexpr std::array<const char*, ProfileSectionCount> ProfileSectionNames = {
    "occupancy", "weather", "ai", "movement", "battalions", "combat", "territory", "economy", "victory",
    "ai.blackboard", "ai.htn", "ai.execute", "ai.utility",
    "draw.map", "draw.units", "draw.ui",
};

// 틱 합계(%)에 들어가는 최상위 시뮬레이션 구간인가.
constexpr bool IsTickSection(ProfileSection section)
{
    return section <= ProfileSection::Victory;
}

struct ProfileStat
{
    double lastMs = 0.0;
    double avgMs = 0.0;      // 지수 이동 평균
    double maxMs = 0.0;
    double totalMs = 0.0;
    std::int64_t calls = 0;
};

// 진영별 AI 판단 통계.
struct AiProfile
{
    std::int64_t thinks = 0;
    double lastThinkMs = 0.0;
    double maxThinkMs = 0.0;
    std::array<std::int64_t, 6> actions{};   // AiAction별 선택 횟수
    std::int64_t moveOrders = 0;             // 실제로 내린 이동 명령
};

struct Profiler
{
    bool enabled = false;
    std::int64_t ticks = 0;
    std::array<ProfileStat, ProfileSectionCount> stats{};
};

void RecordSample(Profiler& profiler, ProfileSection section, double ms);

// 통계만 지우고 enabled는 유지한다.
void ResetProfiler(Profiler& profiler);

// 생성부터 소멸까지 시간을 section에 기록하는 RAII 타이머.
class ProfileScope
{
public:
    ProfileScope(Profiler& profiler, ProfileSection section);
    ~ProfileScope();

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;
    ProfileScope(ProfileScope&&) = delete;
    ProfileScope& operator=(ProfileScope&&) = delete;

    // 지금까지 걸린 시간 (ms). 비활성이면 0.
    double ElapsedMs() const;

private:
    Profiler& profiler;
    ProfileSection section;
    bool active;
    std::chrono::steady_clock::time_point start;
};
