#include "algorithms/EllerMazeGenerator.h"

#include "algorithms/AlgorithmRegistry.h"
#include "map/MapData.h"

#include <algorithm>
#include <memory>
#include <unordered_map>

namespace mgv {

void EllerMazeGenerator::reset(MapData& map, const GeneratorConfig& config) {
    settings_ = config.eller;
    spacing_  = std::max<uint32_t>(2, settings_.cellSpacing);
    width_    = std::max<uint32_t>(2 * spacing_ + 3, config.width);
    depth_    = std::max<uint32_t>(2 * spacing_ + 3, config.depth);
    rng_.seed(config.seed);

    cols_ = (width_  - 1) / spacing_;
    rows_ = (depth_  - 1) / spacing_;
    if (cols_ < 2) cols_ = 2;
    if (rows_ < 2) rows_ = 2;

    phase_       = Phase::AssignRow;
    currentRow_  = 0;
    cursor_      = 0;
    nextSetId_   = 1;
    stepsTaken_  = 0;
    status_      = "Ready";

    rowSets_.assign(cols_, 0);
    carriedDown_.assign(cols_, 0);

    map.resize(width_, depth_, 1);
    // Solid wall start; carving will reveal corridor cells.
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            Cell& c = map.at(x, y);
            c.type     = CellType::Wall;
            c.height   = settings_.wallHeight;
            c.color    = { 0, 0, 0, 0 };
            c.flags    = 0;
            c.metadata = 0;
        }
    }
}

