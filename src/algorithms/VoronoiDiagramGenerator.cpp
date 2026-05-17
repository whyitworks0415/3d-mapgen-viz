#include "algorithms/VoronoiDiagramGenerator.h"

#include "algorithms/AlgorithmRegistry.h"
#include "map/MapData.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>

namespace mgv {

namespace {

// HSV → RGB for distinct seed colours.
glm::u8vec3 hsvToRgb(float h, float s, float v) {
    h = std::fmod(h, 1.0f); if (h < 0.0f) h += 1.0f;
    const float c  = v * s;
    const float hp = h * 6.0f;
    const float x  = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float r = 0, g = 0, b = 0;
    if      (hp < 1) { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else             { r = c; b = x; }
    const float m = v - c;
    return {
        static_cast<uint8_t>(std::clamp((r + m) * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp((g + m) * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp((b + m) * 255.0f, 0.0f, 255.0f))
    };
}

} // namespace

void VoronoiDiagramGenerator::reset(MapData& map, const GeneratorConfig& config) {
    settings_ = config.voronoi;
    width_    = std::max<uint32_t>(8, config.width);
    depth_    = std::max<uint32_t>(8, config.depth);
    rng_.seed(config.seed);

    phase_      = Phase::ScatterSeeds;
    stepsTaken_ = 0;
    status_     = "Ready";
    cursor_     = 0;
    lloydIter_  = 0;

    const uint32_t numSeeds = std::clamp<uint32_t>(settings_.numSeeds, 2u, 1024u);
    seeds_.clear();
    seeds_.reserve(numSeeds);
    assignment_.assign(static_cast<size_t>(width_) * depth_, -1);

    // Pre-generate seed positions and colours; ScatterSeeds reveals them one by one.
    std::uniform_real_distribution<float> distX(1.0f, static_cast<float>(width_  - 1));
    std::uniform_real_distribution<float> distY(1.0f, static_cast<float>(depth_  - 1));
    for (uint32_t i = 0; i < numSeeds; ++i) {
        Seed seed;
        seed.x     = distX(rng_);
        seed.y     = distY(rng_);
        seed.color = colorForSeed(i);
        seeds_.push_back(seed);
    }

    map.resize(width_, depth_, 1);
    // Start blank — fill cells become coloured as they're assigned.
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            Cell& c = map.at(x, y);
            c.type     = CellType::Floor;
            c.height   = 0.04f;
            c.color    = { 30, 30, 36, 255 };  // dark background
            c.flags    = 0;
            c.metadata = 0;
        }
    }
}

glm::u8vec3 VoronoiDiagramGenerator::colorForSeed(uint32_t seedIdx) const {
    const float hue = std::fmod(seedIdx * 0.6180339887f, 1.0f);   // golden-ratio spacing
    const float sat = 0.55f + 0.35f * std::sin(seedIdx * 1.234f);
    const float val = 0.85f;
    return hsvToRgb(hue, std::clamp(sat, 0.45f, 0.95f), val);
}

int VoronoiDiagramGenerator::nearestSeed(uint32_t x, uint32_t y) const {
    int best = -1;
    float bestDist = std::numeric_limits<float>::infinity();
    const float px = static_cast<float>(x) + 0.5f;
    const float py = static_cast<float>(y) + 0.5f;
    for (size_t i = 0; i < seeds_.size(); ++i) {
        const float dx = seeds_[i].x - px;
        const float dy = seeds_[i].y - py;
        const float d  = dx * dx + dy * dy;
        if (d < bestDist) {
            bestDist = d;
            best     = static_cast<int>(i);
        }
    }
    return best;
}

void VoronoiDiagramGenerator::paintCell(MapData& map, uint32_t x, uint32_t y,
                                       int seedIdx, bool highlight) {
    Cell& c = map.at(x, y);
    if (seedIdx < 0) {
        c.color = { 30, 30, 36, 255 };
        c.height = 0.04f;
        c.type   = CellType::Floor;
        return;
    }
    const auto& s = seeds_[static_cast<size_t>(seedIdx)];
    c.color = { s.color.r, s.color.g, s.color.b, 255 };
    c.height = highlight ? settings_.seedMarkerHeight : settings_.floorHeight;
    c.type   = CellType::Floor;
    c.metadata = static_cast<uint32_t>(stepsTaken_);
}

void VoronoiDiagramGenerator::recomputeAssignments(MapData& map) {
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            const int seed = nearestSeed(x, y);
            assignment_[static_cast<size_t>(y) * width_ + x] = seed;
            paintCell(map, x, y, seed, false);
        }
    }
}

void VoronoiDiagramGenerator::runLloydPass() {
    std::vector<float> sumX(seeds_.size(), 0.0f);
    std::vector<float> sumY(seeds_.size(), 0.0f);
    std::vector<uint32_t> count(seeds_.size(), 0u);
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            const int s = assignment_[static_cast<size_t>(y) * width_ + x];
            if (s < 0) continue;
            sumX[static_cast<size_t>(s)] += static_cast<float>(x) + 0.5f;
            sumY[static_cast<size_t>(s)] += static_cast<float>(y) + 0.5f;
            ++count[static_cast<size_t>(s)];
        }
    }
    for (size_t i = 0; i < seeds_.size(); ++i) {
        if (count[i] == 0) continue;
        seeds_[i].x = sumX[i] / static_cast<float>(count[i]);
        seeds_[i].y = sumY[i] / static_cast<float>(count[i]);
    }
}

