#include "algorithms/BSPDungeonGenerator.h"

#include "algorithms/AlgorithmRegistry.h"
#include "map/MapData.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

namespace mgv {

namespace {
constexpr uint32_t kPadding = 1;   // border kept around leaf when placing rooms
}

void BSPDungeonGenerator::reset(MapData& map, const GeneratorConfig& config) {
    settings_ = config.bspDungeon;
    width_    = std::max<uint32_t>(8, config.width);
    depth_    = std::max<uint32_t>(8, config.depth);
    rng_.seed(config.seed);

    phase_      = Phase::Splitting;
    stepsTaken_ = 0;
    status_     = "Ready";

    leaves_.clear();
    splitQueue_.clear();
    roomQueue_.clear();
    pendingCorridors_.clear();
    corridorPath_.clear();
    corridorCursor_ = 0;

    // Initialise map with wall fill so partitions and rooms carve into the wall.
    map.resize(width_, depth_, 1);
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            Cell& c = map.at(x, y);
            c.type     = CellType::Wall;
            c.height   = settings_.wallHeight;
            c.color    = { 0, 0, 0, 0 };
            c.flags    = 0;
            c.metadata = 0;
        }
    }

    // Root leaf covers the whole map.
    Leaf root{};
    root.x = 0;
    root.y = 0;
    root.w = width_;
    root.h = depth_;
    leaves_.push_back(root);
    splitQueue_.push_back(0);
}

void BSPDungeonGenerator::writeCell(MapData& map, uint32_t x, uint32_t y,
                                    CellType type, float height) {
    if (x >= width_ || y >= depth_) return;
    Cell& c = map.at(x, y);
    c.type     = type;
    c.height   = height;
    c.color    = { 0, 0, 0, 0 };
    c.flags    = 0;
    c.metadata = static_cast<uint32_t>(stepsTaken_);
}

void BSPDungeonGenerator::carveRoom(MapData& map, const Leaf& leaf) {
    for (uint32_t y = leaf.ry; y < leaf.ry + leaf.rh; ++y) {
        for (uint32_t x = leaf.rx; x < leaf.rx + leaf.rw; ++x) {
            writeCell(map, x, y, CellType::Room, settings_.roomHeight);
        }
    }
}

void BSPDungeonGenerator::carveCorridorCell(MapData& map, uint32_t x, uint32_t y) {
    const int32_t r = static_cast<int32_t>(std::max<uint32_t>(1, settings_.corridorWidth)) / 2;
    for (int32_t oy = -r; oy <= r; ++oy) {
        for (int32_t ox = -r; ox <= r; ++ox) {
            const int32_t cx = static_cast<int32_t>(x) + ox;
            const int32_t cy = static_cast<int32_t>(y) + oy;
            if (cx <= 0 || cy <= 0 ||
                cx >= static_cast<int32_t>(width_) - 1 ||
                cy >= static_cast<int32_t>(depth_) - 1) continue;
            Cell& c = map.at(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy));
            if (c.type == CellType::Wall) {
                writeCell(map, static_cast<uint32_t>(cx), static_cast<uint32_t>(cy),
                          CellType::Corridor, settings_.corridorHeight);
            }
        }
    }
    // Highlight the current cursor cell so the user can follow the path.
    writeCell(map, x, y, CellType::Current, settings_.corridorHeight + 0.10f);
}

BSPDungeonGenerator::Point BSPDungeonGenerator::roomCenter(const Leaf& leaf) const {
    return { leaf.rx + leaf.rw / 2, leaf.ry + leaf.rh / 2 };
}

int BSPDungeonGenerator::representativeRoomLeaf(int leafIdx) const {
    // Walk down to a leaf node that has a placed room.
    while (leafIdx >= 0) {
        const Leaf& l = leaves_[static_cast<size_t>(leafIdx)];
        if (l.isLeaf) return leafIdx;
        // Prefer left subtree.
        if (l.leftIdx  >= 0) { leafIdx = l.leftIdx;  continue; }
        if (l.rightIdx >= 0) { leafIdx = l.rightIdx; continue; }
        break;
    }
    return leafIdx;
}

