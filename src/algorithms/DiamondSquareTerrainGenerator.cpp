#include "algorithms/DiamondSquareTerrainGenerator.h"

#include "algorithms/AlgorithmRegistry.h"
#include "map/MapData.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace mgv {

namespace {

uint32_t nextPow2GE(uint32_t v) {
    uint32_t p = 1;
    while (p < v) p *= 2;
    return p;
}

float clampNorm(float v) { return std::clamp(v, 0.0f, 1.0f); }

glm::u8vec4 terrainColor(float t, const DiamondSquareSettings& s, bool colorize) {
    if (!colorize) return { 0, 0, 0, 0 };
    if (t < s.seaLevel) {
        const float k = clampNorm(t / std::max(0.001f, s.seaLevel));
        return { static_cast<uint8_t>(20 + k * 30),
                 static_cast<uint8_t>(70 + k * 50),
                 static_cast<uint8_t>(160 + k * 40), 255 };
    } else if (t < s.mountainLevel) {
        const float k = clampNorm((t - s.seaLevel) /
                                  std::max(0.001f, s.mountainLevel - s.seaLevel));
        return { static_cast<uint8_t>(60 + k * 80),
                 static_cast<uint8_t>(140 - k * 40),
                 static_cast<uint8_t>(80  - k * 30), 255 };
    } else {
        const float k = clampNorm((t - s.mountainLevel) / std::max(0.001f, 1.0f - s.mountainLevel));
        return { static_cast<uint8_t>(140 + k * 90),
                 static_cast<uint8_t>(130 + k * 100),
                 static_cast<uint8_t>(120 + k * 110), 255 };
    }
}

} // namespace

float DiamondSquareTerrainGenerator::sampleWrap(int32_t x, int32_t y) const {
    const int32_t s = static_cast<int32_t>(side_);
    if (settings_.wrapEdges) {
        x = ((x % (s + 1)) + (s + 1)) % (s + 1);
        y = ((y % (s + 1)) + (s + 1)) % (s + 1);
    } else {
        x = std::clamp<int32_t>(x, 0, s);
        y = std::clamp<int32_t>(y, 0, s);
    }
    return height_[static_cast<size_t>(y) * (side_ + 1) + static_cast<size_t>(x)];
}

void DiamondSquareTerrainGenerator::reset(MapData& map, const GeneratorConfig& config) {
    settings_ = config.diamondSquare;
    width_    = std::max<uint32_t>(8, config.width);
    depth_    = std::max<uint32_t>(8, config.depth);
    rng_.seed(config.seed);

    side_ = nextPow2GE(std::max(width_, depth_));
    if (side_ < 4) side_ = 4;
    height_.assign(static_cast<size_t>(side_ + 1) * (side_ + 1), 0.0f);

    // Seed the four corners.
    std::uniform_real_distribution<float> corner(-1.0f, 1.0f);
    height_[0]                                             = corner(rng_);
    height_[side_]                                         = corner(rng_);
    height_[static_cast<size_t>(side_) * (side_ + 1)]      = corner(rng_);
    height_[static_cast<size_t>(side_) * (side_ + 1) + side_] = corner(rng_);

    step_       = side_;
    levelIndex_ = 0;
    levelRange_ = 1.0f;
    diamondQueue_.clear();
    squareQueue_.clear();
    diamondCursor_ = 0;
    squareCursor_  = 0;
    phase_         = Phase::Diamond;

    // Enqueue diamond tasks at this level.
    const uint32_t half = step_ / 2;
    for (uint32_t y = 0; y + step_ <= side_; y += step_) {
        for (uint32_t x = 0; x + step_ <= side_; x += step_) {
            diamondQueue_.push_back({ x + half, y + half, half });
        }
    }

    minH_ = 1e9f; maxH_ = -1e9f;
    stepsTaken_ = 0;
    status_     = "Ready";

    map.resize(width_, depth_, 1);
    // Initial paint — flat low surface; will be replaced as cells get heights.
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            Cell& c = map.at(x, y);
            c.type     = CellType::Floor;
            c.height   = settings_.baseHeight;
            c.color    = { 0, 0, 0, 0 };
            c.flags    = 0;
            c.metadata = 0;
        }
    }
}

void DiamondSquareTerrainGenerator::writeHeight(MapData& map, uint32_t x, uint32_t y, float h) {
    height_[static_cast<size_t>(y) * (side_ + 1) + x] = h;
    minH_ = std::min(minH_, h);
    maxH_ = std::max(maxH_, h);
    if (x < width_ && y < depth_) paintTerrainCell(map, x, y, h);
}

void DiamondSquareTerrainGenerator::paintTerrainCell(MapData& map, uint32_t x, uint32_t y, float h) {
    // Normalise current h into [0, 1] given the running min/max.
    const float range = std::max(1e-6f, maxH_ - minH_);
    const float t = (h - minH_) / range;
    const float scaled = settings_.baseHeight + t * settings_.heightScale;

    Cell& c = map.at(x, y);
    if (t < settings_.seaLevel) {
        c.type   = CellType::Water;
        c.height = settings_.baseHeight + settings_.seaLevel * settings_.heightScale * 0.4f;
    } else if (t > settings_.mountainLevel) {
        c.type   = CellType::Mountain;
        c.height = scaled;
    } else {
        c.type   = CellType::Floor;
        c.height = scaled;
    }
    c.color    = terrainColor(t, settings_, settings_.colorize);
    c.metadata = static_cast<uint32_t>(stepsTaken_);
}

