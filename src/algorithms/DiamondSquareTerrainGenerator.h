#pragma once

#include "algorithms/IMapGenerator.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

// True step-by-step Diamond-Square. Each step performs one "diamond" or
// "square" sub-step at the current resolution; you see height information
// propagate from the four corners inwards through halving step sizes.
class DiamondSquareTerrainGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "diamond_square_terrain"; }
    std::string_view name() const override { return "Diamond-Square Terrain"; }
    std::string_view description() const override {
        return "Classic fractal midpoint displacement. Each step performs one diamond or square "
               "averaging at the current resolution; you can watch the heightmap densify "
               "level by level.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return phase_ == Phase::Done; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    enum class Phase { Diamond, Square, NextLevel, Finalise, Done };

    float sampleWrap(int32_t x, int32_t y) const;
    void  writeHeight(MapData& map, uint32_t x, uint32_t y, float h);
    void  paintTerrainCell(MapData& map, uint32_t x, uint32_t y, float h);
    void  prepareNextLevel();

    DiamondSquareSettings settings_;
    uint32_t side_ = 1;          // working square: side+1 x side+1
    uint32_t step_ = 1;          // current grid step size
    uint32_t width_ = 1;
    uint32_t depth_ = 1;
    Phase    phase_ = Phase::Diamond;

    struct DiamondTask { uint32_t cx, cy, half; };
    struct SquareTask  { uint32_t cx, cy, half; };

    std::vector<DiamondTask> diamondQueue_;
    size_t   diamondCursor_ = 0;
    std::vector<SquareTask> squareQueue_;
    size_t   squareCursor_ = 0;

    std::vector<float> height_;
    float    minH_ = 1e9f;
    float    maxH_ = -1e9f;
    uint32_t levelIndex_ = 0;
    float    levelRange_ = 1.0f;

    std::mt19937 rng_;

    uint64_t    stepsTaken_ = 0;
    std::string status_     = "Ready";
};

void registerDiamondSquareTerrainGenerator(AlgorithmRegistry& registry);

} // namespace mgv
