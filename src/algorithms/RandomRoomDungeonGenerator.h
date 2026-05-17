#pragma once

#include "algorithms/IMapGenerator.h"
#include "map/Cell.h"

#include <random>
#include <string>
#include <utility>
#include <vector>

namespace mgv {

class AlgorithmRegistry;

class RandomRoomDungeonGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "random_room_dungeon"; }
    std::string_view name() const override { return "Random Room Dungeon"; }
    std::string_view description() const override {
        return "Randomly places rooms, rejects overlaps, then carves connecting corridors cell by cell.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return finished_; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    struct Room {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t w = 1;
        uint32_t h = 1;

        uint32_t centerX() const { return x + w / 2; }
        uint32_t centerY() const { return y + h / 2; }
    };

    using Point = std::pair<uint32_t, uint32_t>;

    enum class Phase {
        PlaceRooms,
        PrepareCorridors,
        CarveCorridors,
        Done
    };

    size_t index(uint32_t x, uint32_t y) const {
        return static_cast<size_t>(y) * width_ + x;
    }

    Room randomRoom();
    bool canPlace(const Room& room) const;
    void carveRoom(const Room& room);
    void carveCorridorCell(uint32_t x, uint32_t y);
    void prepareCorridors();
    void appendCorridor(Point a, Point b);
    void appendLine(Point a, Point b);
    void forceFallbackRoom();
    void applyVisualization(MapData& map) const;

    uint32_t width_ = 1;
    uint32_t depth_ = 1;
    uint32_t targetRooms_ = 8;
    uint32_t maxAttempts_ = 80;
    uint32_t minRoomWidth_ = 4;
    uint32_t maxRoomWidth_ = 14;
    uint32_t minRoomHeight_ = 4;
    uint32_t maxRoomHeight_ = 12;
    uint32_t roomPadding_ = 1;
    uint32_t corridorWidth_ = 1;
    uint32_t connectorMode_ = 0;
    uint32_t attempts_ = 0;
    uint64_t stepsTaken_ = 0;
    bool finished_ = false;
    bool randomBendOrder_ = true;
    bool allowRoomOverlap_ = false;
    bool showRejectedRooms_ = true;
    bool showAcceptedPreview_ = true;
    float roomHeight_ = 0.10f;
    float corridorHeight_ = 0.12f;
    float wallHeight_ = 1.0f;
    float candidateHeight_ = 0.24f;
    float currentHeight_ = 0.32f;

    Phase phase_ = Phase::PlaceRooms;
    std::mt19937 rng_ { 1337 };
    std::vector<CellType> layout_;
    std::vector<Room> rooms_;
    std::vector<Point> corridorCells_;
    size_t corridorCursor_ = 0;

    bool hasCandidate_ = false;
    bool candidateAccepted_ = false;
    Room candidate_;

    bool hasCurrentCell_ = false;
    Point currentCell_ { 0, 0 };

    std::string status_ = "Ready";
};

void registerRandomRoomDungeonGenerator(AlgorithmRegistry& registry);

} // namespace mgv
