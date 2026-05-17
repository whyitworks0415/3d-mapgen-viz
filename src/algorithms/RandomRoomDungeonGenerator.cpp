#include "algorithms/RandomRoomDungeonGenerator.h"

#include "algorithms/AlgorithmRegistry.h"
#include "map/MapData.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace mgv {

void RandomRoomDungeonGenerator::reset(MapData& map, const GeneratorConfig& config) {
    width_ = std::max<uint32_t>(12, config.width);
    depth_ = std::max<uint32_t>(12, config.depth);
    targetRooms_ = std::clamp<uint32_t>(config.roomDungeon.targetRooms, 1, 128);
    maxAttempts_ = std::clamp<uint32_t>(config.roomDungeon.maxAttempts, targetRooms_, 4096);
    minRoomWidth_ = std::clamp<uint32_t>(config.roomDungeon.minRoomWidth, 2, width_ - 3);
    maxRoomWidth_ = std::clamp<uint32_t>(config.roomDungeon.maxRoomWidth, minRoomWidth_, width_ - 3);
    minRoomHeight_ = std::clamp<uint32_t>(config.roomDungeon.minRoomHeight, 2, depth_ - 3);
    maxRoomHeight_ = std::clamp<uint32_t>(config.roomDungeon.maxRoomHeight, minRoomHeight_, depth_ - 3);
    roomPadding_ = std::clamp<uint32_t>(config.roomDungeon.roomPadding, 0, 12);
    corridorWidth_ = std::clamp<uint32_t>(config.roomDungeon.corridorWidth, 1, 12);
    connectorMode_ = std::min<uint32_t>(config.roomDungeon.connectorMode, 1);
    randomBendOrder_ = config.roomDungeon.randomBendOrder;
    allowRoomOverlap_ = config.roomDungeon.allowRoomOverlap;
    showRejectedRooms_ = config.roomDungeon.showRejectedRooms;
    showAcceptedPreview_ = config.roomDungeon.showAcceptedPreview;
    roomHeight_ = std::max(0.01f, config.roomDungeon.roomHeight);
    corridorHeight_ = std::max(0.01f, config.roomDungeon.corridorHeight);
    wallHeight_ = std::max(0.01f, config.roomDungeon.wallHeight);
    candidateHeight_ = std::max(0.01f, config.roomDungeon.candidateHeight);
    currentHeight_ = std::max(0.01f, config.roomDungeon.currentHeight);
    attempts_ = 0;
    stepsTaken_ = 0;
    finished_ = false;
    phase_ = Phase::PlaceRooms;
    status_ = "Placing rooms";

    rng_.seed(config.seed);
    layout_.assign(static_cast<size_t>(width_) * depth_, CellType::Wall);
    rooms_.clear();
    corridorCells_.clear();
    corridorCursor_ = 0;
    hasCandidate_ = false;
    candidateAccepted_ = false;
    hasCurrentCell_ = false;

    map.resize(width_, depth_, 1);
    applyVisualization(map);
}

GeneratorStep RandomRoomDungeonGenerator::step(MapData& map) {
    if (finished_) {
        return { false, true, status_ };
    }

    if (phase_ == Phase::PlaceRooms) {
        hasCurrentCell_ = false;

        if (attempts_ >= maxAttempts_ || rooms_.size() >= targetRooms_) {
            phase_ = Phase::PrepareCorridors;
            status_ = "Preparing corridors";
            hasCandidate_ = false;
            applyVisualization(map);
            return { true, false, status_ };
        }

        ++attempts_;
        candidate_ = randomRoom();
        hasCandidate_ = true;
        candidateAccepted_ = canPlace(candidate_);

        if (candidateAccepted_) {
            carveRoom(candidate_);
            rooms_.push_back(candidate_);
            status_ = "Room accepted (" + std::to_string(rooms_.size()) + "/" +
                      std::to_string(targetRooms_) + ")";
        } else {
            status_ = "Room rejected";
        }

        ++stepsTaken_;
        applyVisualization(map);
        return { true, false, status_ };
    }

    if (phase_ == Phase::PrepareCorridors) {
        hasCandidate_ = false;
        hasCurrentCell_ = false;

        if (rooms_.empty()) {
            forceFallbackRoom();
        }

        prepareCorridors();
        phase_ = corridorCells_.empty() ? Phase::Done : Phase::CarveCorridors;
        if (phase_ == Phase::Done) {
            finished_ = true;
            status_ = "Completed";
        } else {
            status_ = "Connecting rooms";
        }

        applyVisualization(map);
        return { true, finished_, status_ };
    }

    if (phase_ == Phase::CarveCorridors) {
        hasCandidate_ = false;

        if (corridorCursor_ >= corridorCells_.size()) {
            phase_ = Phase::Done;
            finished_ = true;
            status_ = "Completed";
            hasCurrentCell_ = false;
            applyVisualization(map);
            return { true, true, status_ };
        }

        currentCell_ = corridorCells_[corridorCursor_++];
        hasCurrentCell_ = true;
        carveCorridorCell(currentCell_.first, currentCell_.second);

        ++stepsTaken_;
        if (corridorCursor_ >= corridorCells_.size()) {
            phase_ = Phase::Done;
            finished_ = true;
            status_ = "Completed";
            hasCurrentCell_ = false;
        } else {
            status_ = "Carving corridors";
        }

        applyVisualization(map);
        return { true, finished_, status_ };
    }

    finished_ = true;
    status_ = "Completed";
    applyVisualization(map);
    return { true, true, status_ };
}

