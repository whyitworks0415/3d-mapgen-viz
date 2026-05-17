#pragma once

#include "algorithms/IMapGenerator.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

// True step-by-step Voronoi diagram. Phases:
//   ScatterSeeds      — one seed per step, painted with a Current highlight
//   AssignCells       — cells per step are assigned to the nearest seed and
//                       coloured by that seed's id (each region gets its own hue).
//   LloydRelaxation   — (optional) per iteration, recompute each seed as the
//                       centroid of its current region and reassign cells.
//   Finalise          — optional terrain classification: smallest regions become
//                       water, largest become mountains.
class VoronoiDiagramGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "voronoi_diagram"; }
    std::string_view name() const override { return "Voronoi Diagram"; }
    std::string_view description() const override {
        return "Scatter seeds, paint cells by nearest seed, optionally apply Lloyd relaxation. "
               "Optional terrain mode marks smallest regions as water and largest as mountains.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return phase_ == Phase::Done; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    enum class Phase { ScatterSeeds, AssignCells, LloydRelaxation, Finalise, Done };

    struct Seed {
        float x = 0.0f;
        float y = 0.0f;
        glm::u8vec3 color { 200, 200, 200 };
    };

    void paintCell(MapData& map, uint32_t x, uint32_t y, int seedIdx, bool highlight);
    void recomputeAssignments(MapData& map);
    void runLloydPass();
    void finaliseTerrain(MapData& map);
    int  nearestSeed(uint32_t x, uint32_t y) const;
    glm::u8vec3 colorForSeed(uint32_t seedIdx) const;

    VoronoiSettings settings_;
    uint32_t width_ = 1;
    uint32_t depth_ = 1;

    Phase    phase_      = Phase::ScatterSeeds;
    uint64_t stepsTaken_ = 0;
    std::string status_  = "Ready";
    uint32_t cursor_     = 0;
    uint32_t lloydIter_  = 0;

    std::vector<Seed> seeds_;
    std::vector<int>  assignment_;        // seedIdx per cell
    std::mt19937 rng_;
};

void registerVoronoiDiagramGenerator(AlgorithmRegistry& registry);

} // namespace mgv
