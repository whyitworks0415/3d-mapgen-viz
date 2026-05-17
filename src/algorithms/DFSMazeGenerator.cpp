#include "algorithms/DFSMazeGenerator.h"

#include "algorithms/AlgorithmRegistry.h"
#include "map/MapData.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace mgv {

void DFSMazeGenerator::reset(MapData& map, const GeneratorConfig& config) {
    width_ = std::max<uint32_t>(5, config.width);
    depth_ = std::max<uint32_t>(5, config.depth);
    corridorWidth_ = std::clamp<uint32_t>(config.dfs.corridorWidth, 1, 12);
    cellSpacing_ = std::clamp<uint32_t>(config.dfs.cellSpacing, corridorWidth_ + 1, 16);
    startMode_ = std::min<uint32_t>(config.dfs.startMode, 2);
    randomizeDirections_ = config.dfs.randomizeDirections;
    showBacktrackTrail_ = config.dfs.showBacktrackTrail;
    clearVisitOverlayOnFinish_ = config.dfs.clearVisitOverlayOnFinish;
    braidChance_ = std::clamp(config.dfs.braidChance, 0.0f, 1.0f);
    wallHeight_ = std::max(0.01f, config.dfs.wallHeight);
    floorHeight_ = std::max(0.01f, config.dfs.floorHeight);
    visitedHeight_ = std::max(0.01f, config.dfs.visitedHeight);
    currentHeight_ = std::max(0.01f, config.dfs.currentHeight);
    stepsTaken_ = 0;
    finished_ = false;
    status_ = "Ready";

    rng_.seed(config.seed);
    carved_.assign(static_cast<size_t>(width_) * depth_, 0);
    visited_.assign(static_cast<size_t>(width_) * depth_, 0);
    stack_.clear();

    map.resize(width_, depth_, 1);

    const uint32_t startX = startMode_ == 0 ? randomNodeCoordinate(width_)
                                            : startCoordinate(width_);
    const uint32_t startY = startMode_ == 0 ? randomNodeCoordinate(depth_)
                                            : startCoordinate(depth_);
    carve(startX, startY);
    visited_[index(startX, startY)] = 1;
    stack_.push_back({ startX, startY });

    applyVisualization(map);
}

GeneratorStep DFSMazeGenerator::step(MapData& map) {
    if (finished_) {
        return { false, true, status_ };
    }

    if (stack_.empty()) {
        finished_ = true;
        status_ = "Completed";
        applyVisualization(map);
        return { true, true, status_ };
    }

    const auto [x, y] = stack_.back();
    auto neighbors = unvisitedNeighbors(x, y);

    if (!neighbors.empty()) {
        std::uniform_int_distribution<size_t> pick(0, neighbors.size() - 1);
        const auto [nextX, nextY] = neighbors[pick(rng_)];
        carveLine(x, y, nextX, nextY);
        visited_[index(nextX, nextY)] = 1;
        stack_.push_back({ nextX, nextY });
        status_ = "Carving";
    } else {
        bool braided = false;
        std::uniform_real_distribution<float> braid(0.0f, 1.0f);
        if (braidChance_ > 0.0f && braid(rng_) < braidChance_) {
            auto candidates = visitedNeighbors(x, y);
            if (!candidates.empty()) {
                std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
                const auto [loopX, loopY] = candidates[pick(rng_)];
                carveLine(x, y, loopX, loopY);
                braided = true;
                status_ = "Braiding";
            }
        }
        stack_.pop_back();
        if (!braided) {
            status_ = stack_.empty() ? "Completed" : "Backtracking";
        }
    }

    ++stepsTaken_;
    if (stack_.empty()) {
        finished_ = true;
        status_ = "Completed";
    }

    applyVisualization(map);
    return { true, finished_, status_ };
}

bool DFSMazeGenerator::isMazeCell(int32_t x, int32_t y) const {
    return x > 0 && y > 0 &&
           x < static_cast<int32_t>(width_ - 1) &&
           y < static_cast<int32_t>(depth_ - 1) &&
           ((x - 1) % static_cast<int32_t>(cellSpacing_)) == 0 &&
           ((y - 1) % static_cast<int32_t>(cellSpacing_)) == 0;
}

void DFSMazeGenerator::carve(uint32_t x, uint32_t y) {
    const int32_t halfLo = static_cast<int32_t>((corridorWidth_ - 1) / 2);
    const int32_t halfHi = static_cast<int32_t>(corridorWidth_ / 2);
    for (int32_t oy = -halfLo; oy <= halfHi; ++oy) {
        for (int32_t ox = -halfLo; ox <= halfHi; ++ox) {
            const int32_t cx = static_cast<int32_t>(x) + ox;
            const int32_t cy = static_cast<int32_t>(y) + oy;
            if (cx > 0 && cy > 0 &&
                cx < static_cast<int32_t>(width_ - 1) &&
                cy < static_cast<int32_t>(depth_ - 1)) {
                carved_[index(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy))] = 1;
            }
        }
    }
}

