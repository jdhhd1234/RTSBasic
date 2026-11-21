#include "sim/hex.hpp"

#include <cmath>
#include <cstdlib>
#include <numbers>

namespace
{
    constexpr float Sqrt3 = std::numbers::sqrt3_v<float>;

    void OffsetToAxial(HexCoord coord, int& q, int& r)
    {
        q = coord.col - (coord.row - (coord.row & 1)) / 2;
        r = coord.row;
    }
}

Vec2 HexCenter(HexCoord coord, float hexDistance)
{
    return {
        hexDistance * (static_cast<float>(coord.col) + 0.5f * static_cast<float>(coord.row & 1)),
        hexDistance * (Sqrt3 / 2.0f) * static_cast<float>(coord.row)
    };
}

HexCoord WorldToHex(Vec2 position, float hexDistance)
{
    const float size = hexDistance / Sqrt3;
    const float qf = (Sqrt3 / 3.0f * position.x - position.y / 3.0f) / size;
    const float rf = (2.0f / 3.0f * position.y) / size;
    const float sf = -qf - rf;

    float rq = std::round(qf);
    float rr = std::round(rf);
    const float rs = std::round(sf);
    const float dq = std::abs(rq - qf);
    const float dr = std::abs(rr - rf);
    const float ds = std::abs(rs - sf);

    if (dq > dr && dq > ds)
    {
        rq = -rr - rs;
    }
    else if (dr > ds)
    {
        rr = -rq - rs;
    }

    const int q = static_cast<int>(rq);
    const int r = static_cast<int>(rr);
    return { q + (r - (r & 1)) / 2, r };
}

int HexDistance(HexCoord a, HexCoord b)
{
    int aq = 0;
    int ar = 0;
    int bq = 0;
    int br = 0;
    OffsetToAxial(a, aq, ar);
    OffsetToAxial(b, bq, br);
    const int dq = aq - bq;
    const int dr = ar - br;
    return (std::abs(dq) + std::abs(dr) + std::abs(dq + dr)) / 2;
}

std::array<HexCoord, 6> HexNeighbors(HexCoord c)
{
    if ((c.row & 1) == 0)
    {
        return { { { c.col + 1, c.row }, { c.col, c.row - 1 }, { c.col - 1, c.row - 1 },
                   { c.col - 1, c.row }, { c.col - 1, c.row + 1 }, { c.col, c.row + 1 } } };
    }
    return { { { c.col + 1, c.row }, { c.col + 1, c.row - 1 }, { c.col, c.row - 1 },
               { c.col - 1, c.row }, { c.col, c.row + 1 }, { c.col + 1, c.row + 1 } } };
}

float Distance(Vec2 a, Vec2 b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}
