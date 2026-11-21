#pragma once

#include "sim/world.hpp"

#include <string>

// 프로파일러 결과를 사람이 읽는 표로 만든다 (오버레이 저장, minrts_profile 출력).
// wallSeconds > 0이면 실제 시간 대비 시뮬레이션 배속도 적는다.
std::string ProfileReport(const World& world, double wallSeconds = 0.0);

// 틱 구간(IsTickSection) total 합계 (ms). %tick 계산용.
double TickTotalMs(const Profiler& profiler);
