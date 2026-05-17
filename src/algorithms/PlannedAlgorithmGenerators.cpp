#include "algorithms/PlannedAlgorithmGenerators.h"

#include "algorithms/AlgorithmRegistry.h"
#include "algorithms/IMapGenerator.h"
#include "map/MapData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <queue>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mgv {
namespace {

struct PaintAction {
    uint32_t x = 0;
    uint32_t y = 0;
    CellType type = CellType::Empty;
    float height = 1.0f;
    glm::u8vec4 color { 0, 0, 0, 0 };
};

class ScriptedGenerator : public IMapGenerator {
public:
    void reset(MapData& map, const GeneratorConfig& config) override {
        width_ = std::max<uint32_t>(4, config.width);
        depth_ = std::max<uint32_t>(4, config.depth);
        cursor_ = 0;
        stepsTaken_ = 0;
        finished_ = false;
        status_ = "Ready";
        actions_.clear();
        cellsPerStep_ = 1;
        build(map, config);
        if (actions_.empty()) {
            finished_ = true;
            status_ = "Completed";
        }
    }

    GeneratorStep step(MapData& map) override {
        if (finished_) return { false, true, status_ };

        const size_t end = std::min(actions_.size(), cursor_ + cellsPerStep_);
        for (; cursor_ < end; ++cursor_) {
            const PaintAction& action = actions_[cursor_];
            Cell& cell = map.at(action.x, action.y);
            cell.type = action.type;
            cell.height = action.height;
            cell.color = action.color;
            cell.flags = 0;
            cell.metadata = static_cast<uint32_t>(cursor_);
        }

        ++stepsTaken_;
        if (cursor_ >= actions_.size()) {
            finished_ = true;
            status_ = "Completed";
        } else {
            const int percent = static_cast<int>(
                static_cast<float>(cursor_) / static_cast<float>(actions_.size()) * 100.0f);
            status_ = std::string(name()) + " " + std::to_string(percent) + "%";
        }
        return { true, finished_, status_ };
    }

    bool finished() const override { return finished_; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

protected:
    virtual void build(MapData& map, const GeneratorConfig& config) = 0;

    size_t index(uint32_t x, uint32_t y) const {
        return static_cast<size_t>(y) * width_ + x;
    }

    void push(uint32_t x, uint32_t y, CellType type, float height,
              glm::u8vec4 color = { 0, 0, 0, 0 }) {
        if (x < width_ && y < depth_) actions_.push_back({ x, y, type, height, color });
    }

    void fillInitial(MapData& map, CellType type, float height) const {
        map.resize(width_, depth_, 1);
        for (uint32_t y = 0; y < depth_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                Cell& cell = map.at(x, y);
                cell.type = type;
                cell.height = height;
                cell.color = { 0, 0, 0, 0 };
                cell.flags = 0;
                cell.metadata = 0;
            }
        }
    }

    uint32_t width_ = 1;
    uint32_t depth_ = 1;
    uint32_t cellsPerStep_ = 1;
    size_t cursor_ = 0;
    uint64_t stepsTaken_ = 0;
    bool finished_ = false;
    std::string status_ = "Ready";
    std::vector<PaintAction> actions_;
};

struct MazeWork {
    uint32_t width = 1;
    uint32_t depth = 1;
    uint32_t spacing = 2;
    uint32_t corridorWidth = 1;
    float floorHeight = 0.08f;
    std::vector<uint8_t> carved;

    size_t index(uint32_t x, uint32_t y) const {
        return static_cast<size_t>(y) * width + x;
    }
};

void carveBrush(MazeWork& work,
                std::vector<PaintAction>& actions,
                uint32_t x,
                uint32_t y,
                CellType type = CellType::Corridor,
                float heightOverride = -1.0f) {
    const int32_t halfLo = static_cast<int32_t>((work.corridorWidth - 1) / 2);
    const int32_t halfHi = static_cast<int32_t>(work.corridorWidth / 2);
    const float height = heightOverride > 0.0f ? heightOverride : work.floorHeight;
    for (int32_t oy = -halfLo; oy <= halfHi; ++oy) {
        for (int32_t ox = -halfLo; ox <= halfHi; ++ox) {
            const int32_t cx = static_cast<int32_t>(x) + ox;
            const int32_t cy = static_cast<int32_t>(y) + oy;
            if (cx <= 0 || cy <= 0 ||
                cx >= static_cast<int32_t>(work.width - 1) ||
                cy >= static_cast<int32_t>(work.depth - 1)) {
                continue;
            }
            const uint32_t ux = static_cast<uint32_t>(cx);
            const uint32_t uy = static_cast<uint32_t>(cy);
            work.carved[work.index(ux, uy)] = 1;
            actions.push_back({ ux, uy, type, height, { 0, 0, 0, 0 } });
        }
    }
}

void carveLine(MazeWork& work,
               std::vector<PaintAction>& actions,
               uint32_t ax,
               uint32_t ay,
               uint32_t bx,
               uint32_t by) {
    int32_t x = static_cast<int32_t>(ax);
    int32_t y = static_cast<int32_t>(ay);
    const int32_t endX = static_cast<int32_t>(bx);
    const int32_t endY = static_cast<int32_t>(by);
    const int32_t stepX = (endX > x) ? 1 : (endX < x ? -1 : 0);
    const int32_t stepY = (endY > y) ? 1 : (endY < y ? -1 : 0);
    carveBrush(work, actions, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    while (x != endX || y != endY) {
        if (x != endX) x += stepX;
        if (y != endY) y += stepY;
        carveBrush(work, actions, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    }
}

void markMazeCell(const MazeWork& work,
                  std::vector<PaintAction>& actions,
                  uint32_t x,
                  uint32_t y,
                  CellType type,
                  float height) {
    if (x > 0 && y > 0 && x < work.width - 1 && y < work.depth - 1) {
        actions.push_back({ x, y, type, height, { 0, 0, 0, 0 } });
    }
}

std::vector<std::pair<int32_t, int32_t>> cardinalDirs(uint32_t spacing) {
    const int32_t s = static_cast<int32_t>(spacing);
    return { { s, 0 }, { -s, 0 }, { 0, s }, { 0, -s } };
}

bool mazeNodeInBounds(const MazeWork& work, int32_t x, int32_t y) {
    const int32_t spacing = static_cast<int32_t>(work.spacing);
    return x > 0 && y > 0 &&
           x < static_cast<int32_t>(work.width - 1) &&
           y < static_cast<int32_t>(work.depth - 1) &&
           ((x - 1) % spacing) == 0 &&
           ((y - 1) % spacing) == 0;
}

std::vector<std::pair<uint32_t, uint32_t>> mazeNodes(const MazeWork& work) {
    std::vector<std::pair<uint32_t, uint32_t>> nodes;
    for (uint32_t y = 1; y < work.depth - 1; y += work.spacing) {
        for (uint32_t x = 1; x < work.width - 1; x += work.spacing) {
            nodes.push_back({ x, y });
        }
    }
    return nodes;
}

MazeWork makeMazeWork(uint32_t width, uint32_t depth, const ClassicMazeSettings& settings) {
    MazeWork work;
    work.width = std::max<uint32_t>(5, width);
    work.depth = std::max<uint32_t>(5, depth);
    work.corridorWidth = std::clamp<uint32_t>(settings.corridorWidth, 1, 12);
    work.spacing = std::clamp<uint32_t>(settings.cellSpacing, work.corridorWidth + 1, 16);
    work.floorHeight = std::max(0.01f, settings.floorHeight);
    work.carved.assign(static_cast<size_t>(work.width) * work.depth, 0);
    return work;
}

class RandomizedPrimMazeGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "randomized_prim_maze"; }
    std::string_view name() const override { return "Randomized Prim Maze"; }
    std::string_view description() const override {
        return "Grows a maze from a frontier set, randomly connecting new cells to the carved tree.";
    }

private:
    struct Edge { uint32_t ax, ay, bx, by; };

    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.classicMaze;
        MazeWork work = makeMazeWork(width_, depth_, settings);
        width_ = work.width;
        depth_ = work.depth;
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Wall, settings.wallHeight);

