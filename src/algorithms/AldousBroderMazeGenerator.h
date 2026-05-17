#pragma once

#include "algorithms/IMapGenerator.h"
#include "algorithms/MazeGridUtils.h"

#include <random>
#include <string>
#include <utility>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

// True step-by-step Aldous-Broder maze. A single walker moves around the
// grid; each first-visit to a cell carves the wall it came through. Slow but
// produces uniformly distributed spanning trees.
class AldousBroderMazeGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "aldous_broder_maze"; }
    std::string_view name() const override { return "Aldous-Broder Algorithm"; }
    std::string_view description() const override {
        return "Random walker that carves into each new cell it visits. Uniform spanning tree, "
               "but slow — the visualisation shows the walker meandering until coverage is full.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return finished_; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    ClassicMazeSettings settings_;
    MazeGrid grid_;
    std::vector<uint8_t> carved_;
    uint32_t walkerCol_ = 0;
    uint32_t walkerRow_ = 0;
    uint32_t remaining_ = 0;

    std::mt19937 rng_;
    bool        finished_   = false;
    uint64_t    stepsTaken_ = 0;
    std::string status_     = "Ready";
};

void registerAldousBroderMazeGenerator(AlgorithmRegistry& registry);

} // namespace mgv
