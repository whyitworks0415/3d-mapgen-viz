#include "algorithms/WFCGenerator.h"

#include "algorithms/AlgorithmRegistry.h"
#include "map/MapData.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <queue>
#include <utility>

namespace mgv {

namespace {

constexpr uint8_t maskOf(uint8_t tile) { return static_cast<uint8_t>(1u << tile); }

uint8_t addAllowed(uint8_t mask, std::initializer_list<uint8_t> tiles) {
    for (uint8_t t : tiles) mask |= maskOf(t);
    return mask;
}

} // namespace

void WFCGenerator::buildAdjacency() {
    // Reset.
    for (uint8_t from = 0; from < TileCount; ++from) {
        for (int dir = 0; dir < 4; ++dir) adjacency_[from][dir] = 0;
    }

    // Symmetric rules in all 4 directions:
    //   Water    : Water, Floor
    //   Floor    : Floor, Water, Wall, Mountain
    //   Wall     : Wall,  Floor, Mountain
    //   Mountain : Mountain, Wall, Floor (and Water iff allowed)
    auto allow = [&](uint8_t a, uint8_t b) {
        for (int dir = 0; dir < 4; ++dir) {
            adjacency_[a][dir] = static_cast<uint8_t>(adjacency_[a][dir] | maskOf(b));
            adjacency_[b][dir] = static_cast<uint8_t>(adjacency_[b][dir] | maskOf(a));
        }
    };

    allow(TileWater,    TileWater);
    allow(TileFloor,    TileFloor);
    allow(TileWall,     TileWall);
    allow(TileMountain, TileMountain);

    allow(TileWater,    TileFloor);
    allow(TileFloor,    TileWall);
    allow(TileFloor,    TileMountain);
    allow(TileWall,     TileMountain);

    if (settings_.allowWaterNearMountain) {
        allow(TileWater, TileMountain);
    }
}

void WFCGenerator::reset(MapData& map, const GeneratorConfig& config) {
    settings_ = config.wfc;
    width_    = std::max<uint32_t>(4, config.width);
    depth_    = std::max<uint32_t>(4, config.depth);
    rng_.seed(config.seed);

    buildAdjacency();

    const size_t total = static_cast<size_t>(width_) * depth_;
    wave_.assign(total, kMaskAll);
    collapsed_.assign(total, 255u);

    remaining_ = static_cast<uint32_t>(total);
    phase_     = Phase::Collapsing;
    stepsTaken_ = 0;
    status_    = "Ready";
    hasCurrent_ = false;

    map.resize(width_, depth_, 1);
    // Start with all cells in "Frontier" state (yellow) to visualise unresolved positions.
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            Cell& c = map.at(x, y);
            c.type   = CellType::Frontier;
            c.height = 0.08f;
            c.color  = { 0, 0, 0, 0 };
        }
    }
}

int WFCGenerator::findMinEntropy(uint32_t& outX, uint32_t& outY) const {
    int bestPop = 256;
    int bestCount = 0;
    uint32_t pickedX = 0, pickedY = 0;
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            const size_t idx = static_cast<size_t>(y) * width_ + x;
            if (collapsed_[idx] != 255u) continue;
            const int pop = std::popcount(wave_[idx]);
            if (pop <= 1) {
                // Skip already collapsed (1) or contradicting (0) cells when
                // searching for entropy — they'll be handled in the main loop.
                continue;
            }
            if (pop < bestPop) {
                bestPop = pop;
                bestCount = 1;
                pickedX = x; pickedY = y;
            } else if (pop == bestPop) {
                ++bestCount;
                // Reservoir-style tiebreak so we don't always pick the top-left.
                std::uniform_int_distribution<int> sel(0, bestCount - 1);
                if (sel(const_cast<std::mt19937&>(rng_)) == 0) {
                    pickedX = x; pickedY = y;
                }
            }
        }
    }
    if (bestPop == 256) return -1;
    outX = pickedX;
    outY = pickedY;
    return bestPop;
}