void DiamondSquareTerrainGenerator::prepareNextLevel() {
    step_ /= 2;
    levelRange_ *= std::pow(2.0f, -settings_.roughness);
    ++levelIndex_;
    diamondQueue_.clear();
    squareQueue_.clear();
    diamondCursor_ = 0;
    squareCursor_  = 0;
    if (step_ < 1) return;

    const uint32_t half = step_ / 2;
    if (half >= 1) {
        for (uint32_t y = 0; y + step_ <= side_; y += step_) {
            for (uint32_t x = 0; x + step_ <= side_; x += step_) {
                diamondQueue_.push_back({ x + half, y + half, half });
            }
        }
    }
}

GeneratorStep DiamondSquareTerrainGenerator::step(MapData& map) {
    if (phase_ == Phase::Done) return { false, true, status_ };
    ++stepsTaken_;
    const uint32_t budget = std::clamp<uint32_t>(settings_.cellsPerStep, 1u, 4096u);
    std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
    bool changed = false;

    for (uint32_t i = 0; i < budget; ++i) {
        if (phase_ == Phase::Diamond) {
            if (diamondCursor_ < diamondQueue_.size()) {
                const auto& t = diamondQueue_[diamondCursor_++];
                const float h = 0.25f * (
                    sampleWrap(static_cast<int32_t>(t.cx) - static_cast<int32_t>(t.half),
                               static_cast<int32_t>(t.cy) - static_cast<int32_t>(t.half)) +
                    sampleWrap(static_cast<int32_t>(t.cx) + static_cast<int32_t>(t.half),
                               static_cast<int32_t>(t.cy) - static_cast<int32_t>(t.half)) +
                    sampleWrap(static_cast<int32_t>(t.cx) - static_cast<int32_t>(t.half),
                               static_cast<int32_t>(t.cy) + static_cast<int32_t>(t.half)) +
                    sampleWrap(static_cast<int32_t>(t.cx) + static_cast<int32_t>(t.half),
                               static_cast<int32_t>(t.cy) + static_cast<int32_t>(t.half))) +
                    jitter(rng_) * levelRange_;
                writeHeight(map, t.cx, t.cy, h);
                changed = true;
            } else {
                // Build square tasks: midpoints of edges of each parent cell.
                squareQueue_.clear();
                squareCursor_ = 0;
                const uint32_t half = step_ / 2;
                for (uint32_t y = 0; y <= side_; y += half) {
                    for (uint32_t x = ((y / half) % 2 == 0) ? half : 0; x <= side_; x += step_) {
                        squareQueue_.push_back({ x, y, half });
                    }
                }
                phase_ = Phase::Square;
            }
        } else if (phase_ == Phase::Square) {
            if (squareCursor_ < squareQueue_.size()) {
                const auto& t = squareQueue_[squareCursor_++];
                const float h = 0.25f * (
                    sampleWrap(static_cast<int32_t>(t.cx) - static_cast<int32_t>(t.half),
                               static_cast<int32_t>(t.cy)) +
                    sampleWrap(static_cast<int32_t>(t.cx) + static_cast<int32_t>(t.half),
                               static_cast<int32_t>(t.cy)) +
                    sampleWrap(static_cast<int32_t>(t.cx),
                               static_cast<int32_t>(t.cy) - static_cast<int32_t>(t.half)) +
                    sampleWrap(static_cast<int32_t>(t.cx),
                               static_cast<int32_t>(t.cy) + static_cast<int32_t>(t.half))) +
                    jitter(rng_) * levelRange_;
                writeHeight(map, t.cx, t.cy, h);
                changed = true;
            } else {
                phase_ = Phase::NextLevel;
            }
        } else if (phase_ == Phase::NextLevel) {
            if (step_ <= 1) {
                phase_ = Phase::Finalise;
            } else {
                prepareNextLevel();
                phase_ = Phase::Diamond;
            }
        } else if (phase_ == Phase::Finalise) {
            // Final pass: ensure every visible cell has been painted using the
            // final min/max range.
            for (uint32_t y = 0; y < depth_; ++y) {
                for (uint32_t x = 0; x < width_; ++x) {
                    paintTerrainCell(map, x, y,
                                     height_[static_cast<size_t>(y) * (side_ + 1) + x]);
                }
            }
            phase_  = Phase::Done;
            status_ = "Terrain complete";
            return { true, true, status_ };
        }
    }

    status_ = "Level " + std::to_string(levelIndex_) +
              " (step " + std::to_string(step_) + ")";
    return { changed, false, status_ };
}

void registerDiamondSquareTerrainGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "diamond_square_terrain",
        .name        = "Diamond-Square Terrain",
        .description = "Fractal midpoint displacement. Diamond and square sub-steps alternate at "
                       "halving step sizes; each public step performs one averaging.",
        .category    = "Terrain",
        .family      = "Fractal",
        .useCase     = "Retro-style continents, fast plausible terrain, self-similar height fields.",
        .priority    = 16,
        .create      = [] { return std::make_unique<DiamondSquareTerrainGenerator>(); }
    });
}

} // namespace mgv