void BSPDungeonGenerator::splitOnce(MapData& map) {
    const int idx = splitQueue_.front();
    splitQueue_.erase(splitQueue_.begin());
    Leaf node = leaves_[static_cast<size_t>(idx)];

    const uint32_t minLeaf = std::max<uint32_t>(4, settings_.minLeafSize);
    const bool tooSmall = (node.w < minLeaf * 2 && node.h < minLeaf * 2) ||
                         (node.depth >= settings_.maxDepth);
    if (tooSmall) {
        // Mark as final leaf, schedule for room placement.
        leaves_[static_cast<size_t>(idx)].isLeaf = true;
        roomQueue_.push_back(idx);
        status_ = "Splitting: leaf reached at depth " + std::to_string(node.depth);
        return;
    }

    // Choose split direction by aspect, with jitter.
    std::uniform_real_distribution<float> jitter(-settings_.splitJitter, settings_.splitJitter);
    bool splitHorizontal;
    if (node.w > node.h * 1.25f) {
        splitHorizontal = false;
    } else if (node.h > node.w * 1.25f) {
        splitHorizontal = true;
    } else {
        splitHorizontal = (rng_() & 1u) != 0;
    }

    // Hard fallbacks if one axis is too tight.
    if (splitHorizontal  && node.h < minLeaf * 2) splitHorizontal = false;
    if (!splitHorizontal && node.w < minLeaf * 2) splitHorizontal = true;

    const float bias = 0.5f + jitter(rng_);
    Leaf left{}, right{};
    if (splitHorizontal) {
        const uint32_t lo  = minLeaf;
        const uint32_t hi  = node.h - minLeaf;
        const uint32_t cut = std::clamp<uint32_t>(static_cast<uint32_t>(node.h * bias), lo, hi);
        left  = { node.x, node.y,            node.w, cut,                   -1, -1, idx, true, node.depth + 1, false, 0, 0, 0, 0 };
        right = { node.x, node.y + cut,      node.w, node.h - cut,          -1, -1, idx, true, node.depth + 1, false, 0, 0, 0, 0 };
        // Paint the split line so the user can see the partition decision.
        for (uint32_t x = node.x; x < node.x + node.w; ++x) {
            writeCell(map, x, node.y + cut - 1, CellType::Frontier, settings_.wallHeight * 0.4f);
        }
    } else {
        const uint32_t lo  = minLeaf;
        const uint32_t hi  = node.w - minLeaf;
        const uint32_t cut = std::clamp<uint32_t>(static_cast<uint32_t>(node.w * bias), lo, hi);
        left  = { node.x,           node.y, cut,                   node.h, -1, -1, idx, true, node.depth + 1, false, 0, 0, 0, 0 };
        right = { node.x + cut,     node.y, node.w - cut,          node.h, -1, -1, idx, true, node.depth + 1, false, 0, 0, 0, 0 };
        for (uint32_t y = node.y; y < node.y + node.h; ++y) {
            writeCell(map, node.x + cut - 1, y, CellType::Frontier, settings_.wallHeight * 0.4f);
        }
    }

    const int leftIdx  = static_cast<int>(leaves_.size()); leaves_.push_back(left);
    const int rightIdx = static_cast<int>(leaves_.size()); leaves_.push_back(right);
    leaves_[static_cast<size_t>(idx)].leftIdx  = leftIdx;
    leaves_[static_cast<size_t>(idx)].rightIdx = rightIdx;
    leaves_[static_cast<size_t>(idx)].isLeaf   = false;
    splitQueue_.push_back(leftIdx);
    splitQueue_.push_back(rightIdx);
    pendingCorridors_.emplace_back(leftIdx, rightIdx);

    status_ = "Splitting: depth " + std::to_string(node.depth) +
              (splitHorizontal ? " H" : " V");
}

void BSPDungeonGenerator::placeRoomInLeaf(MapData& map, Leaf& leaf) {
    const uint32_t minRoom = std::max<uint32_t>(2, settings_.minRoomSize);
    const uint32_t pad     = std::max<uint32_t>(kPadding, settings_.roomPadding);

    if (leaf.w < minRoom + pad * 2 || leaf.h < minRoom + pad * 2) {
        // Leaf too thin — skip a room here.
        leaf.hasRoom = false;
        status_ = "Placing: skipped a thin leaf";
        return;
    }

    const uint32_t maxRw = leaf.w - pad * 2;
    const uint32_t maxRh = leaf.h - pad * 2;
    std::uniform_int_distribution<uint32_t> distW(minRoom, maxRw);
    std::uniform_int_distribution<uint32_t> distH(minRoom, maxRh);
    leaf.rw = distW(rng_);
    leaf.rh = distH(rng_);
    std::uniform_int_distribution<uint32_t> distX(leaf.x + pad, leaf.x + leaf.w - pad - leaf.rw);
    std::uniform_int_distribution<uint32_t> distY(leaf.y + pad, leaf.y + leaf.h - pad - leaf.rh);
    leaf.rx = distX(rng_);
    leaf.ry = distY(rng_);
    leaf.hasRoom = true;

    carveRoom(map, leaf);
    status_ = "Placed room " + std::to_string(leaf.rw) + "x" + std::to_string(leaf.rh);
}

