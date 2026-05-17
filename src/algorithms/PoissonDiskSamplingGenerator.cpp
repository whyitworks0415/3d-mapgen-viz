#include "algorithms/PoissonDiskSamplingGenerator.h"

#include "algorithms/AlgorithmRegistry.h"
#include "map/MapData.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace mgv {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

void PoissonDiskSamplingGenerator::reset(MapData& map, const GeneratorConfig& config) {
    settings_ = config.poisson;
    width_    = std::max<uint32_t>(8, config.width);
    depth_    = std::max<uint32_t>(8, config.depth);
    rng_.seed(config.seed);

    minDist_  = std::max(2.0f, settings_.minDistance);
    cellSize_ = minDist_ / std::sqrt(2.0f);
    gridW_    = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(width_ / cellSize_)));
    gridH_    = std::max<uint32_t>(1, static_cast<uint32_t>(std::ceil(depth_ / cellSize_)));

    phase_      = Phase::Seed;
    stepsTaken_ = 0;
    status_     = "Ready";
    hasLastHighlight_ = false;

    grid_.assign(static_cast<size_t>(gridW_) * gridH_, -1);
    samples_.clear();
    active_.clear();

    map.resize(width_, depth_, 1);
    paintGround(map);
}

void PoissonDiskSamplingGenerator::paintGround(MapData& map) {
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            Cell& c = map.at(x, y);
            c.type   = settings_.paintGround ? CellType::Floor : CellType::Empty;
            c.height = settings_.groundHeight;
            c.color  = { 0, 0, 0, 0 };
        }
    }
}

