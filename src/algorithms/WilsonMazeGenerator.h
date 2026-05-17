#pragma once

#include "algorithms/IMapGenerator.h"
#include "algorithms/MazeGridUtils.h"

#include <random>
#include <string>
#include <utility>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

// True step-by-step Wilson's algorithm (uniform spanning tree via
// loop-erased random walks). Each step advances the current walker one cell;
// on hitting the carved set the loop-erased path is committed; on revisiting
// its own walk path the loop is erased.
class WilsonMazeGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "wilson_maze"; }
    std::string_view name() const override { return "Wilson's Algorithm"; }
    std::string_view description() const override {
        return "Uniform spanning-tree maze using loop-erased random walks. Watch the walker "
               "stamp out frontier cells, erase its own loops, then commit the final path on "
               "hitting the carved set.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return finished_; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    using Node = std::pair<uint32_t, uint32_t>;

    bool startNextWalk(MapData& map);
    void clearWalkVisualization(MapData& map);
    void commitWalk(MapData& map);

    ClassicMazeSettings settings_;
    MazeGrid grid_;
    std::vector<uint8_t> carved_;        // in the spanning tree?
    std::vector<int>     walkIndex_;     // index in current walk, -1 if absent

    std::vector<Node> walk_;             // current walker path (loop-erased on revisit)
    Node              walker_   { 0, 0 };
    bool              hasWalk_  = false;
    uint32_t          remaining_ = 0;

    std::mt19937 rng_;
    bool        finished_   = false;
    uint64_t    stepsTaken_ = 0;
    std::string status_     = "Ready";
};

void registerWilsonMazeGenerator(AlgorithmRegistry& registry);

} // namespace mgv
