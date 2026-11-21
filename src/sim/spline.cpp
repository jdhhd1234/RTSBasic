#include "sim/spline.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    Vec2 Lerp(Vec2 a, Vec2 b, float t)
    {
        return { std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t) };
    }

    double SegmentDistanceSquared(Vec2 point, Vec2 start, Vec2 end)
    {
        const double dx = static_cast<double>(end.x) - start.x;
        const double dy = static_cast<double>(end.y) - start.y;
        const double px = static_cast<double>(point.x) - start.x;
        const double py = static_cast<double>(point.y) - start.y;
        const double lengthSquared = dx * dx + dy * dy;
        const double t = lengthSquared > 0.0 ? std::clamp((px * dx + py * dy) / lengthSquared, 0.0, 1.0) : 0.0;
        const double x = px - t * dx;
        const double y = py - t * dy;
        return x * x + y * y;
    }
}

std::vector<Vec2> SampleCatmullRom(const std::vector<Vec2>& points, int segmentsPerSpan, bool closed)
{
    const std::size_t count = points.size();
    if (count <= 2)
    {
        return points;
    }

    segmentsPerSpan = std::max(1, segmentsPerSpan);
    const std::size_t spans = closed ? count : count - 1;
    std::vector<Vec2> samples;
    samples.reserve(spans * static_cast<std::size_t>(segmentsPerSpan) + 1);
    samples.push_back(points.front());

    for (std::size_t i = 0; i < spans; ++i)
    {
        const Vec2 p1 = points[i];
        const Vec2 p2 = points[(i + 1) % count];
        const float d1 = std::sqrt(Distance(p1, p2));
        if (d1 == 0.0f)
        {
            samples.insert(samples.end(), static_cast<std::size_t>(segmentsPerSpan), p2);
            continue;
        }

        Vec2 p0 = (closed || i > 0) ? points[(i + count - 1) % count] : p1;
        Vec2 p3 = (closed || i + 2 < count) ? points[(i + 2) % count] : p2;
        float d0 = std::sqrt(Distance(p0, p1));
        float d2 = std::sqrt(Distance(p2, p3));
        // 열린 끝과 겹친 제어점에선 현재 구간 방향으로 바깥 점을 외삽한다.
        if (d0 == 0.0f)
        {
            p0 = Lerp(p1, p2, -1.0f);
            d0 = d1;
        }
        if (d2 == 0.0f)
        {
            p3 = Lerp(p2, p1, -1.0f);
            d2 = d1;
        }

        for (int step = 1; step < segmentsPerSpan; ++step)
        {
            const float t = static_cast<float>(step) / static_cast<float>(segmentsPerSpan);
            const float knot = t * d1;
            const Vec2 a0 = Lerp(p1, p0, -knot / d0);
            const Vec2 a1 = Lerp(p1, p2, t);
            const Vec2 a2 = Lerp(p2, p3, (knot - d1) / d2);
            const Vec2 b0 = Lerp(a0, a1, (d0 + knot) / (d0 + d1));
            const Vec2 b1 = Lerp(a1, a2, knot / (d1 + d2));
            samples.push_back(Lerp(b0, b1, t));
        }
        samples.push_back(p2);
    }
    return samples;
}

std::vector<Vec2> SampleCubicBezier(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, int segments)
{
    segments = std::max(1, segments);
    std::vector<Vec2> samples;
    samples.reserve(static_cast<std::size_t>(segments) + 1);
    samples.push_back(p0);
    for (int step = 1; step < segments; ++step)
    {
        const float t = static_cast<float>(step) / static_cast<float>(segments);
        const Vec2 a = Lerp(p0, p1, t);
        const Vec2 b = Lerp(p1, p2, t);
        const Vec2 c = Lerp(p2, p3, t);
        samples.push_back(Lerp(Lerp(a, b, t), Lerp(b, c, t), t));
    }
    samples.push_back(p3);
    return samples;
}

std::vector<Vec2> SampleArc(Vec2 from, Vec2 to, float bend, int segments)
{
    const Vec2 side{ (from.y - to.y) * bend, (to.x - from.x) * bend };
    const Vec2 p1 = Lerp(from, to, 1.0f / 3.0f);
    const Vec2 p2 = Lerp(from, to, 2.0f / 3.0f);
    return SampleCubicBezier(from, { p1.x + side.x, p1.y + side.y }, { p2.x + side.x, p2.y + side.y }, to, segments);
}

std::vector<Vec2> SimplifyPolyline(const std::vector<Vec2>& points, float tolerance)
{
    if (points.size() <= 2)
    {
        return points;
    }

    const double threshold = std::max(0.0f, tolerance);
    std::vector<char> keep(points.size(), 0);
    keep.front() = keep.back() = 1;
    std::vector<std::pair<std::size_t, std::size_t>> spans{ { 0, points.size() - 1 } };
    while (!spans.empty())
    {
        const auto [start, end] = spans.back();
        spans.pop_back();
        double furthestDistance = threshold * threshold;
        std::size_t furthest = start;
        for (std::size_t i = start + 1; i < end; ++i)
        {
            const double distance = SegmentDistanceSquared(points[i], points[start], points[end]);
            if (distance > furthestDistance)
            {
                furthestDistance = distance;
                furthest = i;
            }
        }
        if (furthest != start)
        {
            keep[furthest] = 1;
            spans.emplace_back(start, furthest);
            spans.emplace_back(furthest, end);
        }
    }

    std::vector<Vec2> result;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        if (keep[i] != 0)
        {
            result.push_back(points[i]);
        }
    }
    return result;
}