void DFSMazeGenerator::carveLine(uint32_t ax, uint32_t ay, uint32_t bx, uint32_t by) {
    int32_t x = static_cast<int32_t>(ax);
    int32_t y = static_cast<int32_t>(ay);
    const int32_t endX = static_cast<int32_t>(bx);
    const int32_t endY = static_cast<int32_t>(by);
    const int32_t stepX = (endX > x) ? 1 : (endX < x ? -1 : 0);
    const int32_t stepY = (endY > y) ? 1 : (endY < y ? -1 : 0);

    carve(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    while (x != endX || y != endY) {
        if (x != endX) x += stepX;
        if (y != endY) y += stepY;
        carve(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    }
}

std::vector<DFSMazeGenerator::Point>
DFSMazeGenerator::unvisitedNeighbors(uint32_t x, uint32_t y) {
    const std::array<std::pair<int32_t, int32_t>, 4> kDirections = {
        std::pair<int32_t, int32_t> {  static_cast<int32_t>(cellSpacing_),  0 },
        std::pair<int32_t, int32_t> { -static_cast<int32_t>(cellSpacing_),  0 },
        std::pair<int32_t, int32_t> {  0,  static_cast<int32_t>(cellSpacing_) },
        std::pair<int32_t, int32_t> {  0, -static_cast<int32_t>(cellSpacing_) }
    };

    std::vector<Point> neighbors;
    neighbors.reserve(kDirections.size());

    std::vector<std::pair<int32_t, int32_t>> directions(kDirections.begin(), kDirections.end());
    if (randomizeDirections_) {
        std::shuffle(directions.begin(), directions.end(), rng_);
    }

    for (const auto [dx, dy] : directions) {
        const int32_t nx = static_cast<int32_t>(x) + dx;
        const int32_t ny = static_cast<int32_t>(y) + dy;
        if (!isMazeCell(nx, ny)) continue;

        const uint32_t ux = static_cast<uint32_t>(nx);
        const uint32_t uy = static_cast<uint32_t>(ny);
        if (!visited_[index(ux, uy)]) {
            neighbors.push_back({ ux, uy });
        }
    }

    return neighbors;
}

std::vector<DFSMazeGenerator::Point>
DFSMazeGenerator::visitedNeighbors(uint32_t x, uint32_t y) const {
    const std::array<std::pair<int32_t, int32_t>, 4> kDirections = {
        std::pair<int32_t, int32_t> {  static_cast<int32_t>(cellSpacing_),  0 },
        std::pair<int32_t, int32_t> { -static_cast<int32_t>(cellSpacing_),  0 },
        std::pair<int32_t, int32_t> {  0,  static_cast<int32_t>(cellSpacing_) },
        std::pair<int32_t, int32_t> {  0, -static_cast<int32_t>(cellSpacing_) }
    };

    std::vector<Point> neighbors;
    neighbors.reserve(kDirections.size());

    for (const auto [dx, dy] : kDirections) {
        const int32_t nx = static_cast<int32_t>(x) + dx;
        const int32_t ny = static_cast<int32_t>(y) + dy;
        if (!isMazeCell(nx, ny)) continue;

        const uint32_t ux = static_cast<uint32_t>(nx);
        const uint32_t uy = static_cast<uint32_t>(ny);
        if (visited_[index(ux, uy)]) {
            neighbors.push_back({ ux, uy });
        }
    }

    return neighbors;
}

void DFSMazeGenerator::applyVisualization(MapData& map) const {
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            Cell& cell = map.at(x, y);
            cell.color = { 0, 0, 0, 0 };
            cell.flags = 0;
            cell.metadata = 0;

            if (carved_[index(x, y)]) {
                cell.type = CellType::Corridor;
                cell.height = floorHeight_;
            } else {
                cell.type = CellType::Wall;
                cell.height = wallHeight_;
            }
        }
    }

    if (finished_ && clearVisitOverlayOnFinish_) return;
    if (!showBacktrackTrail_ && !stack_.empty()) {
        const auto [x, y] = stack_.back();
        Cell& cell = map.at(x, y);
        cell.type = CellType::Current;
        cell.height = currentHeight_;
        return;
    }

    for (const auto [x, y] : stack_) {
        Cell& cell = map.at(x, y);
        cell.type = CellType::Visited;
        cell.height = visitedHeight_;
    }

    if (!stack_.empty()) {
        const auto [x, y] = stack_.back();
        Cell& cell = map.at(x, y);
        cell.type = CellType::Current;
        cell.height = currentHeight_;
    }
}

uint32_t DFSMazeGenerator::startCoordinate(uint32_t extent) const {
    if (startMode_ == 2) return 1;

    const uint32_t maxIndex = (extent > 3) ? (extent - 3) / cellSpacing_ : 0;
    const uint32_t middle = maxIndex / 2;
    return 1 + middle * cellSpacing_;
}

uint32_t DFSMazeGenerator::randomNodeCoordinate(uint32_t extent) {
    const uint32_t maxIndex = (extent > 3) ? (extent - 3) / cellSpacing_ : 0;
    std::uniform_int_distribution<uint32_t> pick(0, maxIndex);
    return 1 + pick(rng_) * cellSpacing_;
}

void registerDFSMazeGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "dfs_maze",
        .name        = "DFS Maze (Recursive Backtracker)",
        .description = "Recursive backtracker maze generation with step-by-step playback. "
                       "Shows the active cell, visited cells, and the backtracking trail in real time.",
        .category    = "Maze",
        .family      = "Spanning Tree",
        .useCase     = "Roguelike corridors, puzzle mazes, classroom demos of DFS.",
        .priority    = 1,
        .create      = [] { return std::make_unique<DFSMazeGenerator>(); }
    });
}

} // namespace mgv
