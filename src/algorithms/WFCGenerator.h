#pragma once

#include "algorithms/IMapGenerator.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

// True step-by-step Wave Function Collapse over a small terrain tile set
// (Water, Floor, Wall, Mountain).
//   - Each `step()` collapses one min-entropy cell and propagates constraints.
//   - When a cell is permanently constrained to a single tile it is written
//     to the map and visible immediately.
//   - Contradictions cause a local retry (clear that cell + its constrained
//     neighbours) so we don't deadlock on small contradictions.
class WFCGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "simple_tiled_wfc"; }
    std::string_view name() const override { return "Simple Tiled WFC"; }
    std::string_view description() const override {
        return "Wave Function Collapse over four terrain tiles. Each step collapses one min-entropy "
               "cell, propagates constraints to neighbours, and updates the map immediately.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return phase_ == Phase::Done; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    enum class Phase { Collapsing, Done };
    enum Tile : uint8_t { TileWater = 0, TileFloor, TileWall, TileMountain, TileCount };

    static constexpr uint8_t kMaskAll = (1u << TileCount) - 1u;  // 0b1111

    void  buildAdjacency();
    void  writeCellFromMask(MapData& map, uint32_t x, uint32_t y);
    Tile  pickCollapse(uint32_t x, uint32_t y);
    bool  propagate(uint32_t startX, uint32_t startY);
    int   findMinEntropy(uint32_t& outX, uint32_t& outY) const;
    void  highlightCurrent(MapData& map, uint32_t x, uint32_t y);
    void  setMaskCell(MapData& map, uint32_t x, uint32_t y);

    WFCSettings settings_;
    uint32_t width_  = 1;
    uint32_t depth_  = 1;
    Phase    phase_  = Phase::Collapsing;
    uint64_t stepsTaken_ = 0;
    std::string status_  = "Ready";
    uint32_t remaining_ = 0;
    uint32_t currentX_ = 0;
    uint32_t currentY_ = 0;
    bool     hasCurrent_ = false;

    std::mt19937 rng_;

    // Per-cell bitmask of still-allowed tiles (LSB = TileWater).
    std::vector<uint8_t> wave_;
    // Per-cell finalised tile (255 = not yet collapsed).
    std::vector<uint8_t> collapsed_;

    // Adjacency rules: allowed[from][dir] = bitmask of allowed tiles in
    // direction `dir` (0=+X, 1=-X, 2=+Y, 3=-Y).
    uint8_t adjacency_[TileCount][4] {};
};

void registerWFCGenerator(AlgorithmRegistry& registry);

} // namespace mgv
