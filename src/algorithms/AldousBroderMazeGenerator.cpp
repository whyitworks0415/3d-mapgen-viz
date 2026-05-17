#include "algorithms/AldousBroderMazeGenerator.h"

#include "algorithms/AlgorithmRegistry.h"

#include <algorithm>
#include <memory>

namespace mgv {

namespace {
constexpr int kDx[4] = {  1, -1,  0,  0 };
constexpr int kDy[4] = {  0,  0,  1, -1 };
} // namespace

void AldousBroderMazeGenerator::reset(MapData& map, const GeneratorConfig& config) {
    settings_ = config.classicMaze;
    grid_.init(config.width, config.depth, settings_.cellSpacing);
    rng_.seed(config.seed);

    carved_.assign(static_cast<size_t>(grid_.cols) * grid_.rows, 0u);
    remaining_ = static_cast<uint32_t>(carved_.size());

    finished_   = false;
    stepsTaken_ = 0;
    status_     = "Ready";

    fillWallMap(map, grid_, settings_.wallHeight);

    walkerCol_ = std::uniform_int_distribution<uint32_t>(0, grid_.cols - 1)(rng_);
    walkerRow_ = std::uniform_int_distribution<uint32_t>(0, grid_.rows - 1)(rng_);
    carved_[grid_.nodeIndex(walkerCol_, walkerRow_)] = 1u;
    --remaining_;
    carveBrush(map, grid_, grid_.cellX(walkerCol_), grid_.cellY(walkerRow_),
               settings_.corridorWidth, CellType::Floor,
               settings_.floorHeight, stepsTaken_);
    markCell(map, grid_.cellX(walkerCol_), grid_.cellY(walkerRow_),
             CellType::Current, settings_.currentHeight);
}

GeneratorStep AldousBroderMazeGenerator::step(MapData& map) {
    if (finished_) return { false, true, status_ };
    ++stepsTaken_;
    const uint32_t budget = std::max<uint32_t>(1, settings_.cellsPerStep);
    bool changed = false;

    for (uint32_t i = 0; i < budget; ++i) {
        if (remaining_ == 0) {
            finished_ = true;
            status_   = "Maze complete";
            return { changed, true, status_ };
        }
        const int dir = std::uniform_int_distribution<int>(0, 3)(rng_);
        const int32_t nc = static_cast<int32_t>(walkerCol_) + kDx[dir];
        const int32_t nr = static_cast<int32_t>(walkerRow_) + kDy[dir];
        if (!grid_.nodeInBounds(nc, nr)) continue;
        const uint32_t uc = static_cast<uint32_t>(nc);
        const uint32_t ur = static_cast<uint32_t>(nr);
        const size_t idx = grid_.nodeIndex(uc, ur);

        // Clear the old walker highlight.
        markCell(map, grid_.cellX(walkerCol_), grid_.cellY(walkerRow_),
                 CellType::Floor, settings_.floorHeight);

        if (!carved_[idx]) {
            // First visit → carve the wall we came through.
            carveLine(map, grid_, grid_.cellX(walkerCol_), grid_.cellY(walkerRow_),
                                  grid_.cellX(uc), grid_.cellY(ur),
                      settings_.corridorWidth, CellType::Floor,
                      settings_.floorHeight, stepsTaken_);
            carved_[idx] = 1u;
            --remaining_;
            changed = true;
        }

        walkerCol_ = uc;
        walkerRow_ = ur;
        markCell(map, grid_.cellX(walkerCol_), grid_.cellY(walkerRow_),
                 CellType::Current, settings_.currentHeight);
        changed = true;
    }

    status_ = "Remaining: " + std::to_string(remaining_);
    return { changed, false, status_ };
}

void registerAldousBroderMazeGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "aldous_broder_maze",
        .name        = "Aldous-Broder Algorithm",
        .description = "Single random walker that carves on first visit to each cell. Visually "
                       "shows the walker meandering until every cell has been touched.",
        .category    = "Maze",
        .family      = "Spanning Tree (Uniform)",
        .useCase     = "Simplest possible uniform-spanning-tree implementation; pedagogical demo.",
        .priority    = 13,
        .create      = [] { return std::make_unique<AldousBroderMazeGenerator>(); }
    });
}

} // namespace mgv
