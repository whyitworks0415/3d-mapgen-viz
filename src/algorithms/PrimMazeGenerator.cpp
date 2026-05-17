#include "algorithms/PrimMazeGenerator.h"

#include "algorithms/AlgorithmRegistry.h"

#include <algorithm>
#include <memory>

namespace mgv {

namespace {
constexpr int kDx[4] = {  1, -1,  0,  0 };
constexpr int kDy[4] = {  0,  0,  1, -1 };
} // namespace

void PrimMazeGenerator::reset(MapData& map, const GeneratorConfig& config) {
    settings_ = config.classicMaze;
    grid_.init(config.width, config.depth, settings_.cellSpacing);
    rng_.seed(config.seed);

    carved_.assign(static_cast<size_t>(grid_.cols) * grid_.rows, 0u);
    frontier_.clear();
    finished_   = false;
    stepsTaken_ = 0;
    status_     = "Ready";

    fillWallMap(map, grid_, settings_.wallHeight);

    // Seed with one node.
    std::uniform_int_distribution<uint32_t> cdist(0, grid_.cols - 1);
    std::uniform_int_distribution<uint32_t> rdist(0, grid_.rows - 1);
    const uint32_t sc = cdist(rng_);
    const uint32_t sr = rdist(rng_);
    carved_[grid_.nodeIndex(sc, sr)] = 1u;
    carveBrush(map, grid_, grid_.cellX(sc), grid_.cellY(sr), settings_.corridorWidth,
               CellType::Floor, settings_.floorHeight, stepsTaken_);
    addFrontier(map, sc, sr);
}

void PrimMazeGenerator::addFrontier(MapData& map, uint32_t col, uint32_t row) {
    for (int d = 0; d < 4; ++d) {
        const int32_t nc = static_cast<int32_t>(col) + kDx[d];
        const int32_t nr = static_cast<int32_t>(row) + kDy[d];
        if (!grid_.nodeInBounds(nc, nr)) continue;
        const uint32_t uc = static_cast<uint32_t>(nc);
        const uint32_t ur = static_cast<uint32_t>(nr);
        if (carved_[grid_.nodeIndex(uc, ur)]) continue;
        frontier_.push_back({ col, row, uc, ur });
        if (settings_.showFrontier) {
            // Paint the candidate node in Frontier colour.
            carveBrush(map, grid_, grid_.cellX(uc), grid_.cellY(ur),
                       settings_.corridorWidth,
                       CellType::Frontier, settings_.frontierHeight, stepsTaken_);
        }
    }
}

GeneratorStep PrimMazeGenerator::step(MapData& map) {
    if (finished_) return { false, true, status_ };
    ++stepsTaken_;
    const uint32_t budget = std::max<uint32_t>(1, settings_.cellsPerStep);
    bool changed = false;

    std::uniform_real_distribution<float> roll(0.0f, 1.0f);

    for (uint32_t i = 0; i < budget; ++i) {
        if (frontier_.empty()) {
            finished_ = true;
            status_   = "Maze complete";
            return { changed, true, status_ };
        }
        std::uniform_int_distribution<size_t> pick(0, frontier_.size() - 1);
        const size_t idx = pick(rng_);
        const Edge edge = frontier_[idx];
        frontier_[idx] = frontier_.back();
        frontier_.pop_back();

        if (carved_[grid_.nodeIndex(edge.bx, edge.by)]) {
            // Already carved — optional braiding opens an extra passage.
            if (settings_.loopChance > 0.0f && roll(rng_) < settings_.loopChance) {
                carveLine(map, grid_,
                          grid_.cellX(edge.ax), grid_.cellY(edge.ay),
                          grid_.cellX(edge.bx), grid_.cellY(edge.by),
                          settings_.corridorWidth, CellType::Floor,
                          settings_.floorHeight, stepsTaken_);
                changed = true;
            }
            continue;
        }

        carved_[grid_.nodeIndex(edge.bx, edge.by)] = 1u;
        carveLine(map, grid_,
                  grid_.cellX(edge.ax), grid_.cellY(edge.ay),
                  grid_.cellX(edge.bx), grid_.cellY(edge.by),
                  settings_.corridorWidth, CellType::Floor,
                  settings_.floorHeight, stepsTaken_);
        // Highlight just-carved node.
        markCell(map, grid_.cellX(edge.bx), grid_.cellY(edge.by),
                 CellType::Current, settings_.currentHeight);
        addFrontier(map, edge.bx, edge.by);
        changed = true;
    }

    status_ = "Frontier " + std::to_string(frontier_.size());
    return { changed, false, status_ };
}

void registerPrimMazeGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "randomized_prim_maze",
        .name        = "Randomized Prim Maze",
        .description = "Frontier-growth maze. Each step pops one random frontier wall and either "
                       "carves through it or, on collision, optionally adds a braid.",
        .category    = "Maze",
        .family      = "Spanning Tree",
        .useCase     = "Mazes with many short branches and a clear growing frontier.",
        .priority    = 10,
        .create      = [] { return std::make_unique<PrimMazeGenerator>(); }
    });
}

} // namespace mgv
