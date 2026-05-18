#include "algorithms/CatalogAlgorithmGenerators.h"

#include "algorithms/AlgorithmRegistry.h"
#include "algorithms/IMapGenerator.h"
#include "map/MapData.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mgv {
namespace {

enum class CatalogFamily {
    Maze,
    Dungeon,
    Terrain,
    Tile,
    Cellular,
    Graph,
    Noise,
    Sampling,
    Partition,
    City,
    World,
    Road,
    Grammar,
    Agent,
    Optimization,
    AIML,
    Voxel,
    Planet,
    Placement,
    Hybrid,
    Generic
};

struct CatalogSpec {
    std::string id;
    std::string name;
    std::string category;
    CatalogFamily family = CatalogFamily::Generic;
    uint32_t ordinal = 0;
};

struct Action {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t z = 0;
    CellType type = CellType::Empty;
    float height = 1.0f;
    glm::u8vec4 color { 0, 0, 0, 0 };
};

std::string trim(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
}

std::string sanitize(const std::string& s) {
    std::string out;
    bool lastUnderscore = false;
    for (unsigned char ch : s) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
            lastUnderscore = false;
        } else if (!lastUnderscore) {
            out.push_back('_');
            lastUnderscore = true;
        }
    }
    while (!out.empty() && out.front() == '_') out.erase(out.begin());
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "algorithm" : out;
}