RandomRoomDungeonGenerator::Room RandomRoomDungeonGenerator::randomRoom() {
    std::uniform_int_distribution<uint32_t> pickW(minRoomWidth_, maxRoomWidth_);
    std::uniform_int_distribution<uint32_t> pickH(minRoomHeight_, maxRoomHeight_);

    Room room;
    room.w = pickW(rng_);
    room.h = pickH(rng_);

    std::uniform_int_distribution<uint32_t> pickX(1, width_ - room.w - 2);
    std::uniform_int_distribution<uint32_t> pickY(1, depth_ - room.h - 2);
    room.x = pickX(rng_);
    room.y = pickY(rng_);
    return room;
}

bool RandomRoomDungeonGenerator::canPlace(const Room& room) const {
    if (allowRoomOverlap_) return true;

    const uint32_t minX = room.x > roomPadding_ ? room.x - roomPadding_ : 0;
    const uint32_t minY = room.y > roomPadding_ ? room.y - roomPadding_ : 0;
    const uint32_t maxX = std::min(width_ - 1, room.x + room.w - 1 + roomPadding_);
    const uint32_t maxY = std::min(depth_ - 1, room.y + room.h - 1 + roomPadding_);

    for (uint32_t y = minY; y <= maxY; ++y) {
        for (uint32_t x = minX; x <= maxX; ++x) {
            if (layout_[index(x, y)] != CellType::Wall) return false;
        }
    }
    return true;
}

void RandomRoomDungeonGenerator::carveRoom(const Room& room) {
    for (uint32_t y = room.y; y < room.y + room.h; ++y) {
        for (uint32_t x = room.x; x < room.x + room.w; ++x) {
            layout_[index(x, y)] = CellType::Room;
        }
    }
}

void RandomRoomDungeonGenerator::carveCorridorCell(uint32_t x, uint32_t y) {
    const int32_t halfLo = static_cast<int32_t>((corridorWidth_ - 1) / 2);
    const int32_t halfHi = static_cast<int32_t>(corridorWidth_ / 2);
    for (int32_t oy = -halfLo; oy <= halfHi; ++oy) {
        for (int32_t ox = -halfLo; ox <= halfHi; ++ox) {
            const int32_t cx = static_cast<int32_t>(x) + ox;
            const int32_t cy = static_cast<int32_t>(y) + oy;
            if (cx <= 0 || cy <= 0 ||
                cx >= static_cast<int32_t>(width_ - 1) ||
                cy >= static_cast<int32_t>(depth_ - 1)) {
                continue;
            }

            CellType& cell = layout_[index(static_cast<uint32_t>(cx),
                                           static_cast<uint32_t>(cy))];
            if (cell == CellType::Wall) {
                cell = CellType::Corridor;
            }
        }
    }
}

