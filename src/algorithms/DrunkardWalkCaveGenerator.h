#pragma once

#include "algorithms/IMapGenerator.h"

#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

// True step-by-step Drunkard's Walk cave. One walker move per step (or
// cellsPerStep moves per public step()). Walkers bounce inside the bounds,
// occasionally turn, and carve floor at their current position. Generation
// ends when the requested floor fraction is reached or maxSteps is exhausted.
class DrunkardWalkCaveGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "drunkard_walk_cave"; }
    std::string_view name() const override { return "Drunkard's Walk Cave"; }
    std::string_view description() const override {
        return "One or more walkers wander randomly, carving floor as they go. Each step moves "
               "every walker one cell; the cave shape emerges live.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return finished_; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    struct Walker {
        int32_t x = 0;
        int32_t y = 0;
        int     dir = 0;   // 0..3
    };

    void carveAt(MapData& map, int32_t x, int32_t y, bool highlight);

    DrunkardWalkSettings settings_;
    uint32_t width_ = 1;
    uint32_t depth_ = 1;
    uint32_t carvedCount_ = 0;
    uint32_t walkerSteps_ = 0;

    std::vector<Walker>  walkers_;
    std::vector<uint8_t> carved_;
    std::mt19937 rng_;

    bool        finished_   = false;
    uint64_t    stepsTaken_ = 0;
    std::string status_     = "Ready";
};

void registerDrunkardWalkCaveGenerator(AlgorithmRegistry& registry);

} // namespace mgv
