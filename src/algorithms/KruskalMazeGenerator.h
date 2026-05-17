#pragma once

#include "algorithms/IMapGenerator.h"
#include "algorithms/MazeGridUtils.h"

#include <random>
#include <string>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

// True step-by-step Randomized Kruskal maze. The full edge list is shuffled
// once; each algorithmic step considers one edge using union-find. Sibling
// roots → merge & carve; same root → reject (or braid if loopChance > 0).
class KruskalMazeGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "randomized_kruskal_maze"; }
    std::string_view name() const override { return "Randomized Kruskal Maze"; }
    std::string_view description() const override {
        return "Shuffles every grid edge once, then walks the list with union-find. Each step "
               "merges two disjoint sets or rejects an edge that would form a loop.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return finished_; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    struct Edge { uint32_t ax, ay, bx, by; };

    int find(int x);
    void unite(int a, int b);

    ClassicMazeSettings settings_;
    MazeGrid grid_;
    std::vector<Edge>   edges_;
    std::vector<int>    parent_;
    std::vector<int>    rank_;
    size_t              cursor_ = 0;

    std::mt19937 rng_;
    bool        finished_   = false;
    uint64_t    stepsTaken_ = 0;
    std::string status_     = "Ready";
};

void registerKruskalMazeGenerator(AlgorithmRegistry& registry);

} // namespace mgv
