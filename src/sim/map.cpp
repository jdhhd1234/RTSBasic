#include "sim/map.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <queue>
#include <random>
#include <utility>

namespace
{
    float Hash01(int x, int y, std::uint32_t seed)
    {
        std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
    }

    float SmoothNoise(float x, float y, std::uint32_t seed)
    {
        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const float tx = x - static_cast<float>(x0);
        const float ty = y - static_cast<float>(y0);
        const float sx = tx * tx * (3.0f - 2.0f * tx);
        const float sy = ty * ty * (3.0f - 2.0f * ty);

        const float a = Hash01(x0, y0, seed);
        const float b = Hash01(x0 + 1, y0, seed);
        const float c = Hash01(x0, y0 + 1, seed);
        const float d = Hash01(x0 + 1, y0 + 1, seed);
        const float top = a + (b - a) * sx;
        const float bottom = c + (d - c) * sx;
        return top + (bottom - top) * sy;
    }

    float FractalNoise(float x, float y, std::uint32_t seed)
    {
        float sum = 0.0f;
        float amplitude = 0.5f;
        float frequency = 1.0f;
        float total = 0.0f;
        for (int octave = 0; octave < 4; ++octave)
        {
            sum += SmoothNoise(x * frequency, y * frequency, seed + static_cast<std::uint32_t>(octave) * 101u) * amplitude;
            total += amplitude;
            amplitude *= 0.5f;
            frequency *= 2.0f;
        }
        return sum / total;
    }

    Terrain PickTerrain(float elevation, float moisture)
    {
        if (elevation > 0.68f) return Terrain::Mountain;
        if (elevation > 0.61f) return Terrain::Plateau;
        if (moisture > 0.66f && elevation < 0.45f) return Terrain::Swamp;
        if (moisture > 0.57f) return Terrain::Forest;
        if (moisture < 0.30f) return Terrain::Desert;
        return Terrain::Plain;
    }

    void CarveRivers(HexMap& map, const std::vector<float>& elevation, int count, std::mt19937& rng)
    {
        std::vector<int> sources;
        for (int i = 0; i < static_cast<int>(map.hexes.size()); ++i)
        {
            const Terrain t = map.hexes[static_cast<std::size_t>(i)].terrain;
            if (t == Terrain::Mountain || t == Terrain::Plateau)
            {
                sources.push_back(i);
            }
        }
        if (sources.empty())
        {
            return;
        }

        std::uniform_int_distribution<std::size_t> pick(0, sources.size() - 1);
        for (int river = 0; river < count; ++river)
        {
            int current = sources[pick(rng)];
            std::vector<char> visited(map.hexes.size(), 0);
            for (int step = 0; step < 80; ++step)
            {
                visited[static_cast<std::size_t>(current)] = 1;
                Hex& hex = map.hexes[static_cast<std::size_t>(current)];
                if (hex.terrain == Terrain::Sea)
                {
                    break;
                }
                hex.terrain = Terrain::River;

                int next = -1;
                float lowest = std::numeric_limits<float>::max();
                for (const HexCoord nb : HexNeighbors(IndexToHex(map, current)))
                {
                    if (!InBounds(map, nb))
                    {
                        continue;
                    }
                    const int ni = HexIndex(map, nb);
                    if (visited[static_cast<std::size_t>(ni)] != 0)
                    {
                        continue;
                    }
                    if (elevation[static_cast<std::size_t>(ni)] < lowest)
                    {
                        lowest = elevation[static_cast<std::size_t>(ni)];
                        next = ni;
                    }
                }
                if (next < 0)
                {
                    break;
                }
                current = next;
            }
        }
    }
}

bool InBounds(const HexMap& map, HexCoord coord)
{
    return coord.col >= 0 && coord.row >= 0 && coord.col < map.cols && coord.row < map.rows;
}

int HexIndex(const HexMap& map, HexCoord coord)
{
    return coord.row * map.cols + coord.col;
}

HexCoord IndexToHex(const HexMap& map, int index)
{
    return { index % map.cols, index / map.cols };
}

bool IsPassable(const HexMap& map, const GameData& data, int index)
{
    return data.terrain[ToIndex(map.hexes[static_cast<std::size_t>(index)].terrain)].move > 0.0f;
}

float MapWidthKm(const HexMap& map)
{
    return static_cast<float>(map.cols) * map.hexDistance;
}

