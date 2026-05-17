#pragma once

#include "algorithms/IMapGenerator.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

// True row-by-row Eller's algorithm. Per step, advances one sub-phase of the
// current row:
//   AssignRow    — every unassigned cell in the row gets a fresh set id
//   MergeRight   — pair-by-pair, decide whether to merge adjacent sets
//   CarveDown    — for each set still active in the row, pick at least one
//                  cell to carry down to the next row (extras based on
//                  verticalCarryChance)
//   AdvanceRow   — clear non-carried cells, move to next row
// Final row forces every adjacent merge so the maze becomes fully connected.
class EllerMazeGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "eller_maze"; }
    std::string_view name() const override { return "Eller's Algorithm"; }
    std::string_view description() const override {
        return "Row-by-row maze generation using disjoint sets. Each row is processed in three "
               "sub-phases — assign new sets, merge horizontally, carry sets down — visible step "
               "by step.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return phase_ == Phase::Done; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    enum class Phase { AssignRow, MergeRight, CarveDown, AdvanceRow, Done };

    void carveBrush(MapData& map, uint32_t centerX, uint32_t centerY, float height);
    void carveLine(MapData& map, uint32_t ax, uint32_t ay, uint32_t bx, uint32_t by);
    void highlightCell(MapData& map, uint32_t x, uint32_t y);
    void clearHighlight(MapData& map, uint32_t x, uint32_t y);
    void renumberSet(int from, int to);

    uint32_t cellX(uint32_t col) const { return 1u + col * spacing_; }
    uint32_t cellY(uint32_t row) const { return 1u + row * spacing_; }

    EllerSettings settings_;
    uint32_t width_   = 1;
    uint32_t depth_   = 1;
    uint32_t spacing_ = 2;
    uint32_t cols_    = 0;
    uint32_t rows_    = 0;

    Phase    phase_      = Phase::AssignRow;
    uint64_t stepsTaken_ = 0;
    std::string status_  = "Ready";

    uint32_t currentRow_   = 0;
    uint32_t cursor_       = 0;
    int      nextSetId_    = 1;

    std::vector<int>     rowSets_;          // setId per column in the current row
    std::vector<uint8_t> carriedDown_;      // which cells will be carried into the next row

    std::mt19937 rng_;
};

void registerEllerMazeGenerator(AlgorithmRegistry& registry);

} // namespace mgv
