#pragma once

#include "algorithms/IMapGenerator.h"
#include "map/Cell.h"
#include "map/MapData.h"

#include <algorithm>
#include <cstdint>

namespace mgv {

// Shared helpers for grid-based maze generators (nodes at coords
// (1 + col*spacing, 1 + row*spacing); walls between nodes carve through
// the cell at the midpoint). Header-only.
struct MazeGrid {
    uint32_t width   = 1;
    uint32_t depth   = 1;
    uint32_t spacing = 2;
    uint32_t cols    = 0;
    uint32_t rows    = 0;

    void init(uint32_t w, uint32_t d, uint32_t s) {
        spacing = std::max<uint32_t>(2, s);
        width   = std::max<uint32_t>(2 * spacing + 3, w);
        depth   = std::max<uint32_t>(2 * spacing + 3, d);
        cols    = (width  - 1) / spacing;
        rows    = (depth  - 1) / spacing;
        if (cols < 2) cols = 2;
        if (rows < 2) rows = 2;
    }

    uint32_t cellX(uint32_t col) const { return 1u + col * spacing; }
    uint32_t cellY(uint32_t row) const { return 1u + row * spacing; }
    size_t   nodeIndex(uint32_t col, uint32_t row) const {
        return static_cast<size_t>(row) * cols + col;
    }
    bool nodeInBounds(int32_t col, int32_t row) const {
        return col >= 0 && row >= 0 &&
               static_cast<uint32_t>(col) < cols &&
               static_cast<uint32_t>(row) < rows;
    }
};

// Reset map to solid wall fill.
inline void fillWallMap(MapData& map, const MazeGrid& g, float wallHeight) {
    map.resize(g.width, g.depth, 1);
    for (uint32_t y = 0; y < g.depth; ++y) {
        for (uint32_t x = 0; x < g.width; ++x) {
            Cell& c = map.at(x, y);
            c.type     = CellType::Wall;
            c.height   = wallHeight;
            c.color    = { 0, 0, 0, 0 };
            c.flags    = 0;
            c.metadata = 0;
        }
    }
}

inline void carveBrush(MapData& map, const MazeGrid& g, uint32_t cx, uint32_t cy,
                       uint32_t corridorWidth, CellType type, float height,
                       uint64_t stepTag) {
    const int32_t halfLo = static_cast<int32_t>((corridorWidth - 1) / 2);
    const int32_t halfHi = static_cast<int32_t>(corridorWidth / 2);
    for (int32_t oy = -halfLo; oy <= halfHi; ++oy) {
        for (int32_t ox = -halfLo; ox <= halfHi; ++ox) {
            const int32_t x = static_cast<int32_t>(cx) + ox;
            const int32_t y = static_cast<int32_t>(cy) + oy;
            if (x <= 0 || y <= 0 ||
                x >= static_cast<int32_t>(g.width)  - 1 ||
                y >= static_cast<int32_t>(g.depth)  - 1) continue;
            Cell& c = map.at(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
            c.type     = type;
            c.height   = height;
            c.color    = { 0, 0, 0, 0 };
            c.metadata = static_cast<uint32_t>(stepTag);
        }
    }
}

inline void carveLine(MapData& map, const MazeGrid& g,
                      uint32_t ax, uint32_t ay, uint32_t bx, uint32_t by,
                      uint32_t corridorWidth, CellType type, float height,
                      uint64_t stepTag) {
    int32_t x  = static_cast<int32_t>(ax);
    int32_t y  = static_cast<int32_t>(ay);
    const int32_t ex = static_cast<int32_t>(bx);
    const int32_t ey = static_cast<int32_t>(by);
    const int32_t sx = (ex > x) ? 1 : (ex < x ? -1 : 0);
    const int32_t sy = (ey > y) ? 1 : (ey < y ? -1 : 0);
    carveBrush(map, g, static_cast<uint32_t>(x), static_cast<uint32_t>(y),
               corridorWidth, type, height, stepTag);
    while (x != ex || y != ey) {
        if (x != ex) x += sx;
        if (y != ey) y += sy;
        carveBrush(map, g, static_cast<uint32_t>(x), static_cast<uint32_t>(y),
                   corridorWidth, type, height, stepTag);
    }
}

inline void markCell(MapData& map, uint32_t x, uint32_t y, CellType type, float height) {
    Cell& c = map.at(x, y);
    c.type   = type;
    c.height = std::max(c.height, height);
}

} // namespace mgv