float MapHeightKm(const HexMap& map)
{
    return static_cast<float>(map.rows) * map.hexDistance * std::numbers::sqrt3_v<float> / 2.0f;
}

void GenerateMap(HexMap& map, const GameData& data, int cols, int rows, std::uint32_t seed)
{
    const GameConfig& c = data.config;
    map.cols = cols;
    map.rows = rows;
    map.hexDistance = c.hexDistanceKm;
    map.hexes.assign(static_cast<std::size_t>(map.cols * map.rows), Hex{});

    std::mt19937 rng(seed);
    std::vector<float> elevation(map.hexes.size(), 0.0f);

    for (int row = 0; row < map.rows; ++row)
    {
        // 북쪽 해안선 깊이 2~6줄
        for (int col = 0; col < map.cols; ++col)
        {
            const int coast = 2 + static_cast<int>(SmoothNoise(static_cast<float>(col) / 6.0f, 0.5f, seed + 7u) * 5.0f);
            const std::size_t index = static_cast<std::size_t>(HexIndex(map, { col, row }));
            const float x = static_cast<float>(col) / 12.0f;
            const float y = static_cast<float>(row) / 12.0f;
            elevation[index] = FractalNoise(x, y, seed + 1u);
            const float moisture = FractalNoise(x, y, seed + 2u);

            map.hexes[index].terrain = row < coast ? Terrain::Sea : PickTerrain(elevation[index], moisture);
        }
    }

    CarveRivers(map, elevation, c.mapRivers, rng);

    std::uniform_int_distribution<int> anyHex(0, static_cast<int>(map.hexes.size()) - 1);
    const int cities = c.mapCities * static_cast<int>(map.hexes.size()) / std::max(1, c.mapCols * c.mapRows);
    for (int placed = 0, attempts = 0; placed < cities && attempts < 10000; ++attempts)
    {
        Hex& hex = map.hexes[static_cast<std::size_t>(anyHex(rng))];
        if (hex.terrain == Terrain::Plain || hex.terrain == Terrain::Forest)
        {
            hex.terrain = Terrain::City;
            ++placed;
        }
    }
}

std::vector<int> FindPath(const HexMap& map, const GameData& data, int start, int goal)
{
    const int count = static_cast<int>(map.hexes.size());
    if (start < 0 || goal < 0 || start >= count || goal >= count || start == goal || !IsPassable(map, data, goal))
    {
        return {};
    }

    const HexCoord goalCoord = IndexToHex(map, goal);
    const auto heuristic = [&](int index)
    {
        return static_cast<float>(HexDistance(IndexToHex(map, index), goalCoord)) * map.hexDistance;
    };

    std::vector<float> cost(static_cast<std::size_t>(count), std::numeric_limits<float>::max());
    std::vector<int> cameFrom(static_cast<std::size_t>(count), -1);
    using Node = std::pair<float, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;

    cost[static_cast<std::size_t>(start)] = 0.0f;
    open.push({ heuristic(start), start });

    while (!open.empty())
    {
        const auto [priority, current] = open.top();
        open.pop();
        if (current == goal)
        {
            break;
        }
        if (priority > cost[static_cast<std::size_t>(current)] + heuristic(current) + 0.001f)
        {
            continue;   // 오래된 노드
        }

        for (const HexCoord nb : HexNeighbors(IndexToHex(map, current)))
        {
            if (!InBounds(map, nb))
            {
                continue;
            }
            const int next = HexIndex(map, nb);
            const float move = data.terrain[ToIndex(map.hexes[static_cast<std::size_t>(next)].terrain)].move;
            if (move <= 0.0f)
            {
                continue;
            }
            const float nextCost = cost[static_cast<std::size_t>(current)] + map.hexDistance / move;
            if (nextCost < cost[static_cast<std::size_t>(next)])
            {
                cost[static_cast<std::size_t>(next)] = nextCost;
                cameFrom[static_cast<std::size_t>(next)] = current;
                open.push({ nextCost + heuristic(next), next });
            }
        }
    }

    if (cameFrom[static_cast<std::size_t>(goal)] < 0)
    {
        return {};
    }

    std::vector<int> path;
    for (int at = goal; at != start; at = cameFrom[static_cast<std::size_t>(at)])
    {
        path.push_back(at);
    }
    std::reverse(path.begin(), path.end());
    return path;
}