        std::mt19937 rng(config.seed);
        auto nodes = mazeNodes(work);
        if (nodes.empty()) return;

        std::uniform_int_distribution<size_t> pickNode(0, nodes.size() - 1);
        auto [sx, sy] = nodes[pickNode(rng)];
        carveBrush(work, actions_, sx, sy, CellType::Current, settings.currentHeight);
        work.carved[work.index(sx, sy)] = 1;

        std::vector<Edge> frontier;
        auto addFrontier = [&](uint32_t x, uint32_t y) {
            for (auto [dx, dy] : cardinalDirs(work.spacing)) {
                const int32_t nx = static_cast<int32_t>(x) + dx;
                const int32_t ny = static_cast<int32_t>(y) + dy;
                if (!mazeNodeInBounds(work, nx, ny)) continue;
                const uint32_t ux = static_cast<uint32_t>(nx);
                const uint32_t uy = static_cast<uint32_t>(ny);
                if (!work.carved[work.index(ux, uy)]) {
                    frontier.push_back({ x, y, ux, uy });
                    if (settings.showFrontier) {
                        actions_.push_back({ ux, uy, CellType::Frontier, settings.frontierHeight, { 0, 0, 0, 0 } });
                    }
                }
            }
        };
        addFrontier(sx, sy);

        while (!frontier.empty()) {
            std::uniform_int_distribution<size_t> pick(0, frontier.size() - 1);
            const size_t idx = pick(rng);
            const Edge edge = frontier[idx];
            frontier.erase(frontier.begin() + static_cast<std::ptrdiff_t>(idx));

            if (work.carved[work.index(edge.bx, edge.by)]) continue;
            carveLine(work, actions_, edge.ax, edge.ay, edge.bx, edge.by);
            addFrontier(edge.bx, edge.by);
        }
    }
};

class RandomizedKruskalMazeGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "randomized_kruskal_maze"; }
    std::string_view name() const override { return "Randomized Kruskal Maze"; }
    std::string_view description() const override {
        return "Shuffles all candidate edges and carves those that join different disjoint sets.";
    }

private:
    struct Edge { uint32_t a, b; };

    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.classicMaze;
        MazeWork work = makeMazeWork(width_, depth_, settings);
        width_ = work.width;
        depth_ = work.depth;
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Wall, settings.wallHeight);

        auto nodes = mazeNodes(work);
        if (nodes.empty()) return;

        std::vector<int> nodeIndex(static_cast<size_t>(width_) * depth_, -1);
        for (size_t i = 0; i < nodes.size(); ++i) {
            nodeIndex[work.index(nodes[i].first, nodes[i].second)] = static_cast<int>(i);
            carveBrush(work, actions_, nodes[i].first, nodes[i].second);
        }

        std::vector<Edge> edges;
        for (size_t i = 0; i < nodes.size(); ++i) {
            const auto [x, y] = nodes[i];
            for (auto [dx, dy] : std::array<std::pair<int32_t, int32_t>, 2> {
                     std::pair<int32_t, int32_t> { static_cast<int32_t>(work.spacing), 0 },
                     std::pair<int32_t, int32_t> { 0, static_cast<int32_t>(work.spacing) } }) {
                const int32_t nx = static_cast<int32_t>(x) + dx;
                const int32_t ny = static_cast<int32_t>(y) + dy;
                if (!mazeNodeInBounds(work, nx, ny)) continue;
                edges.push_back({ static_cast<uint32_t>(i),
                                  static_cast<uint32_t>(nodeIndex[work.index(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny))]) });
            }
        }

        std::mt19937 rng(config.seed);
        if (settings.randomizeEdges) std::shuffle(edges.begin(), edges.end(), rng);

        std::vector<uint32_t> parent(nodes.size());
        std::iota(parent.begin(), parent.end(), 0);
        auto find = [&](uint32_t v) {
            while (parent[v] != v) {
                parent[v] = parent[parent[v]];
                v = parent[v];
            }
            return v;
        };

        for (const Edge& edge : edges) {
            const uint32_t ra = find(edge.a);
            const uint32_t rb = find(edge.b);
            if (ra == rb) continue;
            parent[ra] = rb;
            carveLine(work, actions_,
                      nodes[edge.a].first, nodes[edge.a].second,
                      nodes[edge.b].first, nodes[edge.b].second);
        }
    }
};

class BinaryTreeMazeGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "binary_tree_maze"; }
    std::string_view name() const override { return "Binary Tree Maze"; }
    std::string_view description() const override {
        return "Visits each cell once and carves either east or north/south according to bias.";
    }

private:
    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.classicMaze;
        MazeWork work = makeMazeWork(width_, depth_, settings);
        width_ = work.width;
        depth_ = work.depth;
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Wall, settings.wallHeight);

        std::mt19937 rng(config.seed);
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);
        auto nodes = mazeNodes(work);
        for (auto [x, y] : nodes) {
            carveBrush(work, actions_, x, y);
            std::vector<std::pair<uint32_t, uint32_t>> candidates;
            if (mazeNodeInBounds(work, static_cast<int32_t>(x + work.spacing), static_cast<int32_t>(y))) {
                candidates.push_back({ x + work.spacing, y });
            }
            if (mazeNodeInBounds(work, static_cast<int32_t>(x), static_cast<int32_t>(y + work.spacing))) {
                candidates.push_back({ x, y + work.spacing });
            }
            if (candidates.empty()) continue;
            size_t chosen = candidates.size() == 1 ? 0 :
                (chance(rng) < std::clamp(settings.horizontalBias, 0.0f, 1.0f) ? 0u : 1u);
            carveLine(work, actions_, x, y, candidates[chosen].first, candidates[chosen].second);
        }
    }
};

class SidewinderMazeGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "sidewinder_maze"; }
    std::string_view name() const override { return "Sidewinder Maze"; }
    std::string_view description() const override {
        return "Builds horizontal runs and occasionally closes a run by carving upward.";
    }

private:
    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.classicMaze;
        MazeWork work = makeMazeWork(width_, depth_, settings);
        width_ = work.width;
        depth_ = work.depth;
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Wall, settings.wallHeight);

        std::mt19937 rng(config.seed);
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);

        for (uint32_t y = 1; y < depth_ - 1; y += work.spacing) {
            std::vector<std::pair<uint32_t, uint32_t>> run;
            for (uint32_t x = 1; x < width_ - 1; x += work.spacing) {
                carveBrush(work, actions_, x, y);
                run.push_back({ x, y });

                const bool canEast = mazeNodeInBounds(work, static_cast<int32_t>(x + work.spacing), static_cast<int32_t>(y));
                const bool canNorth = mazeNodeInBounds(work, static_cast<int32_t>(x), static_cast<int32_t>(y - work.spacing));
                const bool carveEast = canEast && (!canNorth || chance(rng) < std::clamp(settings.horizontalBias, 0.0f, 1.0f));

                if (carveEast) {
                    carveLine(work, actions_, x, y, x + work.spacing, y);
                } else {
                    if (canNorth && !run.empty()) {
                        std::uniform_int_distribution<size_t> pick(0, run.size() - 1);
                        const auto [rx, ry] = run[pick(rng)];
                        carveLine(work, actions_, rx, ry, rx, ry - work.spacing);
                    }
                    run.clear();
                }
            }
        }
    }
};

class WilsonMazeGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "wilson_maze"; }
    std::string_view name() const override { return "Wilson's Algorithm"; }
    std::string_view description() const override {
        return "Creates a uniform spanning tree by loop-erased random walks into the carved tree.";
    }

