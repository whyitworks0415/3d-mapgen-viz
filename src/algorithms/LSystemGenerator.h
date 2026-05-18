#pragma once

#include "algorithms/IMapGenerator.h"
#include "map/Cell.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

// True step-by-step L-system generator with a turtle renderer on the cell
// grid. Each phase advances one chunk:
//   Rewrite — one rewrite iteration per public step (Iterations total)
//   Render  — `cellsPerStep` symbols of the final string per step; turtle
//             draws floor cells; `[` pushes / `]` pops branch state; +/-
//             rotate by 90 degrees.
//
// Built-in presets (axis-aligned, 90-degree turns so the curve lands on grid
// cells):
//   0 Dragon Curve         axiom FX        rules X→X+YF+, Y→-FX-Y
//   1 Hilbert Curve        axiom L         rules L→+RF-LFL-FR+, R→-LF+RFR+FL-
//   2 Koch Square          axiom F         rule  F→F+F-F-F+F
//   3 Plant Branching      axiom X         rules X→F[+X]F[-X]+X, F→FF
class LSystemGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "lsystem"; }
    std::string_view name() const override { return "L-System Turtle"; }
    std::string_view description() const override {
        return "Lindenmayer system on a square grid. Each step applies one rewrite iteration or "
               "draws cellsPerStep symbols via a turtle. Symbols: F/G draw forward, +/- rotate "
               "90 degrees, [ ] push/pop branch state.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return phase_ == Phase::Done; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    enum class Phase { Rewrite, Render, Done };
    struct TurtleState { int32_t x, y; int dir; };

    void initFromPreset();
    void applyOneRewrite();
    void renderOneSymbol(MapData& map, char sym);
    void paintCell(MapData& map, int32_t x, int32_t y, CellType type, float height);
    void clearCurrentMarker(MapData& map);

    LSystemSettings settings_;
    uint32_t width_ = 1;
    uint32_t depth_ = 1;

    Phase    phase_      = Phase::Rewrite;
    uint64_t stepsTaken_ = 0;
    std::string status_  = "Ready";

    std::string current_;
    uint32_t    iteration_ = 0;
    size_t      renderCursor_ = 0;

    std::string axiom_;
    std::unordered_map<char, std::string> rules_;
    bool drawsF_ = true;
    bool drawsG_ = false;

    int32_t turtleX_   = 0;
    int32_t turtleY_   = 0;
    int     turtleDir_ = 0;   // 0=+X, 1=+Y, 2=-X, 3=-Y
    std::vector<TurtleState> stack_;

    bool    hasMarker_ = false;
    int32_t markerX_   = 0;
    int32_t markerY_   = 0;
};

void registerLSystemGenerator(AlgorithmRegistry& registry);

} // namespace mgv