uint32_t stableHash(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

std::string lowerCopy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

CatalogFamily familyForCategory(const std::string& category) {
    const std::string c = lowerCopy(category);
    if (c.find("maze") != std::string::npos) return CatalogFamily::Maze;
    if (c.find("dungeon") != std::string::npos) return CatalogFamily::Dungeon;
    if (c.find("terrain") != std::string::npos) return CatalogFamily::Terrain;
    if (c.find("tile") != std::string::npos) return CatalogFamily::Tile;
    if (c.find("cellular") != std::string::npos) return CatalogFamily::Cellular;
    if (c.find("graph") != std::string::npos) return CatalogFamily::Graph;
    if (c.find("noise") != std::string::npos) return CatalogFamily::Noise;
    if (c.find("sampling") != std::string::npos) return CatalogFamily::Sampling;
    if (c.find("partition") != std::string::npos) return CatalogFamily::Partition;
    if (c.find("city") != std::string::npos || c.find("urban") != std::string::npos) return CatalogFamily::City;
    if (c.find("world") != std::string::npos || c.find("region") != std::string::npos) return CatalogFamily::World;
    if (c.find("road") != std::string::npos || c.find("river") != std::string::npos || c.find("path") != std::string::npos) return CatalogFamily::Road;
    if (c.find("grammar") != std::string::npos) return CatalogFamily::Grammar;
    if (c.find("agent") != std::string::npos) return CatalogFamily::Agent;
    if (c.find("evolutionary") != std::string::npos || c.find("optimization") != std::string::npos) return CatalogFamily::Optimization;
    if (c.find("ai") != std::string::npos || c.find("ml") != std::string::npos) return CatalogFamily::AIML;
    if (c.find("voxel") != std::string::npos || c.find("3d") != std::string::npos) return CatalogFamily::Voxel;
    if (c.find("planet") != std::string::npos || c.find("spherical") != std::string::npos) return CatalogFamily::Planet;
    if (c.find("placement") != std::string::npos || c.find("decoration") != std::string::npos) return CatalogFamily::Placement;
    if (c.find("hybrid") != std::string::npos) return CatalogFamily::Hybrid;
    return CatalogFamily::Generic;
}

glm::u8vec4 colorFor(CellType type) {
    switch (type) {
        case CellType::Water: return { 40, 95, 180, 255 };
        case CellType::Floor: return { 185, 170, 110, 255 };
        case CellType::Room: return { 70, 150, 90, 255 };
        case CellType::Corridor: return { 128, 112, 88, 255 };
        case CellType::Wall: return { 132, 132, 144, 255 };
        case CellType::Mountain: return { 170, 170, 165, 255 };
        case CellType::Path: return { 235, 225, 70, 255 };
        case CellType::Frontier: return { 230, 180, 45, 255 };
        case CellType::Current: return { 245, 65, 60, 255 };
        default: return { 0, 0, 0, 0 };
    }
}

class CatalogAlgorithmGenerator final : public IMapGenerator {
public:
    explicit CatalogAlgorithmGenerator(CatalogSpec spec) : spec_(std::move(spec)) {}

    std::string_view id() const override { return spec_.id; }
    std::string_view name() const override { return displayName_; }
    std::string_view description() const override { return description_; }

    void reset(MapData& map, const GeneratorConfig& config) override {
        width_ = std::max<uint32_t>(4, config.width);
        depth_ = std::max<uint32_t>(4, config.depth);
        layers_ = (spec_.family == CatalogFamily::Voxel && config.catalog.use3DLayers)
            ? std::max<uint32_t>(2, config.layers)
            : 1;
        cellsPerStep_ = std::clamp<uint32_t>(config.catalog.cellsPerStep, 1, 4096);
        cursor_ = 0;
        stepsTaken_ = 0;
        finished_ = false;
        actions_.clear();
        status_ = "Ready";

        rng_.seed(config.seed ^ stableHash(spec_.id));
        map.resize(width_, depth_, layers_);

        displayName_ = spec_.name + " [" + spec_.category + "]";
        description_ = "Catalog implementation for " + spec_.category + ": " + spec_.name;

        switch (spec_.family) {
            case CatalogFamily::Maze: buildMaze(map, config); break;
            case CatalogFamily::Dungeon: buildDungeon(map, config); break;
            case CatalogFamily::Terrain:
            case CatalogFamily::Noise:
            case CatalogFamily::World:
            case CatalogFamily::Planet: buildTerrain(map, config); break;
            case CatalogFamily::Tile:
            case CatalogFamily::Grammar:
            case CatalogFamily::AIML: buildTiles(map, config); break;
            case CatalogFamily::Cellular: buildCellular(map, config); break;
            case CatalogFamily::Graph:
            case CatalogFamily::Road:
            case CatalogFamily::City: buildGraph(map, config); break;
            case CatalogFamily::Sampling:
            case CatalogFamily::Placement: buildPlacement(map, config); break;
            case CatalogFamily::Partition: buildPartition(map, config); break;
            case CatalogFamily::Agent: buildAgent(map, config); break;
            case CatalogFamily::Optimization:
            case CatalogFamily::Hybrid: buildHybrid(map, config); break;
            case CatalogFamily::Voxel: buildVoxel(map, config); break;
            default: buildGeneric(map, config); break;
        }

        if (actions_.empty()) {
            finished_ = true;
            status_ = "Completed";
        }
    }

    GeneratorStep step(MapData& map) override {
        if (finished_) return { false, true, status_ };

        const size_t end = std::min(actions_.size(), cursor_ + cellsPerStep_);
        for (; cursor_ < end; ++cursor_) {
            const Action& action = actions_[cursor_];
            Cell& cell = map.at(action.x, action.y, action.z);
            cell.type = action.type;
            cell.height = action.height;
            cell.color = action.color;
            cell.flags = 0;
            cell.metadata = static_cast<uint32_t>(cursor_);
        }

        ++stepsTaken_;
        if (cursor_ >= actions_.size()) {
            finished_ = true;
            status_ = "Completed";
        } else {
            const int percent = static_cast<int>(
                static_cast<float>(cursor_) / static_cast<float>(actions_.size()) * 100.0f);
            status_ = spec_.name + " " + std::to_string(percent) + "%";
        }
        return { true, finished_, status_ };
    }

    bool finished() const override { return finished_; }
    uint64_t stepsTaken() const override { return stepsTaken_; }
    std::string_view status() const override { return status_; }

private:
    size_t index(uint32_t x, uint32_t y) const {
        return static_cast<size_t>(y) * width_ + x;
    }

    void fill(MapData& map, CellType type, float height) const {
        for (uint32_t z = 0; z < layers_; ++z) {
            for (uint32_t y = 0; y < depth_; ++y) {
                for (uint32_t x = 0; x < width_; ++x) {
                    Cell& cell = map.at(x, y, z);
                    cell.type = type;
                    cell.height = height;
                    cell.color = type == CellType::Empty ? glm::u8vec4 { 0, 0, 0, 0 } : colorFor(type);
                    cell.flags = 0;
                    cell.metadata = 0;
                }
            }
        }
    }

    void push(uint32_t x, uint32_t y, CellType type, float height, uint32_t z = 0) {
        if (x >= width_ || y >= depth_ || z >= layers_) return;
        actions_.push_back({ x, y, z, type, height, colorFor(type) });
    }

    void brush(uint32_t x, uint32_t y, uint32_t radius, CellType type, float height, uint32_t z = 0) {
        const int32_t r = static_cast<int32_t>(radius);
        for (int32_t oy = -r; oy <= r; ++oy) {
            for (int32_t ox = -r; ox <= r; ++ox) {
                const int32_t cx = static_cast<int32_t>(x) + ox;
                const int32_t cy = static_cast<int32_t>(y) + oy;
                if (cx >= 0 && cy >= 0) push(static_cast<uint32_t>(cx), static_cast<uint32_t>(cy), type, height, z);
            }
        }
    }

    void line(uint32_t ax, uint32_t ay, uint32_t bx, uint32_t by,
              CellType type, float height, uint32_t radius = 0, uint32_t z = 0) {
        int32_t x = static_cast<int32_t>(ax);
        int32_t y = static_cast<int32_t>(ay);
        const int32_t endX = static_cast<int32_t>(bx);
        const int32_t endY = static_cast<int32_t>(by);
        const int32_t dx = std::abs(endX - x);
        const int32_t sx = x < endX ? 1 : -1;
        const int32_t dy = -std::abs(endY - y);
        const int32_t sy = y < endY ? 1 : -1;
        int32_t err = dx + dy;
        while (true) {
            if (x >= 0 && y >= 0) brush(static_cast<uint32_t>(x), static_cast<uint32_t>(y), radius, type, height, z);
            if (x == endX && y == endY) break;
            const int32_t e2 = err * 2;
            if (e2 >= dy) { err += dy; x += sx; }
            if (e2 <= dx) { err += dx; y += sy; }
        }
    }

    float valueNoise(float x, float y) const {
        const int32_t xi = static_cast<int32_t>(std::floor(x));
        const int32_t yi = static_cast<int32_t>(std::floor(y));
        auto hash = [](int32_t a, int32_t b) {
            uint32_t h = static_cast<uint32_t>(a) * 374761393u + static_cast<uint32_t>(b) * 668265263u;
            h = (h ^ (h >> 13)) * 1274126177u;
            return static_cast<float>((h ^ (h >> 16)) & 0xffffu) / 65535.0f;
        };
        const float xf = x - std::floor(x);
        const float yf = y - std::floor(y);
        const float u = xf * xf * (3.0f - 2.0f * xf);
        const float v = yf * yf * (3.0f - 2.0f * yf);
        const float a = hash(xi, yi);
        const float b = hash(xi + 1, yi);
        const float c = hash(xi, yi + 1);
        const float d = hash(xi + 1, yi + 1);
        const float x1 = a + (b - a) * u;
        const float x2 = c + (d - c) * u;
        return x1 + (x2 - x1) * v;
    }

    float fractal(float x, float y, float complexity) const {
        float amp = 1.0f;
        float freq = 1.0f;
        float sum = 0.0f;
        float maxAmp = 0.0f;
        const uint32_t octaves = 2 + static_cast<uint32_t>(complexity * 6.0f);
        for (uint32_t i = 0; i < octaves; ++i) {
            sum += valueNoise(x * freq, y * freq) * amp;
            maxAmp += amp;
            amp *= 0.5f;
            freq *= 2.0f;
        }
        return maxAmp > 0.0f ? sum / maxAmp : 0.0f;
    }

    void buildMaze(MapData& map, const GeneratorConfig& config) {
        const auto& s = config.catalog;
        fill(map, CellType::Wall, 1.0f);
        const uint32_t spacing = std::max<uint32_t>(2, s.featureSize / 2);
        const uint32_t radius = std::clamp<uint32_t>(s.corridorWidth / 2, 0, 6);
        std::vector<uint8_t> visited(static_cast<size_t>(width_) * depth_, 0);
        std::vector<std::pair<uint32_t, uint32_t>> stack;
        uint32_t sx = 1 + (stableHash(spec_.name) % std::max<uint32_t>(1, (width_ - 2) / spacing)) * spacing;
        uint32_t sy = 1 + ((stableHash(spec_.category) >> 2) % std::max<uint32_t>(1, (depth_ - 2) / spacing)) * spacing;
        sx = std::min(width_ - 2, sx);
        sy = std::min(depth_ - 2, sy);
        stack.push_back({ sx, sy });
        visited[index(sx, sy)] = 1;
        brush(sx, sy, radius, CellType::Current, 0.24f);

        std::array<std::pair<int32_t, int32_t>, 4> dirs = {
            std::pair<int32_t, int32_t> { static_cast<int32_t>(spacing), 0 },
            std::pair<int32_t, int32_t> { -static_cast<int32_t>(spacing), 0 },
            std::pair<int32_t, int32_t> { 0, static_cast<int32_t>(spacing) },
            std::pair<int32_t, int32_t> { 0, -static_cast<int32_t>(spacing) }
        };
        while (!stack.empty()) {
            auto [x, y] = stack.back();
            std::vector<std::pair<uint32_t, uint32_t>> options;
            std::shuffle(dirs.begin(), dirs.end(), rng_);
            for (auto [dx, dy] : dirs) {
                const int32_t nx = static_cast<int32_t>(x) + dx;
                const int32_t ny = static_cast<int32_t>(y) + dy;
                if (nx <= 0 || ny <= 0 || nx >= static_cast<int32_t>(width_ - 1) || ny >= static_cast<int32_t>(depth_ - 1)) continue;
                if (!visited[index(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny))]) {
                    options.push_back({ static_cast<uint32_t>(nx), static_cast<uint32_t>(ny) });
                }
            }
            if (options.empty()) {
                brush(x, y, radius, CellType::Visited, 0.16f);
                stack.pop_back();
                continue;
            }
            const auto [nx, ny] = options.front();
            brush(nx, ny, radius, CellType::Frontier, 0.24f);
            line(x, y, nx, ny, CellType::Corridor, 0.08f, radius);
            visited[index(nx, ny)] = 1;
            stack.push_back({ nx, ny });
            brush(nx, ny, radius, CellType::Current, 0.32f);
        }
    }

    void buildDungeon(MapData& map, const GeneratorConfig& config) {
        const auto& s = config.catalog;
        fill(map, CellType::Wall, 1.0f);
        std::uniform_int_distribution<uint32_t> rw(4, std::max<uint32_t>(5, s.featureSize * 2));
        std::uniform_int_distribution<uint32_t> rh(4, std::max<uint32_t>(5, s.featureSize * 2));
        std::uniform_int_distribution<uint32_t> px(1, width_ - 2);
        std::uniform_int_distribution<uint32_t> py(1, depth_ - 2);
        std::vector<std::pair<uint32_t, uint32_t>> centers;
        const uint32_t rooms = std::clamp<uint32_t>(s.roomCount, 1, 128);
        for (uint32_t i = 0; i < rooms; ++i) {
            const uint32_t w = std::min<uint32_t>(rw(rng_), width_ - 2);
            const uint32_t h = std::min<uint32_t>(rh(rng_), depth_ - 2);
            const uint32_t x = std::min<uint32_t>(px(rng_), width_ - w - 1);
            const uint32_t y = std::min<uint32_t>(py(rng_), depth_ - h - 1);
            for (uint32_t xx = x; xx < x + w; ++xx) {
                push(xx, y, CellType::Frontier, 0.26f);
                push(xx, y + h - 1, CellType::Frontier, 0.26f);
            }
            for (uint32_t yy = y; yy < y + h; ++yy) {
                push(x, yy, CellType::Frontier, 0.26f);
                push(x + w - 1, yy, CellType::Frontier, 0.26f);
            }
            for (uint32_t yy = y; yy < y + h; ++yy) {
                for (uint32_t xx = x; xx < x + w; ++xx) push(xx, yy, CellType::Room, 0.10f);
            }
            centers.push_back({ x + w / 2, y + h / 2 });
            push(x + w / 2, y + h / 2, CellType::Current, 0.34f);
        }
        std::sort(centers.begin(), centers.end());
        for (size_t i = 1; i < centers.size(); ++i) {
            line(centers[i - 1].first, centers[i - 1].second,
                 centers[i].first, centers[i].second,
                 CellType::Corridor, 0.12f, s.corridorWidth / 2);
            push(centers[i].first, centers[i].second, CellType::Path, 0.28f);
        }
    }

    void buildTerrain(MapData& map, const GeneratorConfig& config) {
        const auto& s = config.catalog;
        fill(map, CellType::Empty, 0.01f);
        const float freq = 1.0f / static_cast<float>(std::max<uint32_t>(2, s.featureSize * 3));
        for (uint32_t y = 0; y < depth_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                float v = fractal(static_cast<float>(x) * freq, static_cast<float>(y) * freq, s.complexity);
                if (spec_.family == CatalogFamily::Planet || lowerCopy(spec_.name).find("island") != std::string::npos) {
                    const float cx = (static_cast<float>(x) / std::max(1u, width_ - 1)) * 2.0f - 1.0f;
                    const float cy = (static_cast<float>(y) / std::max(1u, depth_ - 1)) * 2.0f - 1.0f;
                    v *= std::clamp(1.25f - std::sqrt(cx * cx + cy * cy), 0.0f, 1.0f);
                }
                CellType type = CellType::Room;
                if (v < s.waterLevel) type = CellType::Water;
                else if (v > 0.74f) type = CellType::Mountain;
                else if (v < s.waterLevel + 0.08f) type = CellType::Floor;
                if ((x + y * width_) % std::max<uint32_t>(1, s.cellsPerStep * 4) == 0) {
                    push(x, y, CellType::Current, 0.35f);
                }
                push(x, y, type, 0.08f + v * s.heightScale);
            }
        }
    }

    void buildTiles(MapData& map, const GeneratorConfig& config) {
        (void)config;
        fill(map, CellType::Empty, 0.01f);
        std::vector<CellType> previous(width_ * depth_, CellType::Empty);
        std::discrete_distribution<int> pick({ 2.0, 5.0, 2.0, 1.0 });
        for (uint32_t y = 0; y < depth_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                CellType type = static_cast<CellType>(0);
                push(x, y, CellType::Frontier, 0.28f);
                for (int tries = 0; tries < 8; ++tries) {
                    const int p = pick(rng_);
                    type = p == 0 ? CellType::Water : (p == 1 ? CellType::Floor : (p == 2 ? CellType::Wall : CellType::Mountain));
                    const bool leftOk = x == 0 || !(type == CellType::Water && previous[index(x - 1, y)] == CellType::Mountain);
                    const bool upOk = y == 0 || !(type == CellType::Water && previous[index(x, y - 1)] == CellType::Mountain);
                    if (leftOk && upOk) break;
                }
                previous[index(x, y)] = type;
                push(x, y, type, type == CellType::Mountain ? 2.0f : (type == CellType::Wall ? 1.0f : 0.10f));
                if ((x + y) % 7 == 0) push(x, y, CellType::Current, 0.32f);
            }
        }
    }

    void buildCellular(MapData& map, const GeneratorConfig& config) {
        const auto& s = config.catalog;
        fill(map, CellType::Wall, 1.0f);
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);
        std::vector<uint8_t> grid(width_ * depth_, 0);
        for (uint32_t y = 0; y < depth_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                grid[index(x, y)] = chance(rng_) < s.density;
            }
        }
        for (uint32_t iter = 0; iter < s.iterations; ++iter) {
            std::vector<uint8_t> next = grid;
            for (uint32_t y = 0; y < depth_; ++y) {
                for (uint32_t x = 0; x < width_; ++x) {
                    int count = 0;
                    for (int32_t oy = -1; oy <= 1; ++oy) {
                        for (int32_t ox = -1; ox <= 1; ++ox) {
                            if (ox == 0 && oy == 0) continue;
                            const int32_t nx = static_cast<int32_t>(x) + ox;
                            const int32_t ny = static_cast<int32_t>(y) + oy;
                            if (nx < 0 || ny < 0 || nx >= static_cast<int32_t>(width_) || ny >= static_cast<int32_t>(depth_)) ++count;
                            else if (grid[index(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny))]) ++count;
                        }
                    }
                    next[index(x, y)] = count >= 5;
                    if (next[index(x, y)] != grid[index(x, y)]) {
                        push(x, y, CellType::Current, 0.35f);
                    }
                }
            }
            grid = std::move(next);
            for (uint32_t yy = 0; yy < depth_; ++yy) {
                for (uint32_t xx = 0; xx < width_; ++xx) {
                    const bool wall = grid[index(xx, yy)] != 0;
                    push(xx, yy, wall ? CellType::Wall : CellType::Corridor, wall ? 1.0f : 0.08f);
                }
            }
        }
        for (uint32_t y = 0; y < depth_; ++y) {
            for (uint32_t x = 0; x < width_; ++x) {
                const bool wall = grid[index(x, y)] != 0;
                push(x, y, wall ? CellType::Wall : CellType::Corridor, wall ? 1.0f : 0.08f);
            }
        }
    }

    void buildGraph(MapData& map, const GeneratorConfig& config) {
        const auto& s = config.catalog;
        fill(map, CellType::Empty, 0.01f);
        std::uniform_int_distribution<uint32_t> px(2, width_ - 3);
        std::uniform_int_distribution<uint32_t> py(2, depth_ - 3);
        std::vector<std::pair<uint32_t, uint32_t>> nodes;
        for (uint32_t i = 0; i < std::clamp<uint32_t>(s.roomCount, 2, 128); ++i) {
            nodes.push_back({ px(rng_), py(rng_) });
            brush(nodes.back().first, nodes.back().second, 2, CellType::Frontier, 0.28f);
            brush(nodes.back().first, nodes.back().second, 1, CellType::Room, 0.18f);
        }
        std::sort(nodes.begin(), nodes.end());
        for (size_t i = 1; i < nodes.size(); ++i) {
            line(nodes[i - 1].first, nodes[i - 1].second, nodes[i].first, nodes[i].second,
                 CellType::Path, 0.12f, s.corridorWidth / 2);
        }
        for (size_t i = 2; i < nodes.size(); i += 5) {
            line(nodes[i - 2].first, nodes[i - 2].second, nodes[i].first, nodes[i].second,
                 CellType::Corridor, 0.10f, 0);
        }
    }

    void buildPlacement(MapData& map, const GeneratorConfig& config) {
        const auto& s = config.catalog;
        buildTerrain(map, config);
        std::uniform_int_distribution<uint32_t> px(0, width_ - 1);
        std::uniform_int_distribution<uint32_t> py(0, depth_ - 1);
        for (uint32_t i = 0; i < std::clamp<uint32_t>(s.roomCount * 3, 1, 512); ++i) {
            const CellType type = (i % 3 == 0) ? CellType::Mountain : ((i % 3 == 1) ? CellType::Frontier : CellType::Path);
            brush(px(rng_), py(rng_), i % 3 == 0 ? 1 : 0, type, type == CellType::Mountain ? 2.2f : 0.35f);
        }
    }

    void buildPartition(MapData& map, const GeneratorConfig& config) {
        const auto& s = config.catalog;
        fill(map, CellType::Room, 0.10f);
        const uint32_t step = std::max<uint32_t>(3, s.featureSize);
        for (uint32_t x = step; x < width_; x += step) {
            line(x, 0, x, depth_ - 1, CellType::Frontier, 0.32f);
            line(x, 0, x, depth_ - 1, CellType::Wall, 1.0f);
        }
        for (uint32_t y = step; y < depth_; y += step) {
            line(0, y, width_ - 1, y, CellType::Frontier, 0.32f);
            line(0, y, width_ - 1, y, CellType::Wall, 1.0f);
        }
    }

    void buildAgent(MapData& map, const GeneratorConfig& config) {
        const auto& s = config.catalog;
        fill(map, CellType::Wall, 1.0f);
        std::uniform_int_distribution<int> dir(0, 3);
        std::array<std::pair<int32_t, int32_t>, 4> dirs = { std::pair<int32_t, int32_t>{1,0}, {-1,0}, {0,1}, {0,-1} };
        uint32_t x = width_ / 2;
        uint32_t y = depth_ / 2;
        for (uint32_t i = 0; i < width_ * depth_ * std::clamp(s.density, 0.05f, 0.95f); ++i) {
            push(x, y, CellType::Current, 0.34f);
            brush(x, y, s.corridorWidth / 2, CellType::Corridor, 0.08f);
            auto [dx, dy] = dirs[dir(rng_)];
            x = static_cast<uint32_t>(std::clamp(static_cast<int32_t>(x) + dx, 1, static_cast<int32_t>(width_ - 2)));
            y = static_cast<uint32_t>(std::clamp(static_cast<int32_t>(y) + dy, 1, static_cast<int32_t>(depth_ - 2)));
        }
    }

    void buildHybrid(MapData& map, const GeneratorConfig& config) {
        buildDungeon(map, config);
        const size_t dungeonActions = actions_.size();
        buildTerrain(map, config);
        std::rotate(actions_.begin(), actions_.begin() + static_cast<std::ptrdiff_t>(dungeonActions), actions_.end());
    }

    void buildVoxel(MapData& map, const GeneratorConfig& config) {
        const auto& s = config.catalog;
        fill(map, CellType::Empty, 0.01f);
        for (uint32_t z = 0; z < layers_; ++z) {
            for (uint32_t y = 0; y < depth_; ++y) {
                for (uint32_t x = 0; x < width_; ++x) {
                    const float v = fractal(static_cast<float>(x) * 0.07f,
                                            static_cast<float>(y) * 0.07f + static_cast<float>(z) * 0.31f,
                                            s.complexity);
                    if (v > 1.0f - s.density) {
                        push(x, y, v > 0.82f ? CellType::Mountain : CellType::Wall,
                             0.8f + static_cast<float>(z) * 0.08f, z);
                    }
                }
            }
        }
    }

    void buildGeneric(MapData& map, const GeneratorConfig& config) {
        buildTerrain(map, config);
    }

    CatalogSpec spec_;
    std::string displayName_;
    std::string description_;
    uint32_t width_ = 1;
    uint32_t depth_ = 1;
    uint32_t layers_ = 1;
    uint32_t cellsPerStep_ = 256;
    size_t cursor_ = 0;
    uint64_t stepsTaken_ = 0;
    bool finished_ = false;
    std::string status_ = "Ready";
    std::mt19937 rng_ { 1337 };
    std::vector<Action> actions_;
};

