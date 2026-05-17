#pragma once

#include "algorithms/IMapGenerator.h"
#include "algorithms/MazeGridUtils.h"

#include <random>
#include <string>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

// True step-by-step Hunt-and-Kill. Walk phase: random step into an
// uncarved neighbour until none exist. Hunt phase: scan the grid row by row
// for an uncarved cell adjacent to a carved one, then resume walking from
// there.
class HuntAndKillMazeGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "hunt_and_kill_maze"; }
    std::string_view name() const override { return "Hunt-and-Kill Algorithm"; }
    std::string_view description() const override {
        return "Random walk that carves into uncarved neighbours; when stuck, scans the grid for "
               "the next uncarved cell adjacent to the carved set and resumes from there.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return finished_; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    enum class Phase { Walking, Hunting, Done };

    int  pickUncarvedNeighbour(uint32_t col, uint32_t row);
    bool advanceHunt(MapData& map);

    ClassicMazeSettings settings_;
    MazeGrid grid_;
    std::vector<uint8_t> carved_;
    uint32_t walkerCol_ = 0;
    uint32_t walkerRow_ = 0;

    uint32_t huntRow_ = 0;
    uint32_t huntCol_ = 0;

    Phase phase_ = Phase::Walking;
    std::mt19937 rng_;

    bool        finished_   = false;
    uint64_t    stepsTaken_ = 0;
    std::string status_     = "Ready";
};

void registerHuntAndKillMazeGenerator(AlgorithmRegistry& registry);

} // namespace mgv
