#pragma once

#include "algorithms/IMapGenerator.h"
#include "algorithms/MazeGridUtils.h"

#include <random>
#include <string>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

// True step-by-step Randomized Prim's maze. One frontier edge is processed per
// inner loop iteration; cellsPerStep batches several iterations into one
// public step() call.
class PrimMazeGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "randomized_prim_maze"; }
    std::string_view name() const override { return "Randomized Prim Maze"; }
    std::string_view description() const override {
        return "Grows a maze from a single seed by repeatedly picking a random frontier wall and "
               "carving it. Frontier cells are highlighted as candidates until they're absorbed.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return finished_; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    struct Edge { uint32_t ax, ay, bx, by; };
    void addFrontier(MapData& map, uint32_t col, uint32_t row);

    ClassicMazeSettings settings_;
    MazeGrid grid_;
    std::vector<uint8_t> carved_;
    std::vector<Edge>    frontier_;
    std::mt19937 rng_;

    bool        finished_   = false;
    uint64_t    stepsTaken_ = 0;
    std::string status_     = "Ready";
};

void registerPrimMazeGenerator(AlgorithmRegistry& registry);

} // namespace mgv
