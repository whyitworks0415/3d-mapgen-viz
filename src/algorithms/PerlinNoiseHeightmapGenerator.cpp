#include "algorithms/PerlinNoiseHeightmapGenerator.h"

#include "algorithms/AlgorithmRegistry.h"
#include "map/MapData.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numeric>
#include <random>
#include <string>

namespace mgv {

void PerlinNoiseHeightmapGenerator::reset(MapData& map, const GeneratorConfig& config) {
    width_ = std::max<uint32_t>(4, config.width);
    depth_ = std::max<uint32_t>(4, config.depth);
    settings_ = config.perlin;
    settings_.baseFrequency = std::clamp(settings_.baseFrequency, 0.001f, 1.0f);
    settings_.octaves = std::clamp<uint32_t>(settings_.octaves, 1, 10);
    settings_.persistence = std::clamp(settings_.persistence, 0.05f, 1.0f);
    settings_.lacunarity = std::clamp(settings_.lacunarity, 1.01f, 5.0f);
    settings_.heightScale = std::max(0.01f, settings_.heightScale);
    settings_.baseHeight = std::max(0.01f, settings_.baseHeight);
    settings_.heightPower = std::clamp(settings_.heightPower, 0.1f, 5.0f);
    settings_.seaLevel = std::clamp(settings_.seaLevel, 0.0f, 1.0f);
    settings_.beachLevel = std::clamp(settings_.beachLevel, settings_.seaLevel, 1.0f);
    settings_.mountainLevel = std::clamp(settings_.mountainLevel, settings_.beachLevel, 1.0f);
    settings_.islandFalloff = std::clamp(settings_.islandFalloff, 0.0f, 1.0f);
    settings_.terraceSteps = std::clamp<uint32_t>(settings_.terraceSteps, 0, 32);
    cellsPerStep_ = std::clamp<uint32_t>(settings_.cellsPerStep, 1, 4096);

    cursor_ = 0;
    currentIndex_ = 0;
    stepsTaken_ = 0;
    finished_ = false;
    hasCurrent_ = false;
    status_ = "Sampling noise";

    initPermutation(config.seed);
    map.resize(width_, depth_, 1);
    map.clear(CellType::Empty);
}

GeneratorStep PerlinNoiseHeightmapGenerator::step(MapData& map) {
    if (finished_) {
        return { false, true, status_ };
    }

    if (hasCurrent_) {
        writeCell(map, currentIndex_ % width_, currentIndex_ / width_, false);
        hasCurrent_ = false;
    }

    const uint32_t total = width_ * depth_;
    const uint32_t end = std::min(total, cursor_ + cellsPerStep_);
    for (; cursor_ < end; ++cursor_) {
        writeCell(map, cursor_ % width_, cursor_ / width_, false);
    }

    ++stepsTaken_;
    if (cursor_ >= total) {
        finished_ = true;
        status_ = "Completed";
        return { true, true, status_ };
    }

    currentIndex_ = cursor_ - 1;
    hasCurrent_ = true;
    writeCell(map, currentIndex_ % width_, currentIndex_ / width_, true);

    const int percent = static_cast<int>(
        (static_cast<float>(cursor_) / static_cast<float>(total)) * 100.0f);
    status_ = "Sampling noise " + std::to_string(percent) + "%";
    return { true, false, status_ };
}

void PerlinNoiseHeightmapGenerator::initPermutation(uint32_t seed) {
    std::array<uint8_t, 256> base {};
    std::iota(base.begin(), base.end(), static_cast<uint8_t>(0));

    std::mt19937 rng(seed);
    std::shuffle(base.begin(), base.end(), rng);

    for (size_t i = 0; i < perm_.size(); ++i) {
        perm_[i] = base[i & 255u];
    }
}

void PerlinNoiseHeightmapGenerator::writeCell(MapData& map,
                                              uint32_t x,
                                              uint32_t y,
                                              bool highlight) const {
    const float nx = static_cast<float>(x) * settings_.baseFrequency;
    const float ny = static_cast<float>(y) * settings_.baseFrequency;
    float value = fractalNoise(nx, ny);

    if (settings_.useIslandFalloff) {
        value *= islandMask(static_cast<float>(x), static_cast<float>(y));
    }
    if (settings_.invert) {
        value = 1.0f - value;
    }

    value = std::clamp(value, 0.0f, 1.0f);
    value = std::pow(value, settings_.heightPower);
    if (settings_.terraceSteps > 1) {
        const float steps = static_cast<float>(settings_.terraceSteps);
        value = std::floor(value * steps) / steps;
    }

    Cell& cell = map.at(x, y);
    cell.flags = 0;
    cell.metadata = static_cast<uint32_t>(value * 1000.0f);
    cell.height = settings_.baseHeight + value * settings_.heightScale;
    cell.color = { 0, 0, 0, 0 };

    if (highlight) {
        cell.type = CellType::Current;
        cell.height = std::max(cell.height, settings_.baseHeight + settings_.heightScale * 0.25f);
        return;
    }

    if (value < settings_.seaLevel) {
        cell.type = CellType::Water;
        cell.height = std::max(settings_.baseHeight, cell.height * 0.35f);
        if (settings_.colorize) cell.color = { 40, 92, 170, 255 };
    } else if (value < settings_.beachLevel) {
        cell.type = CellType::Floor;
        if (settings_.colorize) cell.color = { 194, 178, 118, 255 };
    } else if (value > settings_.mountainLevel) {
        cell.type = CellType::Mountain;
        if (settings_.colorize) {
            const uint8_t shade = static_cast<uint8_t>(std::clamp(140.0f + value * 80.0f, 0.0f, 255.0f));
            cell.color = { shade, shade, shade, 255 };
        }
    } else {
        cell.type = CellType::Room;
        if (settings_.colorize) {
            const uint8_t g = static_cast<uint8_t>(std::clamp(95.0f + value * 100.0f, 0.0f, 255.0f));
            cell.color = { 66, g, 78, 255 };
        }
    }
}

float PerlinNoiseHeightmapGenerator::fractalNoise(float x, float y) const {
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float sum = 0.0f;
    float maxAmplitude = 0.0f;

    for (uint32_t octave = 0; octave < settings_.octaves; ++octave) {
        float n = noise(x * frequency, y * frequency);
        n = n * 0.5f + 0.5f;
        if (settings_.ridged) {
            n = 1.0f - std::abs(n * 2.0f - 1.0f);
        }

        sum += n * amplitude;
        maxAmplitude += amplitude;
        amplitude *= settings_.persistence;
        frequency *= settings_.lacunarity;
    }

    return maxAmplitude > 0.0f ? sum / maxAmplitude : 0.0f;
}

float PerlinNoiseHeightmapGenerator::noise(float x, float y) const {
    const int xi = static_cast<int>(std::floor(x)) & 255;
    const int yi = static_cast<int>(std::floor(y)) & 255;
    const float xf = x - std::floor(x);
    const float yf = y - std::floor(y);

    const float u = fade(xf);
    const float v = fade(yf);

    const uint8_t aa = perm_[perm_[xi] + yi];
    const uint8_t ab = perm_[perm_[xi] + yi + 1];
    const uint8_t ba = perm_[perm_[xi + 1] + yi];
    const uint8_t bb = perm_[perm_[xi + 1] + yi + 1];

    const float x1 = lerp(grad(aa, xf, yf),
                          grad(ba, xf - 1.0f, yf), u);
    const float x2 = lerp(grad(ab, xf, yf - 1.0f),
                          grad(bb, xf - 1.0f, yf - 1.0f), u);
    return lerp(x1, x2, v);
}

float PerlinNoiseHeightmapGenerator::islandMask(float x, float y) const {
    const float cx = (x + 0.5f) / static_cast<float>(width_) * 2.0f - 1.0f;
    const float cy = (y + 0.5f) / static_cast<float>(depth_) * 2.0f - 1.0f;
    const float distance = std::sqrt(cx * cx + cy * cy);
    const float edge = std::clamp(1.0f - distance, 0.0f, 1.0f);
    return std::pow(edge, 0.25f + settings_.islandFalloff * 4.0f);
}

float PerlinNoiseHeightmapGenerator::fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float PerlinNoiseHeightmapGenerator::lerp(float a, float b, float t) {
    return a + t * (b - a);
}

float PerlinNoiseHeightmapGenerator::grad(uint8_t hash, float x, float y) {
    switch (hash & 7u) {
        case 0: return  x + y;
        case 1: return -x + y;
        case 2: return  x - y;
        case 3: return -x - y;
        case 4: return  x;
        case 5: return -x;
        case 6: return  y;
        default: return -y;
    }
}

void registerPerlinNoiseHeightmapGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "perlin_noise_heightmap",
        .name        = "Perlin Noise Heightmap",
        .description = "Animated fractal Perlin terrain with configurable scale, octaves, water, "
                       "beaches, mountains, terraces, and island falloff.",
        .category    = "Terrain",
        .family      = "Noise",
        .useCase     = "Open-world heightmaps, island maps, layered biome studies.",
        .priority    = 3,
        .create      = [] { return std::make_unique<PerlinNoiseHeightmapGenerator>(); }
    });
}

} // namespace mgv
