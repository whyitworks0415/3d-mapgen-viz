#pragma once

#include "algorithms/IMapGenerator.h"

#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

// True step-by-step Bridson Poisson-disk sampling.
//   - Each step pops one sample from the active list, throws up to k
//     candidate points in the annulus [r, 2r], and adds the first valid one.
//   - If no candidate is valid the active sample is removed.
//   - Samples are visualised as raised brush-sized dots; the most recent
//     sample is highlighted with the Current colour.
class PoissonDiskSamplingGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "poisson_disk_sampling"; }
    std::string_view name() const override { return "Poisson Disk Sampling"; }
    std::string_view description() const override {
        return "Bridson's algorithm. Each step picks one active sample, tries up to k candidates "
               "in an annulus around it, and either commits the first valid one or removes the "
               "active sample.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return phase_ == Phase::Done; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    enum class Phase { Seed, Expand, Done };

    using Point = std::pair<float, float>;

    void paintGround(MapData& map);
    void paintSample(MapData& map, float fx, float fy, bool highlight);
    void clearHighlight(MapData& map, float fx, float fy);
    bool inBounds(float fx, float fy) const;
    bool tooClose(float fx, float fy) const;
    int  gridIndex(float fx, float fy) const;

    PoissonSettings settings_;
    uint32_t width_ = 1;
    uint32_t depth_ = 1;
    float    minDist_ = 4.0f;
    float    cellSize_ = 2.0f;
    uint32_t gridW_ = 1;
    uint32_t gridH_ = 1;

    Phase    phase_      = Phase::Seed;
    uint64_t stepsTaken_ = 0;
    std::string status_  = "Ready";

    std::vector<int>   grid_;             // sample index per grid cell, -1 empty
    std::vector<Point> samples_;
    std::vector<size_t> active_;          // indices into samples_

    bool     hasLastHighlight_ = false;
    float    lastHighlightX_ = 0.0f;
    float    lastHighlightY_ = 0.0f;

    std::mt19937 rng_;
};

void registerPoissonDiskSamplingGenerator(AlgorithmRegistry& registry);

} // namespace mgv
