#include "algorithms/CellularAutomataCaveGenerator.h"

#include "algorithms/AlgorithmRegistry.h"
#include "map/MapData.h"

#include <algorithm>
#include <memory>

namespace mgv {

void CellularAutomataCaveGenerator::reset(MapData& map, const GeneratorConfig& config) {
    settings_ = config.cellularAutomata;
    width_    = std::max<uint32_t>(4, config.width);
    depth_    = std::max<uint32_t>(4, config.depth);
    rng_.seed(config.seed);

    cursor_     = 0;
    iteration_  = 0;
    stepsTaken_ = 0;
    phase_      = Phase::InitFill;
    status_     = "Ready";

    current_.assign(static_cast<size_t>(width_) * depth_, 0);
    scratch_.assign(static_cast<size_t>(width_) * depth_, 0);

    map.resize(width_, depth_, 1);
    // Fill with floor; InitFill will mutate cell by cell.
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            Cell& c = map.at(x, y);
            c.type   = CellType::Floor;
            c.height = settings_.floorHeight;
            c.color  = { 0, 0, 0, 0 };
        }
    }
}

int CellularAutomataCaveGenerator::wallNeighbors(uint32_t x, uint32_t y) const {
    int count = 0;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            if (ox == 0 && oy == 0) continue;
            const int nx = static_cast<int>(x) + ox;
            const int ny = static_cast<int>(y) + oy;
            if (nx < 0 || ny < 0 ||
                nx >= static_cast<int>(width_) ||
                ny >= static_cast<int>(depth_)) {
                // Treat out-of-bounds as wall (more closed caves).
                ++count;
                continue;
            }
            count += current_[static_cast<size_t>(ny) * width_ + static_cast<size_t>(nx)];
        }
    }
    return count;
}

void CellularAutomataCaveGenerator::writeCell(MapData& map, uint32_t x, uint32_t y, bool wall) {
    Cell& c = map.at(x, y);
    c.type   = wall ? CellType::Wall  : CellType::Floor;
    c.height = wall ? settings_.wallHeight : settings_.floorHeight;
    c.color  = { 0, 0, 0, 0 };
    c.flags  = wall ? 1u : 0u;
    c.metadata = static_cast<uint32_t>(stepsTaken_);
}

GeneratorStep CellularAutomataCaveGenerator::step(MapData& map) {
    ++stepsTaken_;
    const size_t total = static_cast<size_t>(width_) * depth_;
    const uint32_t budget = std::clamp<uint32_t>(settings_.cellsPerStep, 1u, 65535u);

    // --- Phase 1: random fill -------------------------------------------------
    if (phase_ == Phase::InitFill) {
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);
        bool changed = false;
        for (uint32_t i = 0; i < budget && cursor_ < total; ++i, ++cursor_) {
            const uint32_t x = static_cast<uint32_t>(cursor_ % width_);
            const uint32_t y = static_cast<uint32_t>(cursor_ / width_);
            const bool border = settings_.edgeWalls &&
                                (x == 0 || y == 0 || x == width_ - 1 || y == depth_ - 1);
            const bool wall = border || (chance(rng_) < settings_.initialWallChance);
            current_[cursor_] = wall ? 1u : 0u;
            writeCell(map, x, y, wall);
            changed = true;
        }
        if (cursor_ >= total) {
            cursor_ = 0;
            if (settings_.iterations == 0) {
                phase_  = Phase::Done;
                status_ = "Init fill complete (no iterations requested)";
                return { changed, true, status_ };
            }
            phase_  = Phase::Iterating;
            status_ = "Init fill complete; starting smoothing pass 1";
        } else {
            const int percent = static_cast<int>(100.0 * cursor_ / total);
            status_ = "Filling " + std::to_string(percent) + "%";
        }
        return { changed, false, status_ };
    }

    // --- Phase 2: smoothing iterations ---------------------------------------
    if (phase_ == Phase::Iterating) {
        bool changed = false;
        for (uint32_t i = 0; i < budget && cursor_ < total; ++i, ++cursor_) {
            const uint32_t x = static_cast<uint32_t>(cursor_ % width_);
            const uint32_t y = static_cast<uint32_t>(cursor_ / width_);
            // Edge cells are always walls if the option is enabled.
            const bool border = settings_.edgeWalls &&
                                (x == 0 || y == 0 || x == width_ - 1 || y == depth_ - 1);

            const int neighbours = wallNeighbors(x, y);
            const uint8_t cur = current_[cursor_];
            uint8_t next;
            if (border) {
                next = 1u;
            } else if (cur) {
                next = (neighbours < static_cast<int>(settings_.deathLimit)) ? 0u : 1u;
            } else {
                next = (neighbours > static_cast<int>(settings_.birthLimit)) ? 1u : 0u;
            }
            scratch_[cursor_] = next;
            // Write immediately so the user sees the new state appear cell-by-cell.
            writeCell(map, x, y, next != 0);
            changed = true;
        }

        if (cursor_ >= total) {
            std::swap(current_, scratch_);
            ++iteration_;
            cursor_ = 0;
            if (iteration_ >= settings_.iterations) {
                phase_  = Phase::Done;
                status_ = "Smoothing complete (" + std::to_string(iteration_) + " passes)";
                return { changed, true, status_ };
            }
            status_ = "Smoothing pass " + std::to_string(iteration_ + 1) + " of "
                      + std::to_string(settings_.iterations);
        } else {
            status_ = "Pass " + std::to_string(iteration_ + 1) + ": "
                      + std::to_string(static_cast<int>(100.0 * cursor_ / total)) + "%";
        }
        return { changed, false, status_ };
    }

    return { false, true, "Done" };
}

void registerCellularAutomataCaveGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "cellular_automata_cave",
        .name        = "Cellular Automata Cave",
        .description = "Random fill plus N smoothing passes (4-5 birth/death rule). Each step "
                       "advances the cursor through the current pass so the smoothing pattern is "
                       "visible cell by cell.",
        .category    = "Cave",
        .family      = "Cellular Automata",
        .useCase     = "Organic caverns, natural underground systems, biological-looking spaces.",
        .priority    = 5,
        .create      = [] { return std::make_unique<CellularAutomataCaveGenerator>(); }
    });
}

} // namespace mgv