void EllerMazeGenerator::carveBrush(MapData& map, uint32_t cx, uint32_t cy, float height) {
    const int32_t halfLo = static_cast<int32_t>((settings_.corridorWidth - 1) / 2);
    const int32_t halfHi = static_cast<int32_t>(settings_.corridorWidth / 2);
    for (int32_t oy = -halfLo; oy <= halfHi; ++oy) {
        for (int32_t ox = -halfLo; ox <= halfHi; ++ox) {
            const int32_t x = static_cast<int32_t>(cx) + ox;
            const int32_t y = static_cast<int32_t>(cy) + oy;
            if (x <= 0 || y <= 0 ||
                x >= static_cast<int32_t>(width_) - 1 ||
                y >= static_cast<int32_t>(depth_) - 1) continue;
            Cell& c = map.at(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
            c.type   = CellType::Floor;
            c.height = height;
            c.color  = { 0, 0, 0, 0 };
            c.metadata = static_cast<uint32_t>(stepsTaken_);
        }
    }
}

void EllerMazeGenerator::carveLine(MapData& map, uint32_t ax, uint32_t ay,
                                   uint32_t bx, uint32_t by) {
    int32_t x = static_cast<int32_t>(ax);
    int32_t y = static_cast<int32_t>(ay);
    const int32_t ex = static_cast<int32_t>(bx);
    const int32_t ey = static_cast<int32_t>(by);
    const int32_t sx = (ex > x) ? 1 : (ex < x ? -1 : 0);
    const int32_t sy = (ey > y) ? 1 : (ey < y ? -1 : 0);
    carveBrush(map, static_cast<uint32_t>(x), static_cast<uint32_t>(y), settings_.floorHeight);
    while (x != ex || y != ey) {
        if (x != ex) x += sx;
        if (y != ey) y += sy;
        carveBrush(map, static_cast<uint32_t>(x), static_cast<uint32_t>(y), settings_.floorHeight);
    }
}

void EllerMazeGenerator::highlightCell(MapData& map, uint32_t x, uint32_t y) {
    Cell& c = map.at(x, y);
    c.type   = CellType::Current;
    c.height = std::max(c.height, settings_.currentHeight);
}

void EllerMazeGenerator::clearHighlight(MapData& map, uint32_t x, uint32_t y) {
    Cell& c = map.at(x, y);
    if (c.type == CellType::Current) {
        c.type   = CellType::Floor;
        c.height = settings_.floorHeight;
    }
}

void EllerMazeGenerator::renumberSet(int from, int to) {
    for (uint32_t i = 0; i < cols_; ++i) {
        if (rowSets_[i] == from) rowSets_[i] = to;
    }
}

GeneratorStep EllerMazeGenerator::step(MapData& map) {
    ++stepsTaken_;
    const uint32_t budget = std::max<uint32_t>(1, settings_.cellsPerStep);
    const bool isLastRow  = (currentRow_ + 1 >= rows_);
    bool changed = false;

    // ---- AssignRow ----
    if (phase_ == Phase::AssignRow) {
        for (uint32_t k = 0; k < budget && cursor_ < cols_; ++k, ++cursor_) {
            if (rowSets_[cursor_] == 0) rowSets_[cursor_] = nextSetId_++;
            const uint32_t cx = cellX(cursor_);
            const uint32_t cy = cellY(currentRow_);
            carveBrush(map, cx, cy, settings_.floorHeight);
            changed = true;
        }
        if (cursor_ >= cols_) {
            cursor_ = 0;
            phase_  = Phase::MergeRight;
            status_ = "Row " + std::to_string(currentRow_ + 1) + ": merging";
        } else {
            status_ = "Row " + std::to_string(currentRow_ + 1) + ": assigning sets";
        }
        return { changed, false, status_ };
    }

    // ---- MergeRight ----
    if (phase_ == Phase::MergeRight) {
        std::uniform_real_distribution<float> roll(0.0f, 1.0f);
        for (uint32_t k = 0; k < budget && cursor_ + 1 < cols_; ++k, ++cursor_) {
            const int a = rowSets_[cursor_];
            const int b = rowSets_[cursor_ + 1];
            if (a == b) continue;
            const bool forceMerge = isLastRow;
            if (forceMerge || roll(rng_) < settings_.horizontalMergeChance) {
                carveLine(map, cellX(cursor_), cellY(currentRow_),
                              cellX(cursor_ + 1), cellY(currentRow_));
                renumberSet(b, a);
                changed = true;
            }
        }
        if (cursor_ + 1 >= cols_) {
            cursor_ = 0;
            if (isLastRow) {
                phase_  = Phase::Done;
                status_ = "Maze complete";
                return { changed, true, status_ };
            }
            phase_  = Phase::CarveDown;
            std::fill(carriedDown_.begin(), carriedDown_.end(), 0u);
            // Plan: pick at least one cell per set that gets carried down.
            std::unordered_map<int, std::vector<uint32_t>> setToCols;
            for (uint32_t i = 0; i < cols_; ++i) setToCols[rowSets_[i]].push_back(i);
            std::uniform_real_distribution<float> extra(0.0f, 1.0f);
            for (auto& [setId, columns] : setToCols) {
                (void)setId;
                std::shuffle(columns.begin(), columns.end(), rng_);
                if (columns.empty()) continue;
                carriedDown_[columns.front()] = 1u;
                for (size_t i = 1; i < columns.size(); ++i) {
                    if (extra(rng_) < settings_.verticalCarryChance) {
                        carriedDown_[columns[i]] = 1u;
                    }
                }
            }
            status_ = "Row " + std::to_string(currentRow_ + 1) + ": carving down";
        } else {
            status_ = "Row " + std::to_string(currentRow_ + 1) + ": merging";
        }
        return { changed, false, status_ };
    }

    // ---- CarveDown ----
    if (phase_ == Phase::CarveDown) {
        for (uint32_t k = 0; k < budget && cursor_ < cols_; ++k, ++cursor_) {
            const uint32_t cx = cellX(cursor_);
            const uint32_t cy = cellY(currentRow_);
            const uint32_t ny = cellY(currentRow_ + 1);
            if (carriedDown_[cursor_]) {
                carveLine(map, cx, cy, cx, ny);
                changed = true;
            }
        }
        if (cursor_ >= cols_) {
            cursor_ = 0;
            phase_  = Phase::AdvanceRow;
            status_ = "Advancing to row " + std::to_string(currentRow_ + 2);
        } else {
            status_ = "Row " + std::to_string(currentRow_ + 1) + ": carving down";
        }
        return { changed, false, status_ };
    }

    // ---- AdvanceRow ----
    if (phase_ == Phase::AdvanceRow) {
        // Cells that were NOT carried down lose their set assignment in the
        // next row; cells that were carried keep theirs.
        for (uint32_t i = 0; i < cols_; ++i) {
            if (!carriedDown_[i]) rowSets_[i] = 0;
        }
        std::fill(carriedDown_.begin(), carriedDown_.end(), 0u);
        ++currentRow_;
        cursor_ = 0;
        phase_  = (currentRow_ >= rows_) ? Phase::Done : Phase::AssignRow;
        status_ = phase_ == Phase::Done
                    ? "Maze complete"
                    : "Row " + std::to_string(currentRow_ + 1) + ": new row";
        return { false, phase_ == Phase::Done, status_ };
    }

    return { false, true, "Done" };
}

void registerEllerMazeGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "eller_maze",
        .name        = "Eller's Algorithm",
        .description = "Streaming row-by-row maze. Maintains disjoint sets across the current row "
                       "only — assigns new sets, merges adjacent ones, and carries at least one cell "
                       "per set down to the next row. Final row force-merges everything for "
                       "connectivity.",
        .category    = "Maze",
        .family      = "Streaming",
        .useCase     = "Infinite-strip mazes, low-memory generation, classroom demo of disjoint sets.",
        .priority    = 7,
        .create      = [] { return std::make_unique<EllerMazeGenerator>(); }
    });
}

} // namespace mgv