void BSPDungeonGenerator::buildCorridorPath(const Leaf& a, const Leaf& b) {
    corridorPath_.clear();
    corridorCursor_ = 0;
    if (!a.hasRoom || !b.hasRoom) return;

    auto [ax, ay] = roomCenter(a);
    auto [bx, by] = roomCenter(b);

    // Random L-shape: choose horizontal-first vs vertical-first.
    const bool horizFirst = (rng_() & 1u) != 0;
    if (horizFirst) {
        for (uint32_t x = std::min(ax, bx); x <= std::max(ax, bx); ++x)
            corridorPath_.emplace_back(x, ay);
        for (uint32_t y = std::min(ay, by); y <= std::max(ay, by); ++y)
            corridorPath_.emplace_back(bx, y);
    } else {
        for (uint32_t y = std::min(ay, by); y <= std::max(ay, by); ++y)
            corridorPath_.emplace_back(ax, y);
        for (uint32_t x = std::min(ax, bx); x <= std::max(ax, bx); ++x)
            corridorPath_.emplace_back(x, by);
    }
}

GeneratorStep BSPDungeonGenerator::step(MapData& map) {
    ++stepsTaken_;

    if (phase_ == Phase::Splitting) {
        if (!splitQueue_.empty()) {
            splitOnce(map);
            return { true, false, status_ };
        }
        phase_ = Phase::PlacingRooms;
        status_ = "Splitting complete (" + std::to_string(leaves_.size()) + " leaves)";
        return { false, false, status_ };
    }

    if (phase_ == Phase::PlacingRooms) {
        if (!roomQueue_.empty()) {
            const int idx = roomQueue_.front();
            roomQueue_.erase(roomQueue_.begin());
            placeRoomInLeaf(map, leaves_[static_cast<size_t>(idx)]);
            return { true, false, status_ };
        }
        phase_ = Phase::ConnectingCorridors;
        status_ = "All rooms placed; connecting corridors";
        return { false, false, status_ };
    }

    if (phase_ == Phase::ConnectingCorridors) {
        // Start next corridor if current path is exhausted.
        if (corridorCursor_ >= corridorPath_.size()) {
            if (pendingCorridors_.empty()) {
                phase_  = Phase::Done;
                status_ = "Done";
                return { false, true, status_ };
            }
            auto pair = pendingCorridors_.back();
            pendingCorridors_.pop_back();

            const int aIdx = representativeRoomLeaf(pair.first);
            const int bIdx = representativeRoomLeaf(pair.second);
            const Leaf& a = leaves_[static_cast<size_t>(aIdx)];
            const Leaf& b = leaves_[static_cast<size_t>(bIdx)];
            buildCorridorPath(a, b);
            status_ = "Connecting rooms (" + std::to_string(pendingCorridors_.size()) + " links left)";
        }

        const uint32_t budget = std::max<uint32_t>(1, settings_.cellsPerStep);
        bool changed = false;
        for (uint32_t i = 0; i < budget && corridorCursor_ < corridorPath_.size(); ++i, ++corridorCursor_) {
            auto [x, y] = corridorPath_[corridorCursor_];
            carveCorridorCell(map, x, y);
            changed = true;
        }
        return { changed, false, status_ };
    }

    return { false, true, "Done" };
}

void registerBSPDungeonGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "bsp_dungeon",
        .name        = "BSP Dungeon",
        .description = "Binary Space Partitioning dungeon. Splits the map recursively (partition "
                       "lines visible), drops a room into every leaf, then connects sibling rooms "
                       "with L-shaped corridors — one decision per step.",
        .category    = "Dungeon",
        .family      = "Space Partition",
        .useCase     = "Structured dungeons with clear room hierarchy; great for visualising "
                       "divide-and-conquer level design.",
        .priority    = 4,
        .create      = [] { return std::make_unique<BSPDungeonGenerator>(); }
    });
}

} // namespace mgv