std::vector<CatalogSpec> readCatalog() {
    std::vector<CatalogSpec> specs;

#ifdef MGV_ALGORITHM_LIST_FILE
    std::ifstream in(MGV_ALGORITHM_LIST_FILE);
#else
    std::ifstream in("mapAlgorithmList.txt");
#endif
    if (!in) return specs;

    std::string line;
    std::string category = "Catalog";
    uint32_t ordinal = 0;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;

        const size_t dot = line.find(". ");
        if (dot != std::string::npos && std::isdigit(static_cast<unsigned char>(line[0]))) {
            category = trim(line.substr(dot + 2));
            continue;
        }

        if (line.rfind("- ", 0) == 0) {
            const std::string name = trim(line.substr(2));
            CatalogSpec spec;
            spec.name = name;
            spec.category = category;
            spec.family = familyForCategory(category);
            spec.ordinal = ordinal++;
            spec.id = "catalog_" + sanitize(category) + "_" + sanitize(name);
            specs.push_back(std::move(spec));
        }
    }

    return specs;
}

} // namespace

void registerCatalogAlgorithmGenerators(AlgorithmRegistry& registry) {
    // Names that already have a real Tier-A or Tier-B implementation. The
    // catalog skips these so they don't appear twice (once as Featured,
    // once as a generic catalog placeholder).
    static const std::unordered_set<std::string> kShadowedNames = {
        // Maze
        "depth_first_search_maze",
        "recursive_backtracking_maze",
        "randomized_prim_s_maze",
        "randomized_kruskal_s_maze",
        "wilson_s_algorithm",
        "aldous_broder_algorithm",
        "hunt_and_kill_algorithm",
        "growing_tree_algorithm",
        "binary_tree_maze",
        "sidewinder_maze",
        "recursive_division_maze",
        "eller_s_algorithm",
        // Dungeon
        "binary_space_partitioning_bsp_dungeon",
        "bsp_dungeon",
        "random_room_placement_dungeon",
        "random_room_dungeon",
        // Cave
        "cellular_automata_caves",
        "cellular_automata_cave",
        "drunkard_s_walk_caves",
        "drunkard_walk_cave",
        // Terrain
        "perlin_noise_terrain",
        "perlin_noise_heightmap",
        "simplex_noise_terrain",
        "simplex_noise_heightmap",
        "diamond_square_algorithm",
        "diamond_square_terrain",
        "fault_formation",
        "fault_formation_terrain",
        // Tile / Constraint
        "wave_function_collapse_wfc",
        "wave_function_collapse",
        "simple_tiled_wfc",
        // Graph
        "voronoi_maps",
        "voronoi_diagram",
        // Sampling
        "poisson_disk_sampling",
    };

    auto specs = readCatalog();
    size_t skipped = 0;
    for (const CatalogSpec& spec : specs) {
        if (kShadowedNames.count(sanitize(spec.name))) {
            ++skipped;
            continue;
        }
        const std::string category = "Catalog: " + spec.category;
        registry.registerGenerator({
            .id          = spec.id,
            .name        = spec.name,
            .description = "Catalog entry from " + spec.category +
                           ". Uses a generic family-specific generator until a bespoke implementation lands.",
            .category    = category,
            .family      = spec.category,
            .useCase     = "Browse the design space — pick this when you want to see what a "
                           + spec.category + " technique looks like at a glance.",
            .priority    = 1000 + static_cast<int>(spec.ordinal),
            .create      = [spec] { return std::make_unique<CatalogAlgorithmGenerator>(spec); }
        });
    }
    (void)skipped;
}

} // namespace mgv
