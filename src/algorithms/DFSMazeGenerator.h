#pragma once

#include "algorithms/IMapGenerator.h"

#include <random>
#include <string>
#include <utility>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

class DFSMazeGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "dfs_maze"; }
    std::string_view name() const override { return "DFS Maze"; }
    std::string_view description() const override {
        return "Recursive backtracker maze generation with per-step carving and backtracking playback.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return finished_; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    using Point = std::pair<uint32_t, uint32_t>;

    size_t index(uint32_t x, uint32_t y) const {
        return static_cast<size_t>(y) * width_ + x;
    }

    bool isMazeCell(int32_t x, int32_t y) const;
    void carve(uint32_t x, uint32_t y);
    void carveLine(uint32_t ax, uint32_t ay, uint32_t bx, uint32_t by);
    void applyVisualization(MapData& map) const;
    std::vector<Point> unvisitedNeighbors(uint32_t x, uint32_t y);
    std::vector<Point> visitedNeighbors(uint32_t x, uint32_t y) const;

    uint32_t startCoordinate(uint32_t extent) const;
    uint32_t randomNodeCoordinate(uint32_t extent);

    uint32_t width_ = 1;
    uint32_t depth_ = 1;
    uint32_t corridorWidth_ = 1;
    uint32_t cellSpacing_ = 2;
    uint32_t startMode_ = 0;
    uint64_t stepsTaken_ = 0;
    bool finished_ = false;
    bool randomizeDirections_ = true;
    bool showBacktrackTrail_ = true;
    bool clearVisitOverlayOnFinish_ = true;
    float braidChance_ = 0.0f;
    float wallHeight_ = 1.0f;
    float floorHeight_ = 0.08f;
    float visitedHeight_ = 0.16f;
    float currentHeight_ = 0.30f;

    std::mt19937 rng_ { 1337 };
    std::vector<uint8_t> carved_;
    std::vector<uint8_t> visited_;
    std::vector<Point> stack_;
    std::string status_ = "Ready";
};

void registerDFSMazeGenerator(AlgorithmRegistry& registry);

} // namespace mgv
