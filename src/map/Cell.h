#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace mgv {

enum class CellType : uint8_t {
    Empty = 0,
    Wall,
    Floor,
    Room,
    Corridor,
    Water,
    Mountain,
    Visited,
    Frontier,
    Current,
    Path,
    Count_
};

// Per-cell payload. Phase 2 uses `type`, `height`, `color`. `flags`/`metadata`
// are reserved for algorithm state (e.g. DFS visit order, region id).
struct Cell {
    CellType    type     = CellType::Empty;
    float       height   = 1.0f;
    glm::u8vec4 color    { 0, 0, 0, 0 };    // alpha 0 means "use default for type"
    uint32_t    flags    = 0;
    uint32_t    metadata = 0;
};

// Default visualization color per type. Tweak freely.
inline glm::vec3 defaultColorFor(CellType t) {
    switch (t) {
        case CellType::Empty:    return { 0.00f, 0.00f, 0.00f };
        case CellType::Wall:     return { 0.55f, 0.55f, 0.60f };
        case CellType::Floor:    return { 0.30f, 0.30f, 0.35f };
        case CellType::Room:     return { 0.40f, 0.45f, 0.55f };
        case CellType::Corridor: return { 0.50f, 0.45f, 0.40f };
        case CellType::Water:    return { 0.18f, 0.40f, 0.78f };
        case CellType::Mountain: return { 0.55f, 0.50f, 0.45f };
        case CellType::Visited:  return { 0.22f, 0.60f, 0.30f };
        case CellType::Frontier: return { 0.90f, 0.70f, 0.20f };
        case CellType::Current:  return { 1.00f, 0.20f, 0.20f };
        case CellType::Path:     return { 0.95f, 0.95f, 0.40f };
        case CellType::Count_:   break;
    }
    return { 0.85f, 0.10f, 0.85f };   // magenta = "shouldn't happen"
}

} // namespace mgv
