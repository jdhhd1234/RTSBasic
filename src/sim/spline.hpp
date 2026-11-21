#pragma once

#include "sim/hex.hpp"

#include <vector>

// 지도 곡선 기하. 전선, 해안선, 강, 도로, 보급선을 헥스 폴리곤 대신 곡선으로 그리기 위한 순수 계산.
// raylib에 의존하지 않는다 (좌표는 월드 km).

// centripetal Catmull-Rom 스플라인을 폴리라인으로 샘플링한다.
// 모든 제어점을 지난다. closed면 마지막 점과 첫 점도 잇는다.
// 점이 2개 이하면 그대로 돌려준다.
std::vector<Vec2> SampleCatmullRom(const std::vector<Vec2>& points, int segmentsPerSpan, bool closed);

// 3차 베지어 곡선. 양 끝을 포함해 segments + 1개 점.
std::vector<Vec2> SampleCubicBezier(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, int segments);

// from → to를 잇는 휜 베지어 (보급선용). bend는 길이 대비 옆으로 휘는 비율 (0이면 직선).
std::vector<Vec2> SampleArc(Vec2 from, Vec2 to, float bend, int segments);

// Douglas-Peucker 단순화. 양 끝 점은 항상 남는다.
std::vector<Vec2> SimplifyPolyline(const std::vector<Vec2>& points, float tolerance);
