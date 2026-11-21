#include "sim/profiler.hpp"

#include <algorithm>

namespace
{
    constexpr double AverageWeight = 0.05;   // 최근 ~20회 정도가 평균을 주도한다
}

void RecordSample(Profiler& profiler, ProfileSection section, double ms)
{
    ProfileStat& stat = profiler.stats[static_cast<std::size_t>(section)];
    stat.lastMs = ms;
    stat.avgMs = stat.calls == 0 ? ms : stat.avgMs + (ms - stat.avgMs) * AverageWeight;
    stat.maxMs = std::max(stat.maxMs, ms);
    stat.totalMs += ms;
    ++stat.calls;
}

void ResetProfiler(Profiler& profiler)
{
    const bool enabled = profiler.enabled;
    profiler = Profiler{};
    profiler.enabled = enabled;
}

ProfileScope::ProfileScope(Profiler& profiler, ProfileSection section)
    : profiler(profiler), section(section), active(profiler.enabled)
{
    if (active)
    {
        start = std::chrono::steady_clock::now();
    }
}

ProfileScope::~ProfileScope()
{
    if (active)
    {
        RecordSample(profiler, section, ElapsedMs());
    }
}

double ProfileScope::ElapsedMs() const
{
    if (!active)
    {
        return 0.0;
    }
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