private:
    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.classicMaze;
        MazeWork work = makeMazeWork(width_, depth_, settings);
        width_ = work.width;
        depth_ = work.depth;
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Wall, settings.wallHeight);

        auto nodes = mazeNodes(work);
        if (nodes.empty()) return;

        std::mt19937 rng(config.seed);
        std::shuffle(nodes.begin(), nodes.end(), rng);
        std::vector<uint8_t> inTree(static_cast<size_t>(width_) * depth_, 0);
        auto [rootX, rootY] = nodes.back();
        nodes.pop_back();
        inTree[work.index(rootX, rootY)] = 1;
        carveBrush(work, actions_, rootX, rootY, CellType::Current, settings.currentHeight);

        auto dirs = cardinalDirs(work.spacing);
        while (!nodes.empty()) {
            auto startIt = std::find_if(nodes.begin(), nodes.end(), [&](const auto& p) {
                return !inTree[work.index(p.first, p.second)];
            });
            if (startIt == nodes.end()) break;

            std::vector<std::pair<uint32_t, uint32_t>> walk;
            std::vector<int> walkIndex(static_cast<size_t>(width_) * depth_, -1);
            walk.push_back(*startIt);
            walkIndex[work.index(startIt->first, startIt->second)] = 0;
            markMazeCell(work, actions_, startIt->first, startIt->second, CellType::Current, settings.currentHeight);

            while (!inTree[work.index(walk.back().first, walk.back().second)]) {
                auto [x, y] = walk.back();
                std::vector<std::pair<uint32_t, uint32_t>> options;
                for (auto [dx, dy] : dirs) {
                    const int32_t nx = static_cast<int32_t>(x) + dx;
                    const int32_t ny = static_cast<int32_t>(y) + dy;
                    if (mazeNodeInBounds(work, nx, ny)) {
                        options.push_back({ static_cast<uint32_t>(nx), static_cast<uint32_t>(ny) });
                    }
                }
                std::uniform_int_distribution<size_t> pick(0, options.size() - 1);
                auto next = options[pick(rng)];
                markMazeCell(work, actions_, next.first, next.second, CellType::Frontier, settings.frontierHeight);

                int& existing = walkIndex[work.index(next.first, next.second)];
                if (existing >= 0) {
                    for (size_t i = static_cast<size_t>(existing) + 1; i < walk.size(); ++i) {
                        walkIndex[work.index(walk[i].first, walk[i].second)] = -1;
                        markMazeCell(work, actions_, walk[i].first, walk[i].second, CellType::Wall, settings.wallHeight);
                    }
                    walk.resize(static_cast<size_t>(existing) + 1);
                } else {
                    existing = static_cast<int>(walk.size());
                    walk.push_back(next);
                }
            }

            for (size_t i = 1; i < walk.size(); ++i) {
                carveLine(work, actions_, walk[i - 1].first, walk[i - 1].second,
                          walk[i].first, walk[i].second);
                inTree[work.index(walk[i - 1].first, walk[i - 1].second)] = 1;
                inTree[work.index(walk[i].first, walk[i].second)] = 1;
            }
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const auto& p) {
                return inTree[work.index(p.first, p.second)] != 0;
            }), nodes.end());
        }
    }
};

class AldousBroderMazeGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "aldous_broder_maze"; }
    std::string_view name() const override { return "Aldous-Broder Algorithm"; }
    std::string_view description() const override {
        return "Performs a random walk over all cells, carving only when a cell is first visited.";
    }

private:
    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.classicMaze;
        MazeWork work = makeMazeWork(width_, depth_, settings);
        width_ = work.width;
        depth_ = work.depth;
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Wall, settings.wallHeight);

        auto nodes = mazeNodes(work);
        if (nodes.empty()) return;

        std::mt19937 rng(config.seed);
        std::uniform_int_distribution<size_t> pickNode(0, nodes.size() - 1);
        auto [x, y] = nodes[pickNode(rng)];
        std::vector<uint8_t> visited(static_cast<size_t>(width_) * depth_, 0);
        uint32_t remaining = static_cast<uint32_t>(nodes.size()) - 1;
        visited[work.index(x, y)] = 1;
        carveBrush(work, actions_, x, y, CellType::Current, settings.currentHeight);

        auto dirs = cardinalDirs(work.spacing);
        uint32_t guard = 0;
        while (remaining > 0 && guard++ < nodes.size() * nodes.size() * 32) {
            std::vector<std::pair<uint32_t, uint32_t>> options;
            for (auto [dx, dy] : dirs) {
                const int32_t nx = static_cast<int32_t>(x) + dx;
                const int32_t ny = static_cast<int32_t>(y) + dy;
                if (mazeNodeInBounds(work, nx, ny)) {
                    options.push_back({ static_cast<uint32_t>(nx), static_cast<uint32_t>(ny) });
                }
            }
            std::uniform_int_distribution<size_t> pick(0, options.size() - 1);
            const auto [nx, ny] = options[pick(rng)];
            markMazeCell(work, actions_, nx, ny, CellType::Frontier, settings.frontierHeight);
            if (!visited[work.index(nx, ny)]) {
                carveLine(work, actions_, x, y, nx, ny);
                visited[work.index(nx, ny)] = 1;
                --remaining;
            }
            x = nx;
            y = ny;
            markMazeCell(work, actions_, x, y, CellType::Current, settings.currentHeight);
        }
    }
};

class HuntAndKillMazeGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "hunt_and_kill_maze"; }
    std::string_view name() const override { return "Hunt-and-Kill Algorithm"; }
    std::string_view description() const override {
        return "Randomly walks until stuck, then scans for an unvisited cell adjacent to the carved maze.";
    }

private:
    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.classicMaze;
        MazeWork work = makeMazeWork(width_, depth_, settings);
        width_ = work.width;
        depth_ = work.depth;
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Wall, settings.wallHeight);

        auto nodes = mazeNodes(work);
        if (nodes.empty()) return;

        std::mt19937 rng(config.seed);
        std::uniform_int_distribution<size_t> pickNode(0, nodes.size() - 1);
        auto [x, y] = nodes[pickNode(rng)];
        std::vector<uint8_t> visited(static_cast<size_t>(width_) * depth_, 0);
        visited[work.index(x, y)] = 1;
        carveBrush(work, actions_, x, y, CellType::Current, settings.currentHeight);
        auto dirs = cardinalDirs(work.spacing);

        while (true) {
            std::vector<std::pair<uint32_t, uint32_t>> unvisited;
            for (auto [dx, dy] : dirs) {
                const int32_t nx = static_cast<int32_t>(x) + dx;
                const int32_t ny = static_cast<int32_t>(y) + dy;
                if (mazeNodeInBounds(work, nx, ny) &&
                    !visited[work.index(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny))]) {
                    unvisited.push_back({ static_cast<uint32_t>(nx), static_cast<uint32_t>(ny) });
                }
            }

            if (!unvisited.empty()) {
                std::uniform_int_distribution<size_t> pick(0, unvisited.size() - 1);
                const auto [nx, ny] = unvisited[pick(rng)];
                carveLine(work, actions_, x, y, nx, ny);
                visited[work.index(nx, ny)] = 1;
                x = nx;
                y = ny;
                markMazeCell(work, actions_, x, y, CellType::Current, settings.currentHeight);
                continue;
            }

            bool hunted = false;
            for (const auto [hx, hy] : nodes) {
                if (visited[work.index(hx, hy)]) continue;
                markMazeCell(work, actions_, hx, hy, CellType::Frontier, settings.frontierHeight);

                std::vector<std::pair<uint32_t, uint32_t>> carvedNeighbors;
                for (auto [dx, dy] : dirs) {
                    const int32_t nx = static_cast<int32_t>(hx) + dx;
                    const int32_t ny = static_cast<int32_t>(hy) + dy;
                    if (mazeNodeInBounds(work, nx, ny) &&
                        visited[work.index(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny))]) {
                        carvedNeighbors.push_back({ static_cast<uint32_t>(nx), static_cast<uint32_t>(ny) });
                    }
                }
                if (!carvedNeighbors.empty()) {
                    std::uniform_int_distribution<size_t> pick(0, carvedNeighbors.size() - 1);
                    const auto [nx, ny] = carvedNeighbors[pick(rng)];
                    carveLine(work, actions_, hx, hy, nx, ny);
                    visited[work.index(hx, hy)] = 1;
                    x = hx;
                    y = hy;
                    hunted = true;
                    break;
                }
            }
            if (!hunted) break;
        }
    }
};

class GrowingTreeMazeGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "growing_tree_maze"; }
    std::string_view name() const override { return "Growing Tree Algorithm"; }
    std::string_view description() const override {
        return "Maintains an active cell list and chooses newest/random cells to blend DFS and Prim behavior.";
    }

private:
    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.classicMaze;
        MazeWork work = makeMazeWork(width_, depth_, settings);
        width_ = work.width;
        depth_ = work.depth;
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Wall, settings.wallHeight);

        auto nodes = mazeNodes(work);
        if (nodes.empty()) return;

        std::mt19937 rng(config.seed);
        std::uniform_int_distribution<size_t> pickNode(0, nodes.size() - 1);
        auto start = nodes[pickNode(rng)];
        std::vector<uint8_t> visited(static_cast<size_t>(width_) * depth_, 0);
        std::vector<std::pair<uint32_t, uint32_t>> active { start };
        visited[work.index(start.first, start.second)] = 1;
        carveBrush(work, actions_, start.first, start.second, CellType::Current, settings.currentHeight);
        auto dirs = cardinalDirs(work.spacing);
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);

        while (!active.empty()) {
            size_t activeIndex = active.size() - 1;
            if (chance(rng) < 0.35f) {
                std::uniform_int_distribution<size_t> pick(0, active.size() - 1);
                activeIndex = pick(rng);
            }
            const auto [x, y] = active[activeIndex];
            markMazeCell(work, actions_, x, y, CellType::Current, settings.currentHeight);

            std::vector<std::pair<uint32_t, uint32_t>> options;
            std::shuffle(dirs.begin(), dirs.end(), rng);
            for (auto [dx, dy] : dirs) {
                const int32_t nx = static_cast<int32_t>(x) + dx;
                const int32_t ny = static_cast<int32_t>(y) + dy;
                if (mazeNodeInBounds(work, nx, ny) &&
                    !visited[work.index(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny))]) {
                    options.push_back({ static_cast<uint32_t>(nx), static_cast<uint32_t>(ny) });
                }
            }
            if (options.empty()) {
                active.erase(active.begin() + static_cast<std::ptrdiff_t>(activeIndex));
                continue;
            }

            const auto [nx, ny] = options.front();
            carveLine(work, actions_, x, y, nx, ny);
            visited[work.index(nx, ny)] = 1;
            active.push_back({ nx, ny });
            markMazeCell(work, actions_, nx, ny, CellType::Frontier, settings.frontierHeight);
        }
    }
};

class RecursiveDivisionMazeGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "recursive_division_maze"; }
    std::string_view name() const override { return "Recursive Division Maze"; }
    std::string_view description() const override {
        return "Starts open, recursively adds dividing walls, and leaves one passage in each split.";
    }

private:
    void divide(uint32_t x, uint32_t y, uint32_t w, uint32_t h, std::mt19937& rng, const ClassicMazeSettings& settings) {
        if (w < 5 || h < 5) return;

        const bool vertical = w > h ? true : (h > w ? false : ((rng() & 1u) != 0));
        if (vertical) {
            std::uniform_int_distribution<uint32_t> pickWall(1, (w - 2) / 2);
            uint32_t wallX = x + pickWall(rng) * 2;
            std::uniform_int_distribution<uint32_t> pickGap(0, (h - 2) / 2);
            uint32_t gapY = y + 1 + pickGap(rng) * 2;
            for (uint32_t yy = y; yy < y + h; ++yy) {
                if (yy == gapY) {
                    push(wallX, yy, CellType::Path, settings.currentHeight);
                } else {
                    push(wallX, yy, CellType::Wall, settings.wallHeight);
                }
            }
            divide(x, y, wallX - x, h, rng, settings);
            divide(wallX + 1, y, x + w - wallX - 1, h, rng, settings);
        } else {
            std::uniform_int_distribution<uint32_t> pickWall(1, (h - 2) / 2);
            uint32_t wallY = y + pickWall(rng) * 2;
            std::uniform_int_distribution<uint32_t> pickGap(0, (w - 2) / 2);
            uint32_t gapX = x + 1 + pickGap(rng) * 2;
            for (uint32_t xx = x; xx < x + w; ++xx) {
                if (xx == gapX) {
                    push(xx, wallY, CellType::Path, settings.currentHeight);
                } else {
                    push(xx, wallY, CellType::Wall, settings.wallHeight);
                }
            }
            divide(x, y, w, wallY - y, rng, settings);
            divide(x, wallY + 1, w, y + h - wallY - 1, rng, settings);
        }
    }

    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.classicMaze;
        width_ = std::max<uint32_t>(9, width_);
        depth_ = std::max<uint32_t>(9, depth_);
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Room, settings.floorHeight);

        for (uint32_t x = 0; x < width_; ++x) {
            push(x, 0, CellType::Wall, settings.wallHeight);
            push(x, depth_ - 1, CellType::Wall, settings.wallHeight);
        }
        for (uint32_t y = 0; y < depth_; ++y) {
            push(0, y, CellType::Wall, settings.wallHeight);
            push(width_ - 1, y, CellType::Wall, settings.wallHeight);
        }

        std::mt19937 rng(config.seed);
        divide(1, 1, width_ - 2, depth_ - 2, rng, settings);
    }
};

class BSPDungeonGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "bsp_dungeon"; }
    std::string_view name() const override { return "BSP Dungeon"; }
    std::string_view description() const override {
        return "Recursively partitions the map, places one room per leaf, and connects sibling spaces.";
    }

