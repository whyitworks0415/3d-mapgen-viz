#include "map/MapData.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>

namespace mgv {

MapData::MapData(uint32_t width, uint32_t depth, uint32_t layers)
    : width_(width), depth_(depth), layers_(layers),
      cells_(static_cast<size_t>(width) * depth * layers) {}

bool MapData::inBounds(int32_t x, int32_t y, int32_t z) const {
    return x >= 0 && static_cast<uint32_t>(x) < width_ &&
           y >= 0 && static_cast<uint32_t>(y) < depth_ &&
           z >= 0 && static_cast<uint32_t>(z) < layers_;
}

Cell& MapData::at(uint32_t x, uint32_t y, uint32_t z) {
    return cells_[index(x, y, z)];
}

const Cell& MapData::at(uint32_t x, uint32_t y, uint32_t z) const {
    return cells_[index(x, y, z)];
}

void MapData::clear(CellType t) {
    Cell blank;
    blank.type = t;
    std::fill(cells_.begin(), cells_.end(), blank);
}

void MapData::resize(uint32_t width, uint32_t depth, uint32_t layers) {
    width_  = width;
    depth_  = depth;
    layers_ = layers;
    cells_.assign(static_cast<size_t>(width) * depth * layers, Cell{});
}

void MapData::fillSampleTerrain(uint32_t seed) {
    // Phase 2 placeholder: a walled "room" with checker floor + a few pillars +
    // a soft terrain bump so the user has something visually rich to navigate
    // around. Replaced by real algorithm output in Phase 3+.
    clear(CellType::Empty);

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> jitter(-0.05f, 0.05f);

    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            Cell& c = at(x, y);
            const bool border = (x == 0 || y == 0 ||
                                 x == width_ - 1 || y == depth_ - 1);
            if (border) {
                c.type   = CellType::Wall;
                c.height = 2.0f + jitter(rng);
            } else {
                // Smooth bump in the centre + checker pattern.
                float cx   = (x + 0.5f) - width_ * 0.5f;
                float cy   = (y + 0.5f) - depth_ * 0.5f;
                float r    = std::sqrt(cx * cx + cy * cy);
                float bump = std::max(0.0f, 4.0f - r * 0.25f);
                c.type   = ((x + y) & 1) ? CellType::Floor : CellType::Room;
                c.height = 0.2f + bump + jitter(rng);
            }
        }
    }

    // A couple of obvious pillars to see depth/lighting work.
    if (inBounds(static_cast<int32_t>(width_ / 4), static_cast<int32_t>(depth_ / 4))) {
        auto& p = at(width_ / 4, depth_ / 4);
        p.type = CellType::Mountain;
        p.height = 5.0f;
    }
    if (inBounds(static_cast<int32_t>(3 * width_ / 4), static_cast<int32_t>(3 * depth_ / 4))) {
        auto& p = at(3 * width_ / 4, 3 * depth_ / 4);
        p.type = CellType::Mountain;
        p.height = 7.0f;
    }
}

} // namespace mgv
