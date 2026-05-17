#include "algorithms/WilsonMazeGenerator.h"

#include "algorithms/AlgorithmRegistry.h"

#include <algorithm>
#include <memory>

namespace mgv {

namespace {
constexpr int kDx[4] = {  1, -1,  0,  0 };
constexpr int kDy[4] = {  0,  0,  1, -1 };
} // namespace

void WilsonMazeGenerator::reset(MapData& map, const GeneratorConfig& config) {
    settings_ = config.classicMaze;
    grid_.init(config.width, config.depth, settings_.cellSpacing);
    rng_.seed(config.seed);

    carved_.assign(static_cast<size_t>(grid_.cols) * grid_.rows, 0u);
    walkIndex_.assign(carved_.size(), -1);
    walk_.clear();
    hasWalk_  = false;
    remaining_ = static_cast<uint32_t>(carved_.size());

    finished_   = false;
    stepsTaken_ = 0;
    status_     = "Ready";

    fillWallMap(map, grid_, settings_.wallHeight);

    // Seed with a single node (added to the tree). The walks build from there.
    std::uniform_int_distribution<uint32_t> cdist(0, grid_.cols - 1);
    std::uniform_int_distribution<uint32_t> rdist(0, grid_.rows - 1);
    const uint32_t sc = cdist(rng_);
    const uint32_t sr = rdist(rng_);
    carved_[grid_.nodeIndex(sc, sr)] = 1u;
    --remaining_;
    carveBrush(map, grid_, grid_.cellX(sc), grid_.cellY(sr),
               settings_.corridorWidth, CellType::Floor,
               settings_.floorHeight, stepsTaken_);
}

void WilsonMazeGenerator::clearWalkVisualization(MapData& map) {
    for (const auto& [c, r] : walk_) {
        if (!carved_[grid_.nodeIndex(c, r)]) {
            carveBrush(map, grid_, grid_.cellX(c), grid_.cellY(r),
                       settings_.corridorWidth, CellType::Wall,
                       settings_.wallHeight, stepsTaken_);
        }
    }
    walk_.clear();
}

bool WilsonMazeGenerator::startNextWalk(MapData& map) {
    if (remaining_ == 0) return false;
    // Pick a random uncarved node.
    std::vector<uint32_t> candidates;
    candidates.reserve(remaining_);
    for (uint32_t i = 0; i < carved_.size(); ++i) {
        if (!carved_[i]) candidates.push_back(i);
    }
    if (candidates.empty()) return false;
    std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
    const uint32_t idx = candidates[pick(rng_)];
    const uint32_t row = idx / grid_.cols;
    const uint32_t col = idx % grid_.cols;
    walker_ = { col, row };
    walk_.clear();
    walk_.push_back(walker_);
    walkIndex_.assign(walkIndex_.size(), -1);
    walkIndex_[idx] = 0;
    hasWalk_ = true;

    if (settings_.showFrontier) {
        carveBrush(map, grid_, grid_.cellX(col), grid_.cellY(row),
                   settings_.corridorWidth, CellType::Frontier,
                   settings_.frontierHeight, stepsTaken_);
    }
    markCell(map, grid_.cellX(col), grid_.cellY(row),
             CellType::Current, settings_.currentHeight);
    return true;
}

void WilsonMazeGenerator::commitWalk(MapData& map) {
    // walk_[0..N-1] ends at a carved node (the very last entry). Walk the
    // chain and carve each cell + the corridor between consecutive nodes.
    for (size_t i = 0; i + 1 < walk_.size(); ++i) {
        const auto [ac, ar] = walk_[i];
        const auto [bc, br] = walk_[i + 1];
        if (!carved_[grid_.nodeIndex(ac, ar)]) {
            carved_[grid_.nodeIndex(ac, ar)] = 1u;
            if (remaining_ > 0) --remaining_;
        }
        carveLine(map, grid_,
                  grid_.cellX(ac), grid_.cellY(ar),
                  grid_.cellX(bc), grid_.cellY(br),
                  settings_.corridorWidth, CellType::Floor,
                  settings_.floorHeight, stepsTaken_);
    }
    walk_.clear();
    hasWalk_ = false;
}

GeneratorStep WilsonMazeGenerator::step(MapData& map) {
    if (finished_) return { false, true, status_ };
    ++stepsTaken_;
    const uint32_t budget = std::max<uint32_t>(1, settings_.cellsPerStep);
    bool changed = false;

    for (uint32_t i = 0; i < budget; ++i) {
        if (!hasWalk_) {
            if (!startNextWalk(map)) {
                finished_ = true;
                status_   = "Maze complete";
                return { changed, true, status_ };
            }
            changed = true;
            continue;
        }

        // Random direction for the walker.
        const int dir = std::uniform_int_distribution<int>(0, 3)(rng_);
        const int32_t nc = static_cast<int32_t>(walker_.first)  + kDx[dir];
        const int32_t nr = static_cast<int32_t>(walker_.second) + kDy[dir];
        if (!grid_.nodeInBounds(nc, nr)) continue;
        const uint32_t uc = static_cast<uint32_t>(nc);
        const uint32_t ur = static_cast<uint32_t>(nr);
        const size_t idx = grid_.nodeIndex(uc, ur);

        if (carved_[idx]) {
            // Hit the existing tree — commit the loop-erased walk.
            walk_.push_back({ uc, ur });
            commitWalk(map);
            walkIndex_.assign(walkIndex_.size(), -1);
            changed = true;
            continue;
        }

        if (walkIndex_[idx] != -1) {
            // Revisited an earlier walk cell → erase the loop.
            const int eraseFrom = walkIndex_[idx] + 1;
            for (size_t j = static_cast<size_t>(eraseFrom); j < walk_.size(); ++j) {
                const auto [ec, er] = walk_[j];
                walkIndex_[grid_.nodeIndex(ec, er)] = -1;
                if (!carved_[grid_.nodeIndex(ec, er)]) {
                    carveBrush(map, grid_, grid_.cellX(ec), grid_.cellY(er),
                               settings_.corridorWidth, CellType::Wall,
                               settings_.wallHeight, stepsTaken_);
                }
            }
            walk_.resize(static_cast<size_t>(eraseFrom));
            walker_ = walk_.back();
            markCell(map, grid_.cellX(walker_.first), grid_.cellY(walker_.second),
                     CellType::Current, settings_.currentHeight);
            changed = true;
            continue;
        }

        // Extend the walk into an unvisited cell.
        walker_ = { uc, ur };
        walkIndex_[idx] = static_cast<int>(walk_.size());
        walk_.push_back(walker_);
        if (settings_.showFrontier) {
            carveBrush(map, grid_, grid_.cellX(uc), grid_.cellY(ur),
                       settings_.corridorWidth, CellType::Frontier,
                       settings_.frontierHeight, stepsTaken_);
        }
        markCell(map, grid_.cellX(uc), grid_.cellY(ur),
                 CellType::Current, settings_.currentHeight);
        changed = true;
    }

    status_ = "Walks remaining: " + std::to_string(remaining_) +
              "  (walk length " + std::to_string(walk_.size()) + ")";
    return { changed, false, status_ };
}

void registerWilsonMazeGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "wilson_maze",
        .name        = "Wilson's Algorithm",
        .description = "Uniform spanning-tree maze via loop-erased random walks. Walker advances "
                       "one cell per step; revisits erase the resulting loop; hitting the carved "
                       "set commits the walk.",
        .category    = "Maze",
        .family      = "Spanning Tree (Uniform)",
        .useCase     = "Unbiased mazes for research, beautiful loop-erasure visualization.",
        .priority    = 12,
        .create      = [] { return std::make_unique<WilsonMazeGenerator>(); }
    });
}

} // namespace mgv
