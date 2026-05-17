#pragma once

#include "algorithms/IMapGenerator.h"
#include "map/Cell.h"

#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

// True step-by-step BSP dungeon. Phases:
//   1. Splitting           — one partition per step, partition line painted as Frontier
//   2. PlacingRooms        — one leaf room per step
//   3. ConnectingCorridors — one corridor cell per step (cellsPerStep tunable)
class BSPDungeonGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "bsp_dungeon"; }
    std::string_view name() const override { return "BSP Dungeon"; }
    std::string_view description() const override {
        return "Binary space partitioning. Splits the map recursively, drops a room into each leaf, "
               "then links siblings up the tree with corridors — one operation per step.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return phase_ == Phase::Done; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    enum class Phase { Splitting, PlacingRooms, ConnectingCorridors, Done };

    struct Leaf {
        uint32_t x = 0, y = 0, w = 0, h = 0;     // node rectangle
        int      leftIdx   = -1;
        int      rightIdx  = -1;
        int      parentIdx = -1;
        bool     isLeaf    = true;
        uint32_t depth     = 0;
        // room within the leaf
        bool     hasRoom = false;
        uint32_t rx = 0, ry = 0, rw = 0, rh = 0;
    };

    using Point = std::pair<uint32_t, uint32_t>;

    void splitOnce(MapData& map);
    void placeRoomInLeaf(MapData& map, Leaf& leaf);
    void carveRoom(MapData& map, const Leaf& leaf);
    void carveCorridorCell(MapData& map, uint32_t x, uint32_t y);
    void buildCorridorPath(const Leaf& a, const Leaf& b);

    void writeCell(MapData& map, uint32_t x, uint32_t y, CellType type, float height);
    Point roomCenter(const Leaf& leaf) const;

    // Returns the leaf that contains the leftmost descendant room — used to pair siblings.
    int representativeRoomLeaf(int leafIdx) const;

    BSPDungeonSettings settings_;
    uint32_t width_  = 1;
    uint32_t depth_  = 1;
    std::mt19937 rng_;

    Phase    phase_      = Phase::Splitting;
    uint64_t stepsTaken_ = 0;
    std::string status_  = "Ready";

    // Tree storage.
    std::vector<Leaf> leaves_;
    std::vector<int>  splitQueue_;
    std::vector<int>  roomQueue_;
    std::vector<std::pair<int, int>> pendingCorridors_;

    // Current corridor being carved.
    std::vector<Point> corridorPath_;
    size_t             corridorCursor_ = 0;
};

void registerBSPDungeonGenerator(AlgorithmRegistry& registry);

} // namespace mgv
