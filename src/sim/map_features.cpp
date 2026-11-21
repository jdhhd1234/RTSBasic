#include "sim/map_features.hpp"
#include "sim/spline.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace
{
    // HexNeighbors 순서에 맞춘 변의 끝점. x 단위는 d/2, y 단위는 d/(2√3).
    constexpr std::array<std::pair<int, int>, 6> EdgeCorners{ {
        { 1, 1 }, { 1, -1 }, { 0, -2 }, { -1, -1 }, { -1, 1 }, { 0, 2 }
    } };

    Vec2 CenterAt(const HexMap& map, int index)
    {
        return HexCenter(IndexToHex(map, index), map.hexDistance);
    }

    std::vector<MapChain> RiverChains(const HexMap& map)
    {
        const int count = static_cast<int>(map.hexes.size());
        std::vector<char> visited(map.hexes.size(), 0);
        std::vector<MapChain> chains;
        const auto isRiver = [&](int index) { return map.hexes[static_cast<std::size_t>(index)].terrain == Terrain::River; };

        const auto walk = [&](int start)
        {
            MapChain& chain = chains.emplace_back();
            for (int current = start; current >= 0;)
            {
                visited[static_cast<std::size_t>(current)] = 1;
                chain.points.push_back(CenterAt(map, current));

                int next = -1;
                bool touchesStart = false;
                for (const HexCoord nb : HexNeighbors(IndexToHex(map, current)))
                {
                    if (!InBounds(map, nb))
                    {
                        continue;
                    }
                    const int index = HexIndex(map, nb);
                    if (index == start)
                    {
                        touchesStart = true;
                    }
                    if (isRiver(index) && visited[static_cast<std::size_t>(index)] == 0)
                    {
                        next = index;
                        break;
                    }
                }
                chain.closed = next < 0 && touchesStart && chain.points.size() > 2;
                current = next;
            }
        };

        // 끝점부터 걷고, 남은 갈래와 순환 구간은 따로 잇는다.
        for (int i = 0; i < count; ++i)
        {
            if (!isRiver(i) || visited[static_cast<std::size_t>(i)] != 0)
            {
                continue;
            }
            const auto neighbors = HexNeighbors(IndexToHex(map, i));
            const auto degree = std::count_if(neighbors.begin(), neighbors.end(), [&](HexCoord nb)
            {
                return InBounds(map, nb) && isRiver(HexIndex(map, nb));
            });
            if (degree <= 1)
            {
                walk(i);
            }
        }
        for (int i = 0; i < count; ++i)
        {
            if (isRiver(i) && visited[static_cast<std::size_t>(i)] == 0)
            {
                walk(i);
            }
        }
        return chains;
    }

    void BuildRoads(const HexMap& map, const GameData& data, MapFeatures& features)
    {
        const std::size_t neighbors = features.sites.size() > 60 ? 1 : 2;
        std::set<std::pair<int, int>> connected;
        std::vector<std::pair<int, int>> candidates;
        candidates.reserve(features.sites.size());
        for (const int start : features.sites)
        {
            candidates.clear();
            const HexCoord startCoord = IndexToHex(map, start);
            for (const int goal : features.sites)
            {
                if (goal != start)
                {
                    candidates.emplace_back(HexDistance(startCoord, IndexToHex(map, goal)), goal);
                }
            }

            const std::size_t limit = std::min(neighbors, candidates.size());
            std::partial_sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(limit), candidates.end());
            for (std::size_t i = 0; i < limit; ++i)
            {
                const int goal = candidates[i].second;
                if (!connected.emplace(std::min(start, goal), std::max(start, goal)).second)
                {
                    continue;
                }
                std::vector<int> path = FindPath(map, data, start, goal);
                if (path.empty())
                {
                    continue;
                }
                path.insert(path.begin(), start);
                std::vector<Vec2> centers;
                centers.reserve(path.size());
                for (const int index : path)
                {
                    centers.push_back(CenterAt(map, index));
                }
                features.roads.push_back({ std::move(path), SimplifyPolyline(centers, map.hexDistance * 0.3f) });
            }
        }
    }
}

