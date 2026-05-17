#pragma once

#include "algorithms/IMapGenerator.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

// True step-by-step Fault Formation terrain. Each step lays down one fault
// line (a random line through the map) and uplifts one side by the current
// displacement; an optional smoothing pass evens the result before painting.
class FaultFormationTerrainGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "fault_formation_terrain"; }
    std::string_view name() const override { return "Fault Formation Terrain"; }
    std::string_view description() const override {
        return "Repeats: drop a random line across the map, uplift one side. Each step shows one "
               "fault being applied; the running min/max is used to colour and scale the heightmap.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return phase_ == Phase::Done; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    enum class Phase { ApplyFault, Smooth, Finalise, Done };

    void paintAll(MapData& map);
    void paintCell(MapData& map, uint32_t x, uint32_t y, float t);
    void smoothPass();

    FaultFormationSettings settings_;
    uint32_t width_ = 1;
    uint32_t depth_ = 1;

    Phase    phase_      = Phase::ApplyFault;
    uint32_t iteration_  = 0;
    uint64_t stepsTaken_ = 0;
    std::string status_  = "Ready";

    std::vector<float> height_;
    float    minH_ = 1e9f;
    float    maxH_ = -1e9f;
    std::mt19937 rng_;
};

void registerFaultFormationTerrainGenerator(AlgorithmRegistry& registry);

} // namespace mgv
