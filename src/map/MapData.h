#pragma once

#include "map/Cell.h"

#include <cstdint>
#include <vector>

namespace mgv {

// Plain data container for 2D/3D grid maps. No rendering concerns here —
// algorithms write into this, the renderer reads from it.
//
// Coordinate convention:
//   x : column   (world +X)
//   y : row      (world +Y, "depth")
//   z : layer    (world +Z, "height")
//
// For 2D maps just pass layers = 1 and store per-cell terrain height in
// Cell::height.
class MapData {
public:
    MapData(uint32_t width = 1, uint32_t depth = 1, uint32_t layers = 1);

    uint32_t width()  const { return width_;  }
    uint32_t depth()  const { return depth_;  }
    uint32_t layers() const { return layers_; }
    size_t   size()   const { return cells_.size(); }

    bool inBounds(int32_t x, int32_t y, int32_t z = 0) const;

    Cell&       at(uint32_t x, uint32_t y, uint32_t z = 0);
    const Cell& at(uint32_t x, uint32_t y, uint32_t z = 0) const;

    const std::vector<Cell>& cells() const { return cells_; }

    void clear(CellType t = CellType::Empty);
    void resize(uint32_t width, uint32_t depth, uint32_t layers = 1);

    // Visit every non-empty cell. Fn signature: void(uint32_t x, uint32_t y,
    // uint32_t z, const Cell& cell).
    template <typename Fn>
    void forEachNonEmpty(Fn&& fn) const {
        for (uint32_t z = 0; z < layers_; ++z) {
            for (uint32_t y = 0; y < depth_; ++y) {
                for (uint32_t x = 0; x < width_; ++x) {
                    const auto& c = cells_[index(x, y, z)];
                    if (c.type != CellType::Empty) fn(x, y, z, c);
                }
            }
        }
    }

    // Convenience helpers for building test maps until algorithms exist.
    void fillSampleTerrain(uint32_t seed = 1337);

private:
    size_t index(uint32_t x, uint32_t y, uint32_t z) const {
        return (static_cast<size_t>(z) * depth_ + y) * width_ + x;
    }

    uint32_t width_;
    uint32_t depth_;
    uint32_t layers_;
    std::vector<Cell> cells_;
};

} // namespace mgv
