#include "algorithms/HuntAndKillMazeGenerator.h"

#include "algorithms/AlgorithmRegistry.h"

#include <algorithm>
#include <array>
#include <memory>

namespace mgv {

namespace {
constexpr int kDx[4] = {  1, -1,  0,  0 };
constexpr int kDy[4] = {  0,  0,  1, -1 };
} // namespace

void HuntAndKillMazeGenerator::reset(MapData& map, const GeneratorConfig& config) {
    settings_ = config.classicMaze;
    grid_.init(config.width, config.depth, settings_.cellSpacing);
    rng_.seed(config.seed);

    carved_.assign(static_cast<size_t>(grid_.cols) * grid_.rows, 0u);
    huntRow_ = 0;
    huntCol_ = 0;
    phase_   = Phase::Walking;

    finished_   = false;
    stepsTaken_ = 0;
    status_     = "Ready";

    fillWallMap(map, grid_, settings_.wallHeight);

    walkerCol_ = std::uniform_int_distribution<uint32_t>(0, grid_.cols - 1)(rng_);
    walkerRow_ = std::uniform_int_distribution<uint32_t>(0, grid_.rows - 1)(rng_);
    carved_[grid_.nodeIndex(walkerCol_, walkerRow_)] = 1u;
    carveBrush(map, grid_, grid_.cellX(walkerCol_), grid_.cellY(walkerRow_),
               settings_.corridorWidth, CellType::Floor,
               settings_.floorHeight, stepsTaken_);
    markCell(map, grid_.cellX(walkerCol_), grid_.cellY(walkerRow_),
             CellType::Current, settings_.currentHeight);
}

int HuntAndKillMazeGenerator::pickUncarvedNeighbour(uint32_t col, uint32_t row) {
    std::array<int, 4> dirs = { 0, 1, 2, 3 };
    std::shuffle(dirs.begin(), dirs.end(), rng_);
    for (int d : dirs) {
        const int32_t nc = static_cast<int32_t>(col) + kDx[d];
        const int32_t nr = static_cast<int32_t>(row) + kDy[d];
        if (!grid_.nodeInBounds(nc, nr)) continue;
        if (!carved_[grid_.nodeIndex(static_cast<uint32_t>(nc),
                                     static_cast<uint32_t>(nr))]) {
            return d;
        }
    }
    return -1;
}

bool HuntAndKillMazeGenerator::advanceHunt(MapData& map) {
    // Scan row by row for an uncarved cell with at least one carved neighbour.
    for (; huntRow_ < grid_.rows; ++huntRow_) {
        for (; huntCol_ < grid_.cols; ++huntCol_) {
            const size_t idx = grid_.nodeIndex(huntCol_, huntRow_);
            if (carved_[idx]) continue;
            // Look for a carved neighbour.
            int connectDir = -1;
            std::array<int, 4> dirs = { 0, 1, 2, 3 };
            std::shuffle(dirs.begin(), dirs.end(), rng_);
            for (int d : dirs) {
                const int32_t nc = static_cast<int32_t>(huntCol_) + kDx[d];
                const int32_t nr = static_cast<int32_t>(huntRow_) + kDy[d];
                if (!grid_.nodeInBounds(nc, nr)) continue;
                if (carved_[grid_.nodeIndex(static_cast<uint32_t>(nc),
                                            static_cast<uint32_t>(nr))]) {
                    connectDir = d;
                    break;
                }
            }
            if (connectDir < 0) continue;

            const uint32_t nc = static_cast<uint32_t>(static_cast<int32_t>(huntCol_) + kDx[connectDir]);
            const uint32_t nr = static_cast<uint32_t>(static_cast<int32_t>(huntRow_) + kDy[connectDir]);

            // Carve link from the carved neighbour into this cell.
            carved_[idx] = 1u;
            carveLine(map, grid_, grid_.cellX(huntCol_), grid_.cellY(huntRow_),
                                  grid_.cellX(nc),      grid_.cellY(nr),
                      settings_.corridorWidth, CellType::Floor,
                      settings_.floorHeight, stepsTaken_);
            walkerCol_ = huntCol_;
            walkerRow_ = huntRow_;
            markCell(map, grid_.cellX(walkerCol_), grid_.cellY(walkerRow_),
                     CellType::Current, settings_.currentHeight);
            phase_ = Phase::Walking;
            return true;
        }
        huntCol_ = 0;
    }
    return false;
}

GeneratorStep HuntAndKillMazeGenerator::step(MapData& map) {
    if (finished_) return { false, true, status_ };
    ++stepsTaken_;
    const uint32_t budget = std::max<uint32_t>(1, settings_.cellsPerStep);
    bool changed = false;

    for (uint32_t i = 0; i < budget; ++i) {
        if (phase_ == Phase::Walking) {
            const int dir = pickUncarvedNeighbour(walkerCol_, walkerRow_);
            if (dir < 0) {
                phase_   = Phase::Hunting;
                huntRow_ = 0;
                huntCol_ = 0;
                markCell(map, grid_.cellX(walkerCol_), grid_.cellY(walkerRow_),
                         CellType::Floor, settings_.floorHeight);
                status_ = "Hunting...";
                continue;
            }
            const uint32_t nc = static_cast<uint32_t>(static_cast<int32_t>(walkerCol_) + kDx[dir]);
            const uint32_t nr = static_cast<uint32_t>(static_cast<int32_t>(walkerRow_) + kDy[dir]);
            carveLine(map, grid_, grid_.cellX(walkerCol_), grid_.cellY(walkerRow_),
                                  grid_.cellX(nc), grid_.cellY(nr),
                      settings_.corridorWidth, CellType::Floor,
                      settings_.floorHeight, stepsTaken_);
            carved_[grid_.nodeIndex(nc, nr)] = 1u;
            markCell(map, grid_.cellX(walkerCol_), grid_.cellY(walkerRow_),
                     CellType::Floor, settings_.floorHeight);
            walkerCol_ = nc;
            walkerRow_ = nr;
            markCell(map, grid_.cellX(walkerCol_), grid_.cellY(walkerRow_),
                     CellType::Current, settings_.currentHeight);
            changed = true;
            continue;
        }
        if (phase_ == Phase::Hunting) {
            if (!advanceHunt(map)) {
                phase_    = Phase::Done;
                finished_ = true;
                status_   = "Maze complete";
                return { changed, true, status_ };
            }
            changed = true;
            continue;
        }
        // Done.
        finished_ = true;
        return { changed, true, status_ };
    }

    if (phase_ == Phase::Walking) {
        status_ = "Walking at (" + std::to_string(walkerCol_) + "," + std::to_string(walkerRow_) + ")";
    }
    return { changed, false, status_ };
}

void registerHuntAndKillMazeGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "hunt_and_kill_maze",
        .name        = "Hunt-and-Kill Algorithm",
        .description = "Random walk that carves into uncarved neighbours; when stuck, scans the "
                       "grid row by row for an uncarved cell adjacent to a carved one and "
                       "resumes from there.",
        .category    = "Maze",
        .family      = "Walk + Hunt",
        .useCase     = "Long winding passages, classic roguelike corridors.",
        .priority    = 14,
        .create      = [] { return std::make_unique<HuntAndKillMazeGenerator>(); }
    });
}

} // namespace mgv