private:
    struct Rect { uint32_t x, y, w, h; };

    void carveRect(const Rect& rect, CellType type, float height) {
        for (uint32_t y = rect.y; y < rect.y + rect.h && y < depth_; ++y) {
            for (uint32_t x = rect.x; x < rect.x + rect.w && x < width_; ++x) {
                push(x, y, type, height);
            }
        }
    }

    void carveWide(uint32_t x, uint32_t y, uint32_t radius, CellType type, float height) {
        const int32_t r = static_cast<int32_t>(radius);
        for (int32_t oy = -r; oy <= r; ++oy) {
            for (int32_t ox = -r; ox <= r; ++ox) {
                const int32_t cx = static_cast<int32_t>(x) + ox;
                const int32_t cy = static_cast<int32_t>(y) + oy;
                if (cx > 0 && cy > 0 &&
                    cx < static_cast<int32_t>(width_ - 1) &&
                    cy < static_cast<int32_t>(depth_ - 1)) {
                    push(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy), type, height);
                }
            }
        }
    }

    void carveCorridor(std::pair<uint32_t, uint32_t> a,
                       std::pair<uint32_t, uint32_t> b,
                       uint32_t width,
                       float height) {
        int32_t x = static_cast<int32_t>(a.first);
        int32_t y = static_cast<int32_t>(a.second);
        const int32_t endX = static_cast<int32_t>(b.first);
        const int32_t endY = static_cast<int32_t>(b.second);
        const int32_t stepX = (endX > x) ? 1 : (endX < x ? -1 : 0);
        const int32_t stepY = (endY > y) ? 1 : (endY < y ? -1 : 0);
        const uint32_t radius = width / 2;
        while (x != endX) {
            carveWide(static_cast<uint32_t>(x), static_cast<uint32_t>(y), radius, CellType::Corridor, height);
            x += stepX;
        }
        while (y != endY) {
            carveWide(static_cast<uint32_t>(x), static_cast<uint32_t>(y), radius, CellType::Corridor, height);
            y += stepY;
        }
        carveWide(static_cast<uint32_t>(x), static_cast<uint32_t>(y), radius, CellType::Corridor, height);
    }

    void split(const Rect& rect,
               uint32_t depth,
               const BSPDungeonSettings& settings,
               std::mt19937& rng,
               std::vector<Rect>& leaves) {
        if (depth >= settings.maxDepth ||
            rect.w < settings.minLeafSize * 2 ||
            rect.h < settings.minLeafSize * 2) {
            leaves.push_back(rect);
            return;
        }

        const bool splitVertical = rect.w > rect.h ? true : (rect.h > rect.w ? false : (rng() & 1u) != 0);
        if (splitVertical) {
            const uint32_t minSplit = settings.minLeafSize;
            const uint32_t maxSplit = rect.w - settings.minLeafSize;
            if (minSplit >= maxSplit) { leaves.push_back(rect); return; }
            std::uniform_int_distribution<uint32_t> pick(minSplit, maxSplit);
            uint32_t s = pick(rng);
            split({ rect.x, rect.y, s, rect.h }, depth + 1, settings, rng, leaves);
            split({ rect.x + s, rect.y, rect.w - s, rect.h }, depth + 1, settings, rng, leaves);
        } else {
            const uint32_t minSplit = settings.minLeafSize;
            const uint32_t maxSplit = rect.h - settings.minLeafSize;
            if (minSplit >= maxSplit) { leaves.push_back(rect); return; }
            std::uniform_int_distribution<uint32_t> pick(minSplit, maxSplit);
            uint32_t s = pick(rng);
            split({ rect.x, rect.y, rect.w, s }, depth + 1, settings, rng, leaves);
            split({ rect.x, rect.y + s, rect.w, rect.h - s }, depth + 1, settings, rng, leaves);
        }
    }

    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.bspDungeon;
        width_ = std::max<uint32_t>(16, width_);
        depth_ = std::max<uint32_t>(16, depth_);
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Wall, std::max(0.05f, settings.wallHeight));

        settings.minLeafSize = std::clamp<uint32_t>(settings.minLeafSize, 6, 64);
        settings.maxDepth = std::clamp<uint32_t>(settings.maxDepth, 1, 9);
        settings.minRoomSize = std::clamp<uint32_t>(settings.minRoomSize, 3, 32);
        settings.roomPadding = std::clamp<uint32_t>(settings.roomPadding, 1, 12);
        settings.corridorWidth = std::clamp<uint32_t>(settings.corridorWidth, 1, 12);

        std::mt19937 rng(config.seed);
        std::vector<Rect> leaves;
        split({ 1, 1, width_ - 2, depth_ - 2 }, 0, settings, rng, leaves);

        std::vector<Rect> rooms;
        for (const Rect& leaf : leaves) {
            const uint32_t maxW = leaf.w > settings.roomPadding * 2
                ? leaf.w - settings.roomPadding * 2 : leaf.w;
            const uint32_t maxH = leaf.h > settings.roomPadding * 2
                ? leaf.h - settings.roomPadding * 2 : leaf.h;
            const uint32_t rw = std::max(settings.minRoomSize, maxW);
            const uint32_t rh = std::max(settings.minRoomSize, maxH);
            Rect room {
                leaf.x + std::min(settings.roomPadding, leaf.w / 3),
                leaf.y + std::min(settings.roomPadding, leaf.h / 3),
                std::min(maxW, rw),
                std::min(maxH, rh)
            };
            if (room.x + room.w >= width_) room.w = width_ - room.x - 1;
            if (room.y + room.h >= depth_) room.h = depth_ - room.y - 1;
            if (room.w >= 2 && room.h >= 2) {
                rooms.push_back(room);
                carveRect(room, CellType::Room, settings.roomHeight);
            }
        }

        std::sort(rooms.begin(), rooms.end(), [](const Rect& a, const Rect& b) {
            if (a.x == b.x) return a.y < b.y;
            return a.x < b.x;
        });
        for (size_t i = 1; i < rooms.size(); ++i) {
            auto centerA = std::pair<uint32_t, uint32_t> {
                rooms[i - 1].x + rooms[i - 1].w / 2,
                rooms[i - 1].y + rooms[i - 1].h / 2
            };
            auto centerB = std::pair<uint32_t, uint32_t> {
                rooms[i].x + rooms[i].w / 2,
                rooms[i].y + rooms[i].h / 2
            };
            carveCorridor(centerA, centerB, settings.corridorWidth, settings.corridorHeight);
        }
    }
};

class CellularAutomataCaveGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "cellular_automata_cave"; }
    std::string_view name() const override { return "Cellular Automata Cave"; }
    std::string_view description() const override {
        return "Starts from random walls and repeatedly applies cave smoothing birth/death rules.";
    }

private:
    int countWalls(const std::vector<uint8_t>& grid, uint32_t x, uint32_t y, bool edgeWalls) const {
        int count = 0;
        for (int32_t oy = -1; oy <= 1; ++oy) {
            for (int32_t ox = -1; ox <= 1; ++ox) {
                if (ox == 0 && oy == 0) continue;
                const int32_t nx = static_cast<int32_t>(x) + ox;
                const int32_t ny = static_cast<int32_t>(y) + oy;
                if (nx < 0 || ny < 0 || nx >= static_cast<int32_t>(width_) || ny >= static_cast<int32_t>(depth_)) {
                    count += edgeWalls ? 1 : 0;
                } else if (grid[index(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny))]) {
                    ++count;
                }
            }
        }
        return count;
    }

    void emitGrid(const std::vector<uint8_t>& grid, const CellularAutomataSettings& settings) {
        for (uint32_t y = 0; y < depth_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                const bool wall = grid[index(x, y)] != 0;
                push(x, y, wall ? CellType::Wall : CellType::Corridor,
                     wall ? settings.wallHeight : settings.floorHeight);
            }
        }
    }

    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.cellularAutomata;
        width_ = std::max<uint32_t>(8, width_);
        depth_ = std::max<uint32_t>(8, depth_);
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Wall, settings.wallHeight);

        std::mt19937 rng(config.seed);
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);
        std::vector<uint8_t> grid(static_cast<size_t>(width_) * depth_, 0);
        for (uint32_t y = 0; y < depth_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                const bool edge = x == 0 || y == 0 || x == width_ - 1 || y == depth_ - 1;
                grid[index(x, y)] = (settings.edgeWalls && edge) || chance(rng) < settings.initialWallChance;
            }
        }
        emitGrid(grid, settings);

        for (uint32_t iter = 0; iter < settings.iterations; ++iter) {
            std::vector<uint8_t> next = grid;
            for (uint32_t y = 0; y < depth_; ++y) {
                for (uint32_t x = 0; x < width_; ++x) {
                    const int walls = countWalls(grid, x, y, settings.edgeWalls);
                    if (grid[index(x, y)]) {
                        next[index(x, y)] = walls >= static_cast<int>(settings.deathLimit);
                    } else {
                        next[index(x, y)] = walls >= static_cast<int>(settings.birthLimit);
                    }
                }
            }
            grid = std::move(next);
            emitGrid(grid, settings);
        }
    }
};

class DrunkardWalkCaveGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "drunkard_walk_cave"; }
    std::string_view name() const override { return "Drunkard Walk Cave"; }
    std::string_view description() const override {
        return "One or more walkers carve organic caves until the requested fill percentage is reached.";
    }