WFCGenerator::Tile WFCGenerator::pickCollapse(uint32_t x, uint32_t y) {
    const size_t idx = static_cast<size_t>(y) * width_ + x;
    const uint8_t mask = wave_[idx];

    // Weighted pick from the still-allowed tiles.
    const uint32_t weights[TileCount] = {
        settings_.waterWeight, settings_.floorWeight,
        settings_.wallWeight,  settings_.mountainWeight
    };

    uint32_t total = 0;
    for (uint8_t t = 0; t < TileCount; ++t) {
        if (mask & maskOf(t)) {
            uint32_t w = std::max<uint32_t>(1, weights[t]);
            // Mild bias toward connected floors when enabled — bump weight if any
            // already-collapsed neighbour matches.
            if (settings_.preferConnectedFloors && t == TileFloor) {
                const int dx[4] = { 1, -1, 0, 0 };
                const int dy[4] = { 0, 0, 1, -1 };
                for (int d = 0; d < 4; ++d) {
                    const int nx = static_cast<int>(x) + dx[d];
                    const int ny = static_cast<int>(y) + dy[d];
                    if (nx < 0 || ny < 0 ||
                        nx >= static_cast<int>(width_) ||
                        ny >= static_cast<int>(depth_)) continue;
                    const size_t nidx = static_cast<size_t>(ny) * width_ + static_cast<size_t>(nx);
                    if (collapsed_[nidx] == TileFloor) w *= 2;
                }
            }
            total += w;
        }
    }

    std::uniform_int_distribution<uint32_t> pick(0, total > 0 ? total - 1 : 0);
    uint32_t roll = pick(rng_);
    for (uint8_t t = 0; t < TileCount; ++t) {
        if (mask & maskOf(t)) {
            uint32_t w = std::max<uint32_t>(1, weights[t]);
            if (settings_.preferConnectedFloors && t == TileFloor) {
                const int dx[4] = { 1, -1, 0, 0 };
                const int dy[4] = { 0, 0, 1, -1 };
                for (int d = 0; d < 4; ++d) {
                    const int nx = static_cast<int>(x) + dx[d];
                    const int ny = static_cast<int>(y) + dy[d];
                    if (nx < 0 || ny < 0 ||
                        nx >= static_cast<int>(width_) ||
                        ny >= static_cast<int>(depth_)) continue;
                    const size_t nidx = static_cast<size_t>(ny) * width_ + static_cast<size_t>(nx);
                    if (collapsed_[nidx] == TileFloor) w *= 2;
                }
            }
            if (roll < w) return static_cast<Tile>(t);
            roll -= w;
        }
    }
    // Fallback: lowest set bit.
    for (uint8_t t = 0; t < TileCount; ++t)
        if (mask & maskOf(t)) return static_cast<Tile>(t);
    return TileFloor;
}

bool WFCGenerator::propagate(uint32_t startX, uint32_t startY) {
    // For each neighbour direction, the allowed mask must satisfy:
    //   allowed_at_neighbour ⊆ ∪_{t ∈ allowed_here} adjacency_[t][dir_from_here]
    const int dx[4] = { 1, -1, 0, 0 };
    const int dy[4] = { 0, 0, 1, -1 };

    std::queue<std::pair<uint32_t, uint32_t>> queue;
    queue.emplace(startX, startY);

    while (!queue.empty()) {
        auto [x, y] = queue.front();
        queue.pop();
        const size_t idx = static_cast<size_t>(y) * width_ + x;
        const uint8_t mask = wave_[idx];
        if (mask == 0) return false;

        for (int dir = 0; dir < 4; ++dir) {
            const int nx = static_cast<int>(x) + dx[dir];
            const int ny = static_cast<int>(y) + dy[dir];
            if (nx < 0 || ny < 0 ||
                nx >= static_cast<int>(width_) ||
                ny >= static_cast<int>(depth_)) continue;

            uint8_t allowedFromHere = 0;
            for (uint8_t t = 0; t < TileCount; ++t) {
                if (mask & maskOf(t)) allowedFromHere |= adjacency_[t][dir];
            }

            const size_t nidx = static_cast<size_t>(ny) * width_ + static_cast<size_t>(nx);
            const uint8_t before = wave_[nidx];
            const uint8_t after  = static_cast<uint8_t>(before & allowedFromHere);
            if (after != before) {
                if (after == 0) return false;
                wave_[nidx] = after;
                queue.emplace(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny));
                // If a propagation forced a unique tile, finalise it.
                if (std::popcount(after) == 1 && collapsed_[nidx] == 255u) {
                    uint8_t tile = 0;
                    for (uint8_t t = 0; t < TileCount; ++t) {
                        if (after & maskOf(t)) { tile = t; break; }
                    }
                    collapsed_[nidx] = tile;
                    if (remaining_ > 0) --remaining_;
                }
            }
        }
    }
    return true;
}

void WFCGenerator::writeCellFromMask(MapData& map, uint32_t x, uint32_t y) {
    const size_t idx = static_cast<size_t>(y) * width_ + x;
    const uint8_t resolved = collapsed_[idx];
    Cell& c = map.at(x, y);

    if (resolved == 255u) {
        // Still uncertain — show as frontier.
        c.type   = CellType::Frontier;
        c.height = 0.08f;
        c.color  = { 0, 0, 0, 0 };
        return;
    }

    switch (resolved) {
        case TileWater:
            c.type = CellType::Water;
            c.height = settings_.waterHeight;
            break;
        case TileFloor:
            c.type = CellType::Floor;
            c.height = settings_.floorHeight;
            break;
        case TileWall:
            c.type = CellType::Wall;
            c.height = settings_.wallHeight;
            break;
        case TileMountain:
            c.type = CellType::Mountain;
            c.height = settings_.mountainHeight;
            break;
        default:
            c.type = CellType::Floor;
            c.height = settings_.floorHeight;
            break;
    }
    c.color = { 0, 0, 0, 0 };
    c.metadata = static_cast<uint32_t>(stepsTaken_);
}