void PoissonDiskSamplingGenerator::paintSample(MapData& map, float fx, float fy, bool highlight) {
    const int32_t r = static_cast<int32_t>(std::max<uint32_t>(0, settings_.brushRadius));
    const int32_t cx = static_cast<int32_t>(fx);
    const int32_t cy = static_cast<int32_t>(fy);
    for (int32_t oy = -r; oy <= r; ++oy) {
        for (int32_t ox = -r; ox <= r; ++ox) {
            const int32_t x = cx + ox;
            const int32_t y = cy + oy;
            if (x < 0 || y < 0 ||
                x >= static_cast<int32_t>(width_) ||
                y >= static_cast<int32_t>(depth_)) continue;
            Cell& c = map.at(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
            c.type   = highlight ? CellType::Current : CellType::Room;
            c.height = settings_.sampleHeight;
            c.color  = { 0, 0, 0, 0 };
            c.metadata = static_cast<uint32_t>(stepsTaken_);
        }
    }
}

void PoissonDiskSamplingGenerator::clearHighlight(MapData& map, float fx, float fy) {
    // Replace the Current colour on this sample with its committed colour.
    paintSample(map, fx, fy, /*highlight=*/false);
}

bool PoissonDiskSamplingGenerator::inBounds(float fx, float fy) const {
    return fx >= 0.0f && fy >= 0.0f &&
           fx <  static_cast<float>(width_) &&
           fy <  static_cast<float>(depth_);
}

int PoissonDiskSamplingGenerator::gridIndex(float fx, float fy) const {
    const uint32_t gx = std::min<uint32_t>(gridW_ - 1, static_cast<uint32_t>(fx / cellSize_));
    const uint32_t gy = std::min<uint32_t>(gridH_ - 1, static_cast<uint32_t>(fy / cellSize_));
    return static_cast<int>(gy * gridW_ + gx);
}

bool PoissonDiskSamplingGenerator::tooClose(float fx, float fy) const {
    const int32_t gx = static_cast<int32_t>(fx / cellSize_);
    const int32_t gy = static_cast<int32_t>(fy / cellSize_);
    const float r2 = minDist_ * minDist_;
    for (int32_t oy = -2; oy <= 2; ++oy) {
        for (int32_t ox = -2; ox <= 2; ++ox) {
            const int32_t nx = gx + ox;
            const int32_t ny = gy + oy;
            if (nx < 0 || ny < 0 ||
                nx >= static_cast<int32_t>(gridW_) ||
                ny >= static_cast<int32_t>(gridH_)) continue;
            const int idx = grid_[static_cast<size_t>(ny) * gridW_ + static_cast<size_t>(nx)];
            if (idx < 0) continue;
            const auto& [sx, sy] = samples_[static_cast<size_t>(idx)];
            const float dx = sx - fx;
            const float dy = sy - fy;
            if (dx * dx + dy * dy < r2) return true;
        }
    }
    return false;
}

GeneratorStep PoissonDiskSamplingGenerator::step(MapData& map) {
    ++stepsTaken_;
    const uint32_t budget = std::max<uint32_t>(1, settings_.cellsPerStep);
    bool changed = false;

    if (phase_ == Phase::Seed) {
        std::uniform_real_distribution<float> distX(0.0f, static_cast<float>(width_));
        std::uniform_real_distribution<float> distY(0.0f, static_cast<float>(depth_));
        const float fx = settings_.spawnFromCenter
                            ? static_cast<float>(width_)  * 0.5f
                            : distX(rng_);
        const float fy = settings_.spawnFromCenter
                            ? static_cast<float>(depth_)  * 0.5f
                            : distY(rng_);
        samples_.emplace_back(fx, fy);
        active_.push_back(samples_.size() - 1);
        grid_[static_cast<size_t>(gridIndex(fx, fy))] = static_cast<int>(samples_.size() - 1);
        paintSample(map, fx, fy, /*highlight=*/true);
        hasLastHighlight_ = true;
        lastHighlightX_ = fx;
        lastHighlightY_ = fy;

        phase_  = Phase::Expand;
        status_ = "First sample placed";
        return { true, false, status_ };
    }

    if (phase_ == Phase::Expand) {
        for (uint32_t b = 0; b < budget; ++b) {
            if (active_.empty()) {
                if (hasLastHighlight_) clearHighlight(map, lastHighlightX_, lastHighlightY_);
                phase_  = Phase::Done;
                status_ = "Sampling complete (" + std::to_string(samples_.size()) + " samples)";
                return { changed, true, status_ };
            }

            // Pick a random active sample.
            std::uniform_int_distribution<size_t> pickActive(0, active_.size() - 1);
            const size_t ai = pickActive(rng_);
            const size_t sampleIdx = active_[ai];
            const auto [px, py] = samples_[sampleIdx];

            bool found = false;
            float foundX = 0.0f, foundY = 0.0f;
            std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * kPi);
            std::uniform_real_distribution<float> radDist(minDist_, 2.0f * minDist_);
            const uint32_t k = std::clamp<uint32_t>(settings_.kAttempts, 1u, 64u);
            for (uint32_t t = 0; t < k; ++t) {
                const float angle = angleDist(rng_);
                const float r     = radDist(rng_);
                const float cx    = px + std::cos(angle) * r;
                const float cy    = py + std::sin(angle) * r;
                if (!inBounds(cx, cy)) continue;
                if (tooClose(cx, cy)) continue;
                foundX = cx;
                foundY = cy;
                found = true;
                break;
            }

            if (found) {
                samples_.emplace_back(foundX, foundY);
                active_.push_back(samples_.size() - 1);
                grid_[static_cast<size_t>(gridIndex(foundX, foundY))] = static_cast<int>(samples_.size() - 1);
                if (hasLastHighlight_) clearHighlight(map, lastHighlightX_, lastHighlightY_);
                paintSample(map, foundX, foundY, /*highlight=*/true);
                hasLastHighlight_ = true;
                lastHighlightX_ = foundX;
                lastHighlightY_ = foundY;
                changed = true;
            } else {
                // No valid candidate — retire the active sample.
                active_[ai] = active_.back();
                active_.pop_back();
            }
        }
        status_ = "Samples " + std::to_string(samples_.size()) +
                  ", active " + std::to_string(active_.size());
        return { changed, false, status_ };
    }

    return { false, true, "Done" };
}

void registerPoissonDiskSamplingGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "poisson_disk_sampling",
        .name        = "Poisson Disk Sampling",
        .description = "Bridson's O(n) algorithm. Maintains an active sample list, throws up to k "
                       "annulus candidates per step, accepts the first valid one. Produces uniformly "
                       "spaced point distributions visible one acceptance at a time.",
        .category    = "Sampling",
        .family      = "Dart Throw",
        .useCase     = "Scattered features (trees, ores), texture stippling, blue-noise patterns.",
        .priority    = 9,
        .create      = [] { return std::make_unique<PoissonDiskSamplingGenerator>(); }
    });
}

} // namespace mgv