void VoronoiDiagramGenerator::finaliseTerrain(MapData& map) {
    // Count region sizes, then classify smallest fraction as water, largest as mountain.
    std::vector<uint32_t> sizes(seeds_.size(), 0u);
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            const int s = assignment_[static_cast<size_t>(y) * width_ + x];
            if (s >= 0) ++sizes[static_cast<size_t>(s)];
        }
    }
    std::vector<size_t> order(seeds_.size());
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) { return sizes[a] < sizes[b]; });

    const size_t waterCount    = static_cast<size_t>(seeds_.size() * settings_.waterFraction);
    const size_t mountainCount = static_cast<size_t>(seeds_.size() * settings_.mountainFraction);

    std::vector<uint8_t> kind(seeds_.size(), 1u);  // 0=water, 1=land, 2=mountain
    for (size_t i = 0; i < waterCount && i < order.size(); ++i)              kind[order[i]] = 0;
    for (size_t i = 0; i < mountainCount && i < order.size(); ++i)
        kind[order[order.size() - 1 - i]] = 2;

    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            const int s = assignment_[static_cast<size_t>(y) * width_ + x];
            if (s < 0) continue;
            Cell& c = map.at(x, y);
            switch (kind[static_cast<size_t>(s)]) {
                case 0:
                    c.type = CellType::Water;
                    c.height = settings_.waterHeight;
                    break;
                case 2:
                    c.type = CellType::Mountain;
                    c.height = settings_.mountainHeight;
                    break;
                default:
                    c.type = CellType::Floor;
                    c.height = settings_.floorHeight;
                    break;
            }
            // Keep the seed colour to preserve the diagram.
        }
    }

    if (settings_.showSeeds) {
        for (const Seed& s : seeds_) {
            const uint32_t sx = std::min<uint32_t>(width_  - 1, static_cast<uint32_t>(s.x));
            const uint32_t sy = std::min<uint32_t>(depth_  - 1, static_cast<uint32_t>(s.y));
            Cell& c = map.at(sx, sy);
            c.type   = CellType::Current;
            c.height = settings_.seedMarkerHeight;
        }
    }
}

GeneratorStep VoronoiDiagramGenerator::step(MapData& map) {
    ++stepsTaken_;

    if (phase_ == Phase::ScatterSeeds) {
        if (cursor_ < seeds_.size()) {
            const Seed& s = seeds_[cursor_];
            const uint32_t sx = std::clamp<uint32_t>(static_cast<uint32_t>(s.x), 0u, width_ - 1);
            const uint32_t sy = std::clamp<uint32_t>(static_cast<uint32_t>(s.y), 0u, depth_ - 1);
            Cell& c = map.at(sx, sy);
            c.type   = CellType::Current;
            c.height = settings_.seedMarkerHeight;
            c.color  = { s.color.r, s.color.g, s.color.b, 255 };
            ++cursor_;
            status_ = "Seeds: " + std::to_string(cursor_) + "/" + std::to_string(seeds_.size());
            return { true, false, status_ };
        }
        cursor_ = 0;
        phase_  = Phase::AssignCells;
        status_ = "Assigning cells to nearest seed";
        return { false, false, status_ };
    }

    if (phase_ == Phase::AssignCells) {
        const size_t total = static_cast<size_t>(width_) * depth_;
        const uint32_t budget = std::max<uint32_t>(1, settings_.cellsPerStep);
        bool changed = false;
        for (uint32_t i = 0; i < budget && cursor_ < total; ++i, ++cursor_) {
            const uint32_t x = static_cast<uint32_t>(cursor_ % width_);
            const uint32_t y = static_cast<uint32_t>(cursor_ / width_);
            const int seed = nearestSeed(x, y);
            assignment_[cursor_] = seed;
            paintCell(map, x, y, seed, false);
            changed = true;
        }
        if (cursor_ >= total) {
            cursor_ = 0;
            if (settings_.lloydIterations > 0) {
                phase_  = Phase::LloydRelaxation;
                lloydIter_ = 0;
                status_ = "Lloyd pass 1 of " + std::to_string(settings_.lloydIterations);
            } else if (settings_.terrainMode) {
                phase_  = Phase::Finalise;
                status_ = "Classifying terrain";
            } else {
                phase_  = Phase::Done;
                status_ = "Diagram complete";
            }
            return { changed, phase_ == Phase::Done, status_ };
        }
        status_ = "Assigning " + std::to_string(static_cast<int>(100.0 * cursor_ / total)) + "%";
        return { changed, false, status_ };
    }

    if (phase_ == Phase::LloydRelaxation) {
        runLloydPass();
        recomputeAssignments(map);
        ++lloydIter_;
        if (lloydIter_ >= settings_.lloydIterations) {
            if (settings_.terrainMode) {
                phase_  = Phase::Finalise;
                status_ = "Classifying terrain";
            } else {
                phase_  = Phase::Done;
                status_ = "Diagram complete";
            }
        } else {
            status_ = "Lloyd pass " + std::to_string(lloydIter_ + 1) + " of "
                      + std::to_string(settings_.lloydIterations);
        }
        return { true, phase_ == Phase::Done, status_ };
    }

    if (phase_ == Phase::Finalise) {
        finaliseTerrain(map);
        phase_  = Phase::Done;
        status_ = "Terrain classified";
        return { true, true, status_ };
    }

    return { false, true, "Done" };
}

void registerVoronoiDiagramGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "voronoi_diagram",
        .name        = "Voronoi Diagram",
        .description = "Scatters seed points, assigns every cell to the nearest seed, and optionally "
                       "applies Lloyd relaxation. Terrain mode classifies smallest regions as water "
                       "and largest as mountains.",
        .category    = "Graph",
        .family      = "Nearest-Neighbour",
        .useCase     = "Region/biome maps, cell-shaded country diagrams, civic district boundaries.",
        .priority    = 8,
        .create      = [] { return std::make_unique<VoronoiDiagramGenerator>(); }
    });
}

} // namespace mgv