std::vector<MapChain> BoundaryChains(const HexMap& map, const std::function<bool(int, int)>& isBoundary)
{
    std::vector<std::tuple<int, int, Vec2>> edges;
    std::unordered_map<int, std::vector<std::size_t>> cornerEdges;
    const int stride = 2 * map.cols + 2;
    for (int i = 0; i < static_cast<int>(map.hexes.size()); ++i)
    {
        const HexCoord coord = IndexToHex(map, i);
        const auto cornerKey = [&](std::size_t corner)
        {
            const auto [x, y] = EdgeCorners[corner];
            return (3 * coord.row + y + 2) * stride + 2 * coord.col + (coord.row & 1) + x + 1;
        };
        const auto neighbors = HexNeighbors(coord);
        for (std::size_t side = 0; side < neighbors.size(); ++side)
        {
            const HexCoord nb = neighbors[side];
            if (!InBounds(map, nb))
            {
                continue;
            }
            const int j = HexIndex(map, nb);
            if (j <= i || (!isBoundary(i, j) && !isBoundary(j, i)))
            {
                continue;
            }

            const int a = cornerKey(side);
            const int b = cornerKey((side + 1) % EdgeCorners.size());
            const Vec2 ci = HexCenter(coord, map.hexDistance);
            const Vec2 cj = HexCenter(nb, map.hexDistance);
            cornerEdges[a].push_back(edges.size());
            cornerEdges[b].push_back(edges.size());
            edges.emplace_back(a, b, Vec2{ (ci.x + cj.x) * 0.5f, (ci.y + cj.y) * 0.5f });
        }
    }

    std::vector<char> visited(edges.size(), 0);
    std::vector<MapChain> chains;
    const auto walk = [&](int start, std::size_t edge)
    {
        MapChain& chain = chains.emplace_back();
        int corner = start;
        while (visited[edge] == 0)
        {
            visited[edge] = 1;
            const auto& [a, b, midpoint] = edges[edge];
            chain.points.push_back(midpoint);
            corner = corner == a ? b : a;
            if (corner == start)
            {
                chain.closed = true;
                break;
            }
            const auto& incident = cornerEdges.at(corner);
            if (incident.size() != 2)
            {
                break;
            }
            edge = edge == incident.front() ? incident.back() : incident.front();
        }
    };

    // 끝점이나 분기점부터 잇고, 남은 닫힌 고리를 잇는다.
    for (std::size_t i = 0; i < edges.size(); ++i)
    {
        if (visited[i] != 0)
        {
            continue;
        }
        const auto& [a, b, midpoint] = edges[i];
        if (cornerEdges.at(a).size() != 2)
        {
            walk(a, i);
        }
        else if (cornerEdges.at(b).size() != 2)
        {
            walk(b, i);
        }
    }
    for (std::size_t i = 0; i < edges.size(); ++i)
    {
        if (visited[i] == 0)
        {
            walk(std::get<0>(edges[i]), i);
        }
    }
    return chains;
}

std::vector<MapChain> FrontLines(const HexMap& map)
{
    return BoundaryChains(map, [&](int a, int b)
    {
        const int ownerA = map.hexes[static_cast<std::size_t>(a)].owner;
        const int ownerB = map.hexes[static_cast<std::size_t>(b)].owner;
        return ownerA >= 0 && ownerB >= 0 && ownerA != ownerB;
    });
}

void BuildMapFeatures(const HexMap& map, const GameData& data, const std::vector<int>& baseHexes, MapFeatures& out)
{
    MapFeatures features;
    features.coastlines = BoundaryChains(map, [&](int a, int b)
    {
        return (map.hexes[static_cast<std::size_t>(a)].terrain == Terrain::Sea)
            != (map.hexes[static_cast<std::size_t>(b)].terrain == Terrain::Sea);
    });
    features.rivers = RiverChains(map);

    const int count = static_cast<int>(map.hexes.size());
    std::vector<char> isSite(map.hexes.size(), 0);
    const auto addSite = [&](int index)
    {
        if (isSite[static_cast<std::size_t>(index)] == 0)
        {
            isSite[static_cast<std::size_t>(index)] = 1;
            features.sites.push_back(index);
        }
    };
    for (const int index : baseHexes)
    {
        if (index >= 0 && index < count)
        {
            addSite(index);
        }
    }
    for (int i = 0; i < count; ++i)
    {
        if (map.hexes[static_cast<std::size_t>(i)].terrain == Terrain::City)
        {
            addSite(i);
        }
    }
    BuildRoads(map, data, features);
    out = std::move(features);
}