void WFCGenerator::setMaskCell(MapData& map, uint32_t x, uint32_t y) {
    writeCellFromMask(map, x, y);
}

void WFCGenerator::highlightCurrent(MapData& map, uint32_t x, uint32_t y) {
    Cell& c = map.at(x, y);
    c.type = CellType::Current;
    c.height = std::max(c.height, 0.30f);
}

GeneratorStep WFCGenerator::step(MapData& map) {
    ++stepsTaken_;
    const uint32_t budget = std::clamp<uint32_t>(settings_.cellsPerStep, 1u, 256u);

    bool changed = false;
    for (uint32_t i = 0; i < budget; ++i) {
        uint32_t x = 0, y = 0;
        const int pop = findMinEntropy(x, y);

        if (pop < 0) {
            // No uncollapsed cell with entropy > 1 — we're done.
            phase_  = Phase::Done;
            status_ = "Collapse complete";
            return { changed, true, status_ };
        }

        // Clear the previous "current" highlight by writing its final cell.
        if (hasCurrent_) setMaskCell(map, currentX_, currentY_);

        const Tile chosen = pickCollapse(x, y);
        wave_[static_cast<size_t>(y) * width_ + x] = maskOf(chosen);
        if (collapsed_[static_cast<size_t>(y) * width_ + x] == 255u && remaining_ > 0) {
            --remaining_;
        }
        collapsed_[static_cast<size_t>(y) * width_ + x] = chosen;

        const bool ok = propagate(x, y);
        if (!ok) {
            // Local contradiction — reset the offender + neighbours so we can recover.
            const int dxs[4] = { 1, -1, 0, 0 };
            const int dys[4] = { 0, 0, 1, -1 };
            for (int d = -1; d < 4; ++d) {
                int rx = static_cast<int>(x) + (d < 0 ? 0 : dxs[d]);
                int ry = static_cast<int>(y) + (d < 0 ? 0 : dys[d]);
                if (rx < 0 || ry < 0 ||
                    rx >= static_cast<int>(width_) ||
                    ry >= static_cast<int>(depth_)) continue;
                const size_t ridx = static_cast<size_t>(ry) * width_ + static_cast<size_t>(rx);
                if (collapsed_[ridx] != 255u && remaining_ < static_cast<uint32_t>(width_) * depth_) {
                    ++remaining_;
                }
                wave_[ridx] = kMaskAll;
                collapsed_[ridx] = 255u;
                Cell& cell = map.at(static_cast<uint32_t>(rx), static_cast<uint32_t>(ry));
                cell.type = CellType::Frontier;
                cell.height = 0.08f;
            }
            status_ = "Contradiction at (" + std::to_string(x) + ", " + std::to_string(y) +
                      ") — local reset";
        }

        // Write any cells that became uniquely determined.
        for (uint32_t yy = 0; yy < depth_; ++yy) {
            for (uint32_t xx = 0; xx < width_; ++xx) {
                const size_t idx = static_cast<size_t>(yy) * width_ + xx;
                if (collapsed_[idx] != 255u) {
                    writeCellFromMask(map, xx, yy);
                }
            }
        }
        // Highlight this collapse so the user can see where work happened.
        highlightCurrent(map, x, y);
        currentX_   = x;
        currentY_   = y;
        hasCurrent_ = true;

        changed = true;
        if (remaining_ == 0) {
            // Final pass: clear the current marker.
            writeCellFromMask(map, currentX_, currentY_);
            phase_  = Phase::Done;
            status_ = "Collapse complete";
            return { changed, true, status_ };
        }
    }

    status_ = "Remaining: " + std::to_string(remaining_);
    return { changed, false, status_ };
}

void registerWFCGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "simple_tiled_wfc",
        .name        = "Simple Tiled WFC",
        .description = "Wave Function Collapse over four terrain tiles (Water/Floor/Wall/Mountain). "
                       "Each step finds the lowest-entropy cell, collapses it with weighted random "
                       "choice, and propagates constraints.",
        .category    = "Tile / WFC",
        .family      = "Constraint Solver",
        .useCase     = "Designer-driven local tile patterns; classroom demo of constraint "
                       "propagation.",
        .priority    = 6,
        .create      = [] { return std::make_unique<WFCGenerator>(); }
    });
}

} // namespace mgv
