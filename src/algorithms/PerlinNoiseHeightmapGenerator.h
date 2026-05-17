#pragma once

#include "algorithms/IMapGenerator.h"

#include <array>
#include <cstdint>
#include <random>
#include <string>

namespace mgv {

class AlgorithmRegistry;
class MapData;

class PerlinNoiseHeightmapGenerator final : public IMapGenerator {
public:
    std::string_view id() const override { return "perlin_noise_heightmap"; }
    std::string_view name() const override { return "Perlin Noise Heightmap"; }
    std::string_view description() const override {
        return "Fractal Perlin terrain generation with animated cell filling, water, beaches, mountains, terraces, and island falloff.";
    }

    void reset(MapData& map, const GeneratorConfig& config) override;
    GeneratorStep step(MapData& map) override;

    bool finished() const override { return finished_; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    void initPermutation(uint32_t seed);
    void writeCell(MapData& map, uint32_t x, uint32_t y, bool highlight) const;
    float fractalNoise(float x, float y) const;
    float noise(float x, float y) const;
    float islandMask(float x, float y) const;

    static float fade(float t);
    static float lerp(float a, float b, float t);
    static float grad(uint8_t hash, float x, float y);

    uint32_t width_ = 1;
    uint32_t depth_ = 1;
    uint32_t cellsPerStep_ = 64;
    uint32_t cursor_ = 0;
    uint32_t currentIndex_ = 0;
    uint64_t stepsTaken_ = 0;
    bool finished_ = false;
    bool hasCurrent_ = false;

    PerlinNoiseSettings settings_;
    std::array<uint8_t, 512> perm_ {};
    std::string status_ = "Ready";
};

void registerPerlinNoiseHeightmapGenerator(AlgorithmRegistry& registry);

} // namespace mgv
