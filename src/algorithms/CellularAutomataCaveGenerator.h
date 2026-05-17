#pragma once

#include "algorithms/IMapGenerator.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

// True step-by-step Cellular Automata cave. Two visible phases:
//   1. InitFill   — cells per step are seeded with the initial wall chance
//   2. Iterating  — applies the 4-5 rule cell by cell across N passes; each
//                   visited cell is rewritten into the map as the cursor moves.
class CellularAutomataCaveGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "cellular_automata_cave"; }
    std::string_view name() const override { return "Cellular Automata Cave"; }
    std::string_view description() const override {
        return "Random fill followed by smoothing passes (birth/death rule). Watch each pass "
               "ripple through the grid as the cursor advances.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return phase_ == Phase::Done; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    enum class Phase { InitFill, Iterating, Done };

    void writeCell(MapData& map, uint32_t x, uint32_t y, bool wall);
    int  wallNeighbors(uint32_t x, uint32_t y) const;

    CellularAutomataSettings settings_;
    uint32_t width_ = 1;
    uint32_t depth_ = 1;
    uint32_t cursor_ = 0;
    uint32_t iteration_ = 0;
    uint64_t stepsTaken_ = 0;
    Phase    phase_ = Phase::InitFill;
    std::string status_ = "Ready";

    std::mt19937 rng_;
    std::vector<uint8_t> current_;   // 1 = wall, 0 = floor
    std::vector<uint8_t> scratch_;
};

void registerCellularAutomataCaveGenerator(AlgorithmRegistry& registry);

} // namespace mgv
