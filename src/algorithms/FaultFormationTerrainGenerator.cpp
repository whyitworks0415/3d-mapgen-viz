#include "algorithms/FaultFormationTerrainGenerator.h"

#include "algorithms/AlgorithmRegistry.h"
#include "map/MapData.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace mgv {

namespace {
constexpr float kPi = 3.14159265358979323846f;

float clampNorm(float v) { return std::clamp(v, 0.0f, 1.0f); }

glm::u8vec4 terrainColor(float t, const FaultFormationSettings& s) {
    if (!s.colorize) return { 0, 0, 0, 0 };
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
                 static_cast<uint8_t>(80 - k * 30), 255 };
    } else {
        const float k = clampNorm((t - s.mountainLevel) /
                                  std::max(0.001f, 1.0f - s.mountainLevel));
        return { static_cast<uint8_t>(140 + k * 90),
                 static_cast<uint8_t>(130 + k * 100),
                 static_cast<uint8_t>(120 + k * 110), 255 };
    }
}

} // namespace

void FaultFormationTerrainGenerator::reset(MapData& map, const GeneratorConfig& config) {
    settings_ = config.faultFormation;
    width_    = std::max<uint32_t>(8, config.width);
    depth_    = std::max<uint32_t>(8, config.depth);
    rng_.seed(config.seed);

    height_.assign(static_cast<size_t>(width_) * depth_, 0.0f);
    iteration_ = 0;
    phase_     = Phase::ApplyFault;
    minH_      = 0.0f;
    maxH_      = 0.0f;
    stepsTaken_ = 0;
    status_     = "Ready";

    map.resize(width_, depth_, 1);
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

void FaultFormationTerrainGenerator::paintCell(MapData& map, uint32_t x, uint32_t y, float t) {
    Cell& c = map.at(x, y);
    const float scaled = settings_.baseHeight + t * settings_.heightScale;
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
    c.color    = terrainColor(t, settings_);
    c.metadata = static_cast<uint32_t>(stepsTaken_);
}

void FaultFormationTerrainGenerator::paintAll(MapData& map) {
    const float range = std::max(1e-6f, maxH_ - minH_);
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            const float h = height_[static_cast<size_t>(y) * width_ + x];
            paintCell(map, x, y, (h - minH_) / range);
        }
    }
}

void FaultFormationTerrainGenerator::smoothPass() {
    if (settings_.smoothing <= 0.0f) return;
    std::vector<float> tmp = height_;
    for (uint32_t y = 1; y + 1 < depth_; ++y) {
        for (uint32_t x = 1; x + 1 < width_; ++x) {
            float sum = 0.0f;
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox)
                    sum += height_[static_cast<size_t>(y + oy) * width_ + static_cast<size_t>(x + ox)];
            const float avg = sum / 9.0f;
            tmp[static_cast<size_t>(y) * width_ + x] =
                height_[static_cast<size_t>(y) * width_ + x] * (1.0f - settings_.smoothing) +
                avg * settings_.smoothing;
        }
    }
    height_ = std::move(tmp);
}

GeneratorStep FaultFormationTerrainGenerator::step(MapData& map) {
    if (phase_ == Phase::Done) return { false, true, status_ };
    ++stepsTaken_;
    const uint32_t budget = std::clamp<uint32_t>(settings_.cellsPerStep, 1u, 256u);
    bool changed = false;

    if (phase_ == Phase::ApplyFault) {
        std::uniform_real_distribution<float> angDist(0.0f, kPi);
        for (uint32_t k = 0; k < budget && iteration_ < settings_.iterations; ++k, ++iteration_) {
            const float angle = angDist(rng_);
            const float a = std::cos(angle);
            const float b = std::sin(angle);
            // Line passes through random point inside the map.
            std::uniform_real_distribution<float> px(0.0f, static_cast<float>(width_));
            std::uniform_real_distribution<float> py(0.0f, static_cast<float>(depth_));
            const float ox = px(rng_);
            const float oy = py(rng_);
            const float disp = settings_.displacement;
            for (uint32_t y = 0; y < depth_; ++y) {
                for (uint32_t x = 0; x < width_; ++x) {
                    const float dx = static_cast<float>(x) - ox;
                    const float dy = static_cast<float>(y) - oy;
                    const float side = a * dx + b * dy;
                    height_[static_cast<size_t>(y) * width_ + x] += (side >= 0.0f) ? disp : -disp;
                }
            }
            minH_ = *std::min_element(height_.begin(), height_.end());
            maxH_ = *std::max_element(height_.begin(), height_.end());
            paintAll(map);
            changed = true;
        }
        if (iteration_ >= settings_.iterations) {
            phase_ = Phase::Smooth;
            status_ = "Applying smoothing";
        } else {
            status_ = "Faults " + std::to_string(iteration_) + " / "
                      + std::to_string(settings_.iterations);
        }
        return { changed, false, status_ };
    }

    if (phase_ == Phase::Smooth) {
        smoothPass();
        minH_ = *std::min_element(height_.begin(), height_.end());
        maxH_ = *std::max_element(height_.begin(), height_.end());
        paintAll(map);
        phase_  = Phase::Finalise;
        status_ = "Finalising";
        return { true, false, status_ };
    }

    if (phase_ == Phase::Finalise) {
        phase_  = Phase::Done;
        status_ = "Terrain complete";
        return { true, true, status_ };
    }

    return { false, true, status_ };
}

void registerFaultFormationTerrainGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "fault_formation_terrain",
        .name        = "Fault Formation Terrain",
        .description = "Repeats: drop a random line across the map, uplift one side by the current "
                       "displacement. Each step shows one fault being applied; optional smoothing "
                       "evens the result before the final paint.",
        .category    = "Terrain",
        .family      = "Tectonic",
        .useCase     = "Mountain ridges, plateau-and-valley landscapes, retro RTS-style worlds.",
        .priority    = 17,
        .create      = [] { return std::make_unique<FaultFormationTerrainGenerator>(); }
    });
}

} // namespace mgv