private:
    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.drunkardWalk;
        width_ = std::max<uint32_t>(8, width_);
        depth_ = std::max<uint32_t>(8, depth_);
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Wall, settings.wallHeight);

        std::mt19937 rng(config.seed);
        std::uniform_int_distribution<uint32_t> pickX(1, width_ - 2);
        std::uniform_int_distribution<uint32_t> pickY(1, depth_ - 2);
        std::uniform_int_distribution<int> pickDir(0, 3);
        std::uniform_real_distribution<float> turn(0.0f, 1.0f);
        std::array<std::pair<int32_t, int32_t>, 4> dirs = {
            std::pair<int32_t, int32_t> { 1, 0 },
            std::pair<int32_t, int32_t> { -1, 0 },
            std::pair<int32_t, int32_t> { 0, 1 },
            std::pair<int32_t, int32_t> { 0, -1 }
        };

        std::vector<std::pair<uint32_t, uint32_t>> walkers;
        const uint32_t walkerCount = std::clamp<uint32_t>(settings.walkerCount, 1, 32);
        for (uint32_t i = 0; i < walkerCount; ++i) {
            walkers.push_back(settings.spawnFromCenter
                ? std::pair<uint32_t, uint32_t> { width_ / 2, depth_ / 2 }
                : std::pair<uint32_t, uint32_t> { pickX(rng), pickY(rng) });
        }

        std::vector<uint8_t> carved(static_cast<size_t>(width_) * depth_, 0);
        uint32_t carvedCount = 0;
        const uint32_t target = static_cast<uint32_t>(
            std::clamp(settings.targetFill, 0.01f, 0.95f) * static_cast<float>(width_ * depth_));
        std::vector<int> dirIndex(walkerCount, 0);
        for (int& d : dirIndex) d = pickDir(rng);

        const uint32_t radius = std::clamp<uint32_t>(settings.brushRadius, 0, 8);
        for (uint32_t step = 0; step < settings.maxSteps && carvedCount < target; ++step) {
            for (uint32_t w = 0; w < walkerCount && carvedCount < target; ++w) {
                auto& pos = walkers[w];
                for (int32_t oy = -static_cast<int32_t>(radius); oy <= static_cast<int32_t>(radius); ++oy) {
                    for (int32_t ox = -static_cast<int32_t>(radius); ox <= static_cast<int32_t>(radius); ++ox) {
                        const int32_t cx = static_cast<int32_t>(pos.first) + ox;
                        const int32_t cy = static_cast<int32_t>(pos.second) + oy;
                        if (cx <= 0 || cy <= 0 ||
                            cx >= static_cast<int32_t>(width_ - 1) ||
                            cy >= static_cast<int32_t>(depth_ - 1)) {
                            continue;
                        }
                        const uint32_t ux = static_cast<uint32_t>(cx);
                        const uint32_t uy = static_cast<uint32_t>(cy);
                        if (!carved[index(ux, uy)]) {
                            carved[index(ux, uy)] = 1;
                            ++carvedCount;
                        }
                        push(ux, uy, CellType::Corridor, settings.floorHeight);
                    }
                }

                if (turn(rng) < settings.turnChance) dirIndex[w] = pickDir(rng);
                int32_t nx = static_cast<int32_t>(pos.first) + dirs[dirIndex[w]].first;
                int32_t ny = static_cast<int32_t>(pos.second) + dirs[dirIndex[w]].second;
                nx = std::clamp(nx, 1, static_cast<int32_t>(width_ - 2));
                ny = std::clamp(ny, 1, static_cast<int32_t>(depth_ - 2));
                pos = { static_cast<uint32_t>(nx), static_cast<uint32_t>(ny) };
            }
        }
    }
};

uint8_t shadeByte(float v) {
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
}

PaintAction terrainAction(uint32_t x,
                          uint32_t y,
                          float v,
                          float baseHeight,
                          float heightScale,
                          float seaLevel,
                          float mountainLevel,
                          bool colorize) {
    v = std::clamp(v, 0.0f, 1.0f);
    CellType type = CellType::Room;
    glm::u8vec4 color { 0, 0, 0, 0 };
    float height = baseHeight + v * heightScale;
    if (v < seaLevel) {
        type = CellType::Water;
        height = std::max(0.02f, height * 0.35f);
        if (colorize) color = { 38, 90, 170, 255 };
    } else if (v > mountainLevel) {
        type = CellType::Mountain;
        if (colorize) {
            const uint8_t s = shadeByte(130.0f + v * 95.0f);
            color = { s, s, s, 255 };
        }
    } else {
        type = CellType::Room;
        if (colorize) color = { 60, shadeByte(105.0f + v * 95.0f), 72, 255 };
    }
    return { x, y, type, height, color };
}

class SimplexNoiseHeightmapGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "simplex_noise_heightmap"; }
    std::string_view name() const override { return "Simplex Noise Heightmap"; }
    std::string_view description() const override {
        return "Uses 2D simplex gradient noise for less grid-aligned terrain than classic Perlin.";
    }

private:
    static float dot(int g, float x, float y) {
        static constexpr int grad3[12][2] = {
            { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 },
            { 1, 0 }, { -1, 0 }, { 1, 0 }, { -1, 0 },
            { 0, 1 }, { 0, -1 }, { 0, 1 }, { 0, -1 }
        };
        return grad3[g][0] * x + grad3[g][1] * y;
    }

    float simplex(float xin, float yin, const std::array<uint8_t, 512>& perm) const {
        constexpr float F2 = 0.366025403f;
        constexpr float G2 = 0.211324865f;
        const float s = (xin + yin) * F2;
        const int i = static_cast<int>(std::floor(xin + s));
        const int j = static_cast<int>(std::floor(yin + s));
        const float t = static_cast<float>(i + j) * G2;
        const float X0 = static_cast<float>(i) - t;
        const float Y0 = static_cast<float>(j) - t;
        const float x0 = xin - X0;
        const float y0 = yin - Y0;

        int i1 = 0, j1 = 0;
        if (x0 > y0) { i1 = 1; j1 = 0; } else { i1 = 0; j1 = 1; }

        const float x1 = x0 - static_cast<float>(i1) + G2;
        const float y1 = y0 - static_cast<float>(j1) + G2;
        const float x2 = x0 - 1.0f + 2.0f * G2;
        const float y2 = y0 - 1.0f + 2.0f * G2;

        const int ii = i & 255;
        const int jj = j & 255;
        const int gi0 = perm[ii + perm[jj]] % 12;
        const int gi1 = perm[ii + i1 + perm[jj + j1]] % 12;
        const int gi2 = perm[ii + 1 + perm[jj + 1]] % 12;

        auto corner = [](float tx, float ty, int gi, auto dotFn) {
            float t = 0.5f - tx * tx - ty * ty;
            if (t < 0.0f) return 0.0f;
            t *= t;
            return t * t * dotFn(gi, tx, ty);
        };

        return 70.0f * (corner(x0, y0, gi0, dot) +
                        corner(x1, y1, gi1, dot) +
                        corner(x2, y2, gi2, dot));
    }

    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.simplex;
        width_ = std::max<uint32_t>(4, width_);
        depth_ = std::max<uint32_t>(4, depth_);
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Empty, 0.01f);

        std::array<uint8_t, 256> base {};
        std::iota(base.begin(), base.end(), static_cast<uint8_t>(0));
        std::mt19937 rng(config.seed);
        std::shuffle(base.begin(), base.end(), rng);
        std::array<uint8_t, 512> perm {};
        for (size_t i = 0; i < perm.size(); ++i) perm[i] = base[i & 255u];

        for (uint32_t y = 0; y < depth_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                float amplitude = 1.0f;
                float frequency = 1.0f;
                float sum = 0.0f;
                float maxAmp = 0.0f;
                float sx = static_cast<float>(x) * settings.baseFrequency;
                float sy = static_cast<float>(y) * settings.baseFrequency;
                if (settings.domainWarp > 0.0f) {
                    sx += simplex(sx * 0.65f + 31.1f, sy * 0.65f, perm) * settings.domainWarp;
                    sy += simplex(sx * 0.65f, sy * 0.65f + 17.7f, perm) * settings.domainWarp;
                }
                for (uint32_t octave = 0; octave < std::max<uint32_t>(1, settings.octaves); ++octave) {
                    float n = simplex(sx * frequency, sy * frequency, perm) * 0.5f + 0.5f;
                    if (settings.ridged) n = 1.0f - std::abs(n * 2.0f - 1.0f);
                    sum += n * amplitude;
                    maxAmp += amplitude;
                    amplitude *= settings.persistence;
                    frequency *= settings.lacunarity;
                }
                float v = maxAmp > 0.0f ? sum / maxAmp : 0.0f;
                actions_.push_back(terrainAction(x, y, v, settings.baseHeight,
                                                 settings.heightScale, settings.seaLevel,
                                                 settings.mountainLevel, settings.colorize));
            }
        }
    }
};

class DiamondSquareTerrainGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "diamond_square_terrain"; }
    std::string_view name() const override { return "Diamond-Square Terrain"; }
    std::string_view description() const override {
        return "Generates fractal terrain by alternating diamond and square midpoint displacement passes.";
    }

private:
    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.diamondSquare;
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Empty, 0.01f);

        uint32_t n = 1;
        const uint32_t target = std::max(width_, depth_);
        while (n < target - 1) n <<= 1;
        const uint32_t size = n + 1;
        std::vector<float> h(static_cast<size_t>(size) * size, 0.5f);
        auto idx = [size](uint32_t x, uint32_t y) { return static_cast<size_t>(y) * size + x; };

        std::mt19937 rng(config.seed);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        h[idx(0, 0)] = 0.5f + dist(rng) * 0.1f;
        h[idx(n, 0)] = 0.5f + dist(rng) * 0.1f;
        h[idx(0, n)] = 0.5f + dist(rng) * 0.1f;
        h[idx(n, n)] = 0.5f + dist(rng) * 0.1f;

        float scale = 0.55f;
        for (uint32_t step = n; step > 1; step /= 2) {
            const uint32_t half = step / 2;
            for (uint32_t y = half; y < n; y += step) {
                for (uint32_t x = half; x < n; x += step) {
                    const float avg = (h[idx(x - half, y - half)] + h[idx(x + half, y - half)] +
                                       h[idx(x - half, y + half)] + h[idx(x + half, y + half)]) * 0.25f;
                    h[idx(x, y)] = avg + dist(rng) * scale;
                }
            }
            for (uint32_t y = 0; y <= n; y += half) {
                for (uint32_t x = (y + half) % step; x <= n; x += step) {
                    float sum = 0.0f;
                    int count = 0;
                    if (x >= half) { sum += h[idx(x - half, y)]; ++count; }
                    if (x + half <= n) { sum += h[idx(x + half, y)]; ++count; }
                    if (y >= half) { sum += h[idx(x, y - half)]; ++count; }
                    if (y + half <= n) { sum += h[idx(x, y + half)]; ++count; }
                    h[idx(x, y)] = sum / static_cast<float>(count) + dist(rng) * scale;
                }
            }
            scale *= std::clamp(settings.roughness, 0.2f, 0.95f);
        }

        auto [minIt, maxIt] = std::minmax_element(h.begin(), h.end());
        const float range = std::max(0.001f, *maxIt - *minIt);
        for (uint32_t y = 0; y < depth_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                const uint32_t sx = std::min<uint32_t>(n, static_cast<uint32_t>((static_cast<float>(x) / std::max(1u, width_ - 1)) * n));
                const uint32_t sy = std::min<uint32_t>(n, static_cast<uint32_t>((static_cast<float>(y) / std::max(1u, depth_ - 1)) * n));
                const float v = (h[idx(sx, sy)] - *minIt) / range;
                actions_.push_back(terrainAction(x, y, v, settings.baseHeight,
                                                 settings.heightScale, settings.seaLevel,
                                                 settings.mountainLevel, settings.colorize));
            }
        }
    }
};

class FaultFormationTerrainGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "fault_formation_terrain"; }
    std::string_view name() const override { return "Fault Formation Terrain"; }
    std::string_view description() const override {
        return "Repeatedly raises one side of random fault lines and lowers the other, then smooths.";
    }

private:
    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.faultFormation;
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Empty, 0.01f);

        std::vector<float> h(static_cast<size_t>(width_) * depth_, 0.5f);
        std::mt19937 rng(config.seed);
        std::uniform_real_distribution<float> angleDist(0.0f, 6.2831853f);
        std::uniform_real_distribution<float> pointX(0.0f, static_cast<float>(width_));
        std::uniform_real_distribution<float> pointY(0.0f, static_cast<float>(depth_));

        for (uint32_t iter = 0; iter < settings.iterations; ++iter) {
            const float angle = angleDist(rng);
            const float nx = std::cos(angle);
            const float ny = std::sin(angle);
            const float px = pointX(rng);
            const float py = pointY(rng);
            const float disp = settings.displacement * (1.0f - static_cast<float>(iter) / std::max(1u, settings.iterations));
            for (uint32_t y = 0; y < depth_; ++y) {
                for (uint32_t x = 0; x < width_; ++x) {
                    const float side = (static_cast<float>(x) - px) * nx + (static_cast<float>(y) - py) * ny;
                    h[index(x, y)] += side > 0.0f ? disp : -disp;
                }
            }
        }

        for (uint32_t pass = 0; pass < 4; ++pass) {
            std::vector<float> next = h;
            for (uint32_t y = 1; y + 1 < depth_; ++y) {
                for (uint32_t x = 1; x + 1 < width_; ++x) {
                    float avg = (h[index(x - 1, y)] + h[index(x + 1, y)] +
                                 h[index(x, y - 1)] + h[index(x, y + 1)]) * 0.25f;
                    next[index(x, y)] = h[index(x, y)] * (1.0f - settings.smoothing) + avg * settings.smoothing;
                }
            }
            h = std::move(next);
        }

        auto [minIt, maxIt] = std::minmax_element(h.begin(), h.end());
        const float range = std::max(0.001f, *maxIt - *minIt);
        for (uint32_t y = 0; y < depth_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                const float v = (h[index(x, y)] - *minIt) / range;
                actions_.push_back(terrainAction(x, y, v, settings.baseHeight,
                                                 settings.heightScale, settings.seaLevel,
                                                 settings.mountainLevel, settings.colorize));
            }
        }
    }
};

class SimpleTiledWFCGenerator final : public ScriptedGenerator {
public:
    std::string_view id() const override { return "simple_tiled_wfc"; }
    std::string_view name() const override { return "Simple Tiled WFC"; }
    std::string_view description() const override {
        return "A lightweight weighted tile-collapse pass with local adjacency constraints.";
    }

private:
    struct Tile { CellType type; uint32_t weight; float height; };

    bool compatible(CellType a, CellType b, const WFCSettings& settings) const {
        if (!settings.allowWaterNearMountain &&
            ((a == CellType::Water && b == CellType::Mountain) ||
             (a == CellType::Mountain && b == CellType::Water))) {
            return false;
        }
        if (a == CellType::Wall && b == CellType::Water) return false;
        if (a == CellType::Water && b == CellType::Wall) return false;
        return true;
    }

    void build(MapData& map, const GeneratorConfig& config) override {
        auto settings = config.wfc;
        cellsPerStep_ = std::max<uint32_t>(1, settings.cellsPerStep);
        fillInitial(map, CellType::Empty, 0.01f);

        std::vector<Tile> tiles = {
            { CellType::Water,    std::max<uint32_t>(1, settings.waterWeight),    settings.waterHeight },
            { CellType::Floor,    std::max<uint32_t>(1, settings.floorWeight),    settings.floorHeight },
            { CellType::Wall,     std::max<uint32_t>(1, settings.wallWeight),     settings.wallHeight },
            { CellType::Mountain, std::max<uint32_t>(1, settings.mountainWeight), settings.mountainHeight }
        };

        std::mt19937 rng(config.seed);
        std::vector<CellType> grid(static_cast<size_t>(width_) * depth_, CellType::Empty);
        for (uint32_t y = 0; y < depth_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                std::vector<Tile> candidates;
                for (Tile tile : tiles) {
                    bool ok = true;
                    if (x > 0 && !compatible(tile.type, grid[index(x - 1, y)], settings)) ok = false;
                    if (y > 0 && !compatible(tile.type, grid[index(x, y - 1)], settings)) ok = false;
                    if (settings.preferConnectedFloors &&
                        tile.type == CellType::Floor &&
                        ((x > 0 && grid[index(x - 1, y)] == CellType::Floor) ||
                         (y > 0 && grid[index(x, y - 1)] == CellType::Floor))) {
                        tile.weight *= 2;
                    }
                    if (ok) candidates.push_back(tile);
                }
                if (candidates.empty()) candidates = tiles;

                uint32_t total = 0;
                for (const Tile& tile : candidates) total += tile.weight;
                std::uniform_int_distribution<uint32_t> pick(1, total);
                uint32_t roll = pick(rng);
                Tile chosen = candidates.front();
                for (const Tile& tile : candidates) {
                    if (roll <= tile.weight) { chosen = tile; break; }
                    roll -= tile.weight;
                }
                grid[index(x, y)] = chosen.type;
                push(x, y, chosen.type, chosen.height);
            }
        }
    }
};

