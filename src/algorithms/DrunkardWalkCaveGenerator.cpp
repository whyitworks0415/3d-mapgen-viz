#include "algorithms/DrunkardWalkCaveGenerator.h"

#include "algorithms/AlgorithmRegistry.h"
#include "map/MapData.h"

#include <algorithm>
#include <memory>

namespace mgv {

namespace {
constexpr int kDx[4] = {  1, -1,  0,  0 };
constexpr int kDy[4] = {  0,  0,  1, -1 };
} // namespace

void DrunkardWalkCaveGenerator::reset(MapData& map, const GeneratorConfig& config) {
    settings_ = config.drunkardWalk;
    width_    = std::max<uint32_t>(8, config.width);
    depth_    = std::max<uint32_t>(8, config.depth);
    rng_.seed(config.seed);

    carved_.assign(static_cast<size_t>(width_) * depth_, 0u);
    walkers_.clear();
    walkers_.resize(std::clamp<uint32_t>(settings_.walkerCount, 1, 64));

    finished_    = false;
    stepsTaken_  = 0;
    carvedCount_ = 0;
    walkerSteps_ = 0;
    status_      = "Ready";

    // Solid wall fill.
    map.resize(width_, depth_, 1);
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            Cell& c = map.at(x, y);
            c.type   = CellType::Wall;
            c.height = settings_.wallHeight;
            c.color  = { 0, 0, 0, 0 };
        }
    }

    const int32_t cx = static_cast<int32_t>(width_) / 2;
    const int32_t cy = static_cast<int32_t>(depth_) / 2;
    std::uniform_int_distribution<int32_t> distX(1, static_cast<int32_t>(width_) - 2);
    std::uniform_int_distribution<int32_t> distY(1, static_cast<int32_t>(depth_) - 2);
    std::uniform_int_distribution<int>     distDir(0, 3);

    for (Walker& w : walkers_) {
        if (settings_.spawnFromCenter) {
            w.x = cx;
            w.y = cy;
        } else {
            w.x = distX(rng_);
            w.y = distY(rng_);
        }
        w.dir = distDir(rng_);
        carveAt(map, w.x, w.y, /*highlight=*/true);
    }
}

void DrunkardWalkCaveGenerator::carveAt(MapData& map, int32_t x, int32_t y, bool highlight) {
    const int32_t r = static_cast<int32_t>(settings_.brushRadius);
    for (int32_t oy = -r; oy <= r; ++oy) {
        for (int32_t ox = -r; ox <= r; ++ox) {
            const int32_t nx = x + ox;
            const int32_t ny = y + oy;
            if (nx <= 0 || ny <= 0 ||
                nx >= static_cast<int32_t>(width_) - 1 ||
                ny >= static_cast<int32_t>(depth_) - 1) continue;
            const size_t idx = static_cast<size_t>(ny) * width_ + static_cast<size_t>(nx);
            if (!carved_[idx]) {
                carved_[idx] = 1u;
                ++carvedCount_;
            }
            Cell& c = map.at(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny));
            c.type   = CellType::Floor;
            c.height = settings_.floorHeight;
            c.color  = { 0, 0, 0, 0 };
        }
    }
    if (highlight) {
        Cell& c = map.at(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        c.type   = CellType::Current;
        c.height = std::max(c.height, settings_.floorHeight + 0.18f);
    }
}

GeneratorStep DrunkardWalkCaveGenerator::step(MapData& map) {
    if (finished_) return { false, true, status_ };
    ++stepsTaken_;

    const size_t total = static_cast<size_t>(width_) * depth_;
    const float currentFill = total > 0 ? static_cast<float>(carvedCount_) / total : 0.0f;
    if (currentFill >= settings_.targetFill || walkerSteps_ >= settings_.maxSteps) {
        // Final cleanup: clear walker highlights.
        for (const Walker& w : walkers_) {
            if (w.x >= 0 && w.y >= 0 &&
                w.x < static_cast<int32_t>(width_) &&
                w.y < static_cast<int32_t>(depth_)) {
                Cell& c = map.at(static_cast<uint32_t>(w.x), static_cast<uint32_t>(w.y));
                if (c.type == CellType::Current) {
                    c.type   = CellType::Floor;
                    c.height = settings_.floorHeight;
                }
            }
        }
        finished_ = true;
        status_   = "Cave complete (" +
                    std::to_string(static_cast<int>(currentFill * 100.0f)) + "% filled)";
        return { true, true, status_ };
    }

    const uint32_t budget = std::max<uint32_t>(1, settings_.cellsPerStep);
    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    std::uniform_int_distribution<int>    distDir(0, 3);

    bool changed = false;
    for (uint32_t k = 0; k < budget && walkerSteps_ < settings_.maxSteps; ++k, ++walkerSteps_) {
        for (Walker& w : walkers_) {
            // Clear previous highlight.
            if (w.x > 0 && w.y > 0 &&
                w.x < static_cast<int32_t>(width_) - 1 &&
                w.y < static_cast<int32_t>(depth_) - 1) {
                Cell& c = map.at(static_cast<uint32_t>(w.x), static_cast<uint32_t>(w.y));
                if (c.type == CellType::Current) {
                    c.type   = CellType::Floor;
                    c.height = settings_.floorHeight;
                }
            }

            if (roll(rng_) < settings_.turnChance) w.dir = distDir(rng_);

            int32_t nx = w.x + kDx[w.dir];
            int32_t ny = w.y + kDy[w.dir];
            if (nx <= 0 || ny <= 0 ||
                nx >= static_cast<int32_t>(width_) - 1 ||
                ny >= static_cast<int32_t>(depth_) - 1) {
                // Bounce: pick a new direction.
                w.dir = distDir(rng_);
                nx = std::clamp<int32_t>(w.x + kDx[w.dir], 1, static_cast<int32_t>(width_) - 2);
                ny = std::clamp<int32_t>(w.y + kDy[w.dir], 1, static_cast<int32_t>(depth_) - 2);
            }
            w.x = nx;
            w.y = ny;
            carveAt(map, w.x, w.y, /*highlight=*/true);
            changed = true;
        }
    }

    const float pct = static_cast<float>(carvedCount_) / static_cast<float>(total) * 100.0f;
    status_ = "Filled " + std::to_string(static_cast<int>(pct)) +
              "% / target " + std::to_string(static_cast<int>(settings_.targetFill * 100.0f)) + "%";
    return { changed, false, status_ };
}

void registerDrunkardWalkCaveGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "drunkard_walk_cave",
        .name        = "Drunkard's Walk Cave",
        .description = "Agent-based cave. One or more walkers wander, carving floor as they go. "
                       "Step-by-step shows each walker move; generation stops when targetFill is "
                       "reached or maxSteps is exhausted.",
        .category    = "Cave",
        .family      = "Agent",
        .useCase     = "Twisting tunnels, mining-game maps, organic underground systems.",
        .priority    = 15,
        .create      = [] { return std::make_unique<DrunkardWalkCaveGenerator>(); }
    });
}

} // namespace mgv