void RandomRoomDungeonGenerator::prepareCorridors() {
    corridorCells_.clear();
    corridorCursor_ = 0;

    if (connectorMode_ == 0) {
        std::sort(rooms_.begin(), rooms_.end(), [](const Room& a, const Room& b) {
            if (a.centerX() == b.centerX()) return a.centerY() < b.centerY();
            return a.centerX() < b.centerX();
        });

        for (size_t i = 1; i < rooms_.size(); ++i) {
            appendCorridor({ rooms_[i - 1].centerX(), rooms_[i - 1].centerY() },
                           { rooms_[i].centerX(),     rooms_[i].centerY()     });
        }
        return;
    }

    std::vector<size_t> connected;
    std::vector<size_t> pending;
    connected.push_back(0);
    for (size_t i = 1; i < rooms_.size(); ++i) pending.push_back(i);

    while (!pending.empty()) {
        size_t bestConnected = 0;
        size_t bestPendingSlot = 0;
        uint32_t bestDistance = UINT32_MAX;

        for (size_t ci : connected) {
            for (size_t pi = 0; pi < pending.size(); ++pi) {
                const Room& a = rooms_[ci];
                const Room& b = rooms_[pending[pi]];
                const uint32_t dx = a.centerX() > b.centerX()
                    ? a.centerX() - b.centerX() : b.centerX() - a.centerX();
                const uint32_t dy = a.centerY() > b.centerY()
                    ? a.centerY() - b.centerY() : b.centerY() - a.centerY();
                const uint32_t distance = dx + dy;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestConnected = ci;
                    bestPendingSlot = pi;
                }
            }
        }

        const size_t next = pending[bestPendingSlot];
        appendCorridor({ rooms_[bestConnected].centerX(), rooms_[bestConnected].centerY() },
                       { rooms_[next].centerX(),          rooms_[next].centerY()          });
        connected.push_back(next);
        pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(bestPendingSlot));
    }
}

void RandomRoomDungeonGenerator::appendCorridor(Point a, Point b) {
    int bendMode = 0;
    if (randomBendOrder_) {
        std::uniform_int_distribution<int> coin(0, 1);
        bendMode = coin(rng_);
    }
    const Point bend = bendMode == 0 ? Point { b.first, a.second }
                                    : Point { a.first, b.second };
    appendLine(a, bend);
    appendLine(bend, b);
}

void RandomRoomDungeonGenerator::appendLine(Point a, Point b) {
    int32_t x = static_cast<int32_t>(a.first);
    int32_t y = static_cast<int32_t>(a.second);
    const int32_t endX = static_cast<int32_t>(b.first);
    const int32_t endY = static_cast<int32_t>(b.second);
    const int32_t stepX = (endX > x) ? 1 : (endX < x ? -1 : 0);
    const int32_t stepY = (endY > y) ? 1 : (endY < y ? -1 : 0);

    while (x != endX || y != endY) {
        if (x != endX) x += stepX;
        if (y != endY) y += stepY;

        const uint32_t ux = static_cast<uint32_t>(x);
        const uint32_t uy = static_cast<uint32_t>(y);
        if (ux < width_ && uy < depth_) {
            corridorCells_.push_back({ ux, uy });
        }
    }
}

void RandomRoomDungeonGenerator::forceFallbackRoom() {
    Room room;
    room.w = std::min<uint32_t>(8, width_ - 2);
    room.h = std::min<uint32_t>(6, depth_ - 2);
    room.x = (width_ - room.w) / 2;
    room.y = (depth_ - room.h) / 2;
    carveRoom(room);
    rooms_.push_back(room);
}

void RandomRoomDungeonGenerator::applyVisualization(MapData& map) const {
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            Cell& cell = map.at(x, y);
            cell.color = { 0, 0, 0, 0 };
            cell.flags = 0;
            cell.metadata = 0;

            const CellType type = layout_[index(x, y)];
            cell.type = type;
            switch (type) {
                case CellType::Room:
                    cell.height = roomHeight_;
                    break;
                case CellType::Corridor:
                    cell.height = corridorHeight_;
                    break;
                case CellType::Wall:
                default:
                    cell.height = wallHeight_;
                    break;
            }
        }
    }

    if (hasCandidate_ &&
        ((candidateAccepted_ && showAcceptedPreview_) ||
         (!candidateAccepted_ && showRejectedRooms_))) {
        const CellType overlay = candidateAccepted_ ? CellType::Frontier : CellType::Current;
        for (uint32_t y = candidate_.y; y < candidate_.y + candidate_.h; ++y) {
            for (uint32_t x = candidate_.x; x < candidate_.x + candidate_.w; ++x) {
                Cell& cell = map.at(x, y);
                cell.type = overlay;
                cell.height = candidateHeight_;
            }
        }
    }

    if (hasCurrentCell_) {
        Cell& cell = map.at(currentCell_.first, currentCell_.second);
        cell.type = CellType::Current;
        cell.height = currentHeight_;
    }
}

void registerRandomRoomDungeonGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "random_room_dungeon",
        .name        = "Random Room Dungeon",
        .description = "Places random rooms with rejection visualization, then carves L-shaped "
                       "corridors that connect them cell by cell.",
        .category    = "Dungeon",
        .family      = "Room Placement",
        .useCase     = "Classic roguelike floors with discrete rooms and connecting halls.",
        .priority    = 2,
        .create      = [] { return std::make_unique<RandomRoomDungeonGenerator>(); }
    });
}

} // namespace mgv
