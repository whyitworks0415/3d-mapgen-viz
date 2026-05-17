#include "algorithms/KruskalMazeGenerator.h"

#include "algorithms/AlgorithmRegistry.h"

#include <algorithm>
#include <memory>
#include <numeric>

namespace mgv {

int KruskalMazeGenerator::find(int x) {
    while (parent_[static_cast<size_t>(x)] != x) {
        parent_[static_cast<size_t>(x)] =
            parent_[static_cast<size_t>(parent_[static_cast<size_t>(x)])];
        x = parent_[static_cast<size_t>(x)];
    }
    return x;
}

void KruskalMazeGenerator::unite(int a, int b) {
    int ra = find(a);
    int rb = find(b);
    if (ra == rb) return;
    if (rank_[static_cast<size_t>(ra)] < rank_[static_cast<size_t>(rb)]) std::swap(ra, rb);
    parent_[static_cast<size_t>(rb)] = ra;
    if (rank_[static_cast<size_t>(ra)] == rank_[static_cast<size_t>(rb)])
        ++rank_[static_cast<size_t>(ra)];
}

void KruskalMazeGenerator::reset(MapData& map, const GeneratorConfig& config) {
    settings_ = config.classicMaze;
    grid_.init(config.width, config.depth, settings_.cellSpacing);
    rng_.seed(config.seed);

    parent_.resize(static_cast<size_t>(grid_.cols) * grid_.rows);
    std::iota(parent_.begin(), parent_.end(), 0);
    rank_.assign(parent_.size(), 0);
    cursor_     = 0;
    finished_   = false;
    stepsTaken_ = 0;
    status_     = "Ready";

    fillWallMap(map, grid_, settings_.wallHeight);

    // Enumerate every right-edge and down-edge once, then shuffle.
    edges_.clear();
    edges_.reserve(2u * grid_.cols * grid_.rows);
    for (uint32_t r = 0; r < grid_.rows; ++r) {
        for (uint32_t c = 0; c < grid_.cols; ++c) {
            if (c + 1 < grid_.cols) edges_.push_back({ c, r, c + 1, r });
            if (r + 1 < grid_.rows) edges_.push_back({ c, r, c, r + 1 });
        }
    }
    std::shuffle(edges_.begin(), edges_.end(), rng_);

    // Carve every node up front as floor (Kruskal's only carves walls between).
    for (uint32_t r = 0; r < grid_.rows; ++r) {
        for (uint32_t c = 0; c < grid_.cols; ++c) {
            carveBrush(map, grid_, grid_.cellX(c), grid_.cellY(r),
                       settings_.corridorWidth, CellType::Floor,
                       settings_.floorHeight, 0);
        }
    }
}

GeneratorStep KruskalMazeGenerator::step(MapData& map) {
    if (finished_) return { false, true, status_ };
    ++stepsTaken_;
    const uint32_t budget = std::max<uint32_t>(1, settings_.cellsPerStep);
    bool changed = false;
    std::uniform_real_distribution<float> roll(0.0f, 1.0f);

    for (uint32_t i = 0; i < budget; ++i) {
        if (cursor_ >= edges_.size()) {
            finished_ = true;
            status_   = "Maze complete (" + std::to_string(stepsTaken_) + " steps)";
            return { changed, true, status_ };
        }

        const Edge e = edges_[cursor_++];
        const int a = static_cast<int>(grid_.nodeIndex(e.ax, e.ay));
        const int b = static_cast<int>(grid_.nodeIndex(e.bx, e.by));
        const int ra = find(a);
        const int rb = find(b);

        if (ra != rb) {
            unite(ra, rb);
            carveLine(map, grid_, grid_.cellX(e.ax), grid_.cellY(e.ay),
                                  grid_.cellX(e.bx), grid_.cellY(e.by),
                      settings_.corridorWidth, CellType::Floor,
                      settings_.floorHeight, stepsTaken_);
            markCell(map, grid_.cellX(e.bx), grid_.cellY(e.by),
                     CellType::Current, settings_.currentHeight);
            changed = true;
        } else if (settings_.loopChance > 0.0f && roll(rng_) < settings_.loopChance) {
            // Optional braid — carve anyway to add a loop.
            carveLine(map, grid_, grid_.cellX(e.ax), grid_.cellY(e.ay),
                                  grid_.cellX(e.bx), grid_.cellY(e.by),
                      settings_.corridorWidth, CellType::Path,
                      settings_.floorHeight, stepsTaken_);
            changed = true;
        }
    }

    status_ = "Edge " + std::to_string(cursor_) + " / " + std::to_string(edges_.size());
    return { changed, false, status_ };
}

void registerKruskalMazeGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "randomized_kruskal_maze",
        .name        = "Randomized Kruskal Maze",
        .description = "Shuffled edge list + union-find. Each step considers one edge and merges "
                       "two sets or rejects it. loopChance > 0 enables braiding (carve rejected "
                       "edges as Path-coloured loops).",
        .category    = "Maze",
        .family      = "Spanning Tree",
        .useCase     = "Even distribution of corridors, classroom demo of union-find.",
        .priority    = 11,
        .create      = [] { return std::make_unique<KruskalMazeGenerator>(); }
    });
}

} // namespace mgv