template <typename T>
std::unique_ptr<IMapGenerator> makeGenerator() {
    return std::make_unique<T>();
}

} // namespace

void registerPlannedAlgorithmGenerators(AlgorithmRegistry& registry) {
    // Mazes — classic graph/grid algorithms.
    registry.registerGenerator({
        .id = "randomized_prim_maze",
        .name = "Randomized Prim Maze",
        .description = "Frontier-growth maze. Grows a tree by always picking a random frontier wall and carving it.",
        .category = "Maze", .family = "Spanning Tree",
        .useCase = "Mazes with many short branches; easy to follow step by step.",
        .priority = 20,
        .create = [] { return makeGenerator<RandomizedPrimMazeGenerator>(); }
    });
    registry.registerGenerator({
        .id = "randomized_kruskal_maze",
        .name = "Randomized Kruskal Maze",
        .description = "Randomized minimum-spanning-tree maze using union-find over grid cells.",
        .category = "Maze", .family = "Spanning Tree",
        .useCase = "Even distribution of corridors; great for puzzle-style layouts.",
        .priority = 21,
        .create = [] { return makeGenerator<RandomizedKruskalMazeGenerator>(); }
    });
    registry.registerGenerator({
        .id = "wilson_maze",
        .name = "Wilson's Algorithm",
        .description = "Uniform spanning-tree maze via loop-erased random walks. Slow start, fast finish.",
        .category = "Maze", .family = "Spanning Tree",
        .useCase = "Unbiased mazes for research / statistical comparisons.",
        .priority = 22,
        .create = [] { return makeGenerator<WilsonMazeGenerator>(); }
    });
    registry.registerGenerator({
        .id = "aldous_broder_maze",
        .name = "Aldous-Broder Algorithm",
        .description = "Uniform spanning-tree maze via a single random walk that carves on first visit.",
        .category = "Maze", .family = "Spanning Tree",
        .useCase = "Unbiased mazes with extremely simple implementation.",
        .priority = 23,
        .create = [] { return makeGenerator<AldousBroderMazeGenerator>(); }
    });
    registry.registerGenerator({
        .id = "hunt_and_kill_maze",
        .name = "Hunt-and-Kill Algorithm",
        .description = "Random walk plus a scanning hunt phase to pick the next unvisited frontier.",
        .category = "Maze", .family = "Walk + Hunt",
        .useCase = "Long winding passages, classic dungeon corridors.",
        .priority = 24,
        .create = [] { return makeGenerator<HuntAndKillMazeGenerator>(); }
    });
    registry.registerGenerator({
        .id = "growing_tree_maze",
        .name = "Growing Tree Algorithm",
        .description = "Active-list maze. Blends DFS-like and Prim-like behaviour by varying the pick rule.",
        .category = "Maze", .family = "Active List",
        .useCase = "Tunable mix of long corridors and branching.",
        .priority = 25,
        .create = [] { return makeGenerator<GrowingTreeMazeGenerator>(); }
    });
    registry.registerGenerator({
        .id = "binary_tree_maze",
        .name = "Binary Tree Maze",
        .description = "Fast biased generator that picks one of two directions per cell. Visibly diagonal.",
        .category = "Maze", .family = "Biased",
        .useCase = "Demos, small mazes, intentionally directional puzzles.",
        .priority = 26,
        .create = [] { return makeGenerator<BinaryTreeMazeGenerator>(); }
    });
    registry.registerGenerator({
        .id = "sidewinder_maze",
        .name = "Sidewinder Maze",
        .description = "Run-based generator that creates horizontal corridors closed by vertical taps.",
        .category = "Maze", .family = "Biased",
        .useCase = "Maps that should read top-to-bottom; horizontally biased levels.",
        .priority = 27,
        .create = [] { return makeGenerator<SidewinderMazeGenerator>(); }
    });
    registry.registerGenerator({
        .id = "recursive_division_maze",
        .name = "Recursive Division Maze",
        .description = "Starts fully open, then recursively adds walls with single passage gaps.",
        .category = "Maze", .family = "Subdivision",
        .useCase = "Rectangular halls, architectural ruin layouts.",
        .priority = 28,
        .create = [] { return makeGenerator<RecursiveDivisionMazeGenerator>(); }
    });

    // Dungeons & caves.
    registry.registerGenerator({
        .id = "bsp_dungeon",
        .name = "BSP Dungeon",
        .description = "Binary space partitioning. Recursively splits the map, drops rooms in leaves, "
                       "connects siblings with corridors.",
        .category = "Dungeon", .family = "Space Partition",
        .useCase = "Structured dungeons with clear room hierarchy.",
        .priority = 30,
        .create = [] { return makeGenerator<BSPDungeonGenerator>(); }
    });
    registry.registerGenerator({
        .id = "cellular_automata_cave",
        .name = "Cellular Automata Cave",
        .description = "Random fill followed by smoothing rules (4-5 rule). Organic, biological feel.",
        .category = "Cave", .family = "Cellular Automata",
        .useCase = "Organic caverns, natural underground systems.",
        .priority = 31,
        .create = [] { return makeGenerator<CellularAutomataCaveGenerator>(); }
    });
    registry.registerGenerator({
        .id = "drunkard_walk_cave",
        .name = "Drunkard Walk Cave",
        .description = "Agent-based carving. One or more walkers wander randomly, opening floors as they go.",
        .category = "Cave", .family = "Agent",
        .useCase = "Twisting tunnels, mining-game maps.",
        .priority = 32,
        .create = [] { return makeGenerator<DrunkardWalkCaveGenerator>(); }
    });

    // Terrain.
    registry.registerGenerator({
        .id = "simplex_noise_heightmap",
        .name = "Simplex Noise Heightmap",
        .description = "Simplex gradient noise (Perlin's successor) with optional domain warp.",
        .category = "Terrain", .family = "Noise",
        .useCase = "Smooth open-world heightmaps without grid artefacts.",
        .priority = 40,
        .create = [] { return makeGenerator<SimplexNoiseHeightmapGenerator>(); }
    });
    registry.registerGenerator({
        .id = "diamond_square_terrain",
        .name = "Diamond-Square Terrain",
        .description = "Classic fractal midpoint displacement. Self-similar at every scale.",
        .category = "Terrain", .family = "Fractal",
        .useCase = "Retro-style continents, fast plausible terrain.",
        .priority = 41,
        .create = [] { return makeGenerator<DiamondSquareTerrainGenerator>(); }
    });
    registry.registerGenerator({
        .id = "fault_formation_terrain",
        .name = "Fault Formation Terrain",
        .description = "Repeated tectonic-style fault lines uplift and depress strips of the map.",
        .category = "Terrain", .family = "Fault",
        .useCase = "Mountain ridges, plateau-and-valley landscapes.",
        .priority = 42,
        .create = [] { return makeGenerator<FaultFormationTerrainGenerator>(); }
    });

    // Tile / constraint.
    registry.registerGenerator({
        .id = "simple_tiled_wfc",
        .name = "Simple Tiled WFC",
        .description = "Constraint-based weighted tile collapse. Tiles propagate local rules until stable.",
        .category = "Tile / WFC", .family = "Constraint Solver",
        .useCase = "Designer-driven content from a hand-authored tile set.",
        .priority = 50,
        .create = [] { return makeGenerator<SimpleTiledWFCGenerator>(); }
    });
}

} // namespace mgv
