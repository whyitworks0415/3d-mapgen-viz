#include "algorithms/LSystemGenerator.h"

#include "algorithms/AlgorithmRegistry.h"
#include "map/MapData.h"

#include <algorithm>
#include <memory>

namespace mgv {

namespace {
constexpr int kDx[4] = {  1,  0, -1,  0 };
constexpr int kDy[4] = {  0,  1,  0, -1 };
}

void LSystemGenerator::reset(MapData& map, const GeneratorConfig& config) {
    settings_ = config.lsystem;
    width_    = std::max<uint32_t>(8, config.width);
    depth_    = std::max<uint32_t>(8, config.depth);

    phase_         = Phase::Rewrite;
    stepsTaken_    = 0;
    status_        = "Ready";
    iteration_     = 0;
    renderCursor_  = 0;
    current_.clear();
    rules_.clear();
    stack_.clear();
    hasMarker_     = false;
    drawsF_        = true;
    drawsG_        = false;

    initFromPreset();

    // Solid-empty background.
    map.resize(width_, depth_, 1);
    for (uint32_t y = 0; y < depth_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            Cell& c = map.at(x, y);
            c.type   = CellType::Empty;
            c.height = 0.02f;
            c.color  = { 0, 0, 0, 0 };
        }
    }

    if (settings_.startCentered) {
        turtleX_ = static_cast<int32_t>(width_)  / 2;
        turtleY_ = static_cast<int32_t>(depth_)  / 2;
    } else {
        turtleX_ = 2;
        turtleY_ = 2;
    }
    turtleDir_ = 0;
    current_ = axiom_;
}

void LSystemGenerator::initFromPreset() {
    switch (settings_.preset) {
        case 0:  // Dragon Curve
            axiom_ = "FX";
            rules_['X'] = "X+YF+";
            rules_['Y'] = "-FX-Y";
            drawsF_ = true; drawsG_ = false;
            break;
        case 1:  // Hilbert Curve
            axiom_ = "L";
            rules_['L'] = "+RF-LFL-FR+";
            rules_['R'] = "-LF+RFR+FL-";
            drawsF_ = true; drawsG_ = false;
            break;
        case 2:  // Koch Square
            axiom_ = "F";
            rules_['F'] = "F+F-F-F+F";
            drawsF_ = true; drawsG_ = false;
            break;
        case 3:  // Plant Branching
        default:
            axiom_ = "X";
            rules_['X'] = "F[+X]F[-X]+X";
            rules_['F'] = "FF";
            drawsF_ = true; drawsG_ = false;
            break;
    }
}

void LSystemGenerator::applyOneRewrite() {
    std::string next;
    next.reserve(current_.size() * 2);
    for (char c : current_) {
        auto it = rules_.find(c);
        if (it != rules_.end()) next += it->second;
        else                    next.push_back(c);
    }
    current_ = std::move(next);
}

void LSystemGenerator::paintCell(MapData& map, int32_t x, int32_t y, CellType type, float height) {
    if (x < 0 || y < 0 ||
        x >= static_cast<int32_t>(width_) ||
        y >= static_cast<int32_t>(depth_)) return;
    Cell& c = map.at(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    c.type     = type;
    c.height   = std::max(c.height, height);
    c.color    = { 0, 0, 0, 0 };
    c.metadata = static_cast<uint32_t>(stepsTaken_);
}

void LSystemGenerator::clearCurrentMarker(MapData& map) {
    if (!hasMarker_) return;
    if (markerX_ < 0 || markerY_ < 0 ||
        markerX_ >= static_cast<int32_t>(width_) ||
        markerY_ >= static_cast<int32_t>(depth_)) {
        hasMarker_ = false;
        return;
    }
    Cell& c = map.at(static_cast<uint32_t>(markerX_), static_cast<uint32_t>(markerY_));
    if (c.type == CellType::Current) {
        c.type   = CellType::Floor;
        c.height = settings_.pathHeight;
    }
    hasMarker_ = false;
}

void LSystemGenerator::renderOneSymbol(MapData& map, char sym) {
    switch (sym) {
        case 'F':
        case 'G': {
            const bool draws = (sym == 'F' ? drawsF_ : drawsG_);
            for (uint32_t s = 0; s < settings_.stepLength; ++s) {
                turtleX_ += kDx[turtleDir_];
                turtleY_ += kDy[turtleDir_];
                if (draws) paintCell(map, turtleX_, turtleY_, CellType::Floor, settings_.pathHeight);
            }
            // Highlight current turtle position.
            clearCurrentMarker(map);
            paintCell(map, turtleX_, turtleY_, CellType::Current, settings_.currentHeight);
            hasMarker_ = true;
            markerX_   = turtleX_;
            markerY_   = turtleY_;
            break;
        }
        case '+':
            // Turn left (counter-clockwise on world Y).
            turtleDir_ = (turtleDir_ + 1) % 4;
            break;
        case '-':
            // Turn right.
            turtleDir_ = (turtleDir_ + 3) % 4;
            break;
        case '[':
            stack_.push_back({ turtleX_, turtleY_, turtleDir_ });
            if (settings_.showStack) {
                paintCell(map, turtleX_, turtleY_, CellType::Frontier, settings_.branchHeight);
            }
            break;
        case ']':
            if (!stack_.empty()) {
                const TurtleState s = stack_.back();
                stack_.pop_back();
                turtleX_ = s.x;
                turtleY_ = s.y;
                turtleDir_ = s.dir;
                if (settings_.showStack) {
                    paintCell(map, turtleX_, turtleY_, CellType::Frontier, settings_.branchHeight);
                }
            }
            break;
        default:
            // Variables like X, Y, L, R don't draw or move.
            break;
    }
}

GeneratorStep LSystemGenerator::step(MapData& map) {
    if (phase_ == Phase::Done) return { false, true, status_ };
    ++stepsTaken_;

    if (phase_ == Phase::Rewrite) {
        if (iteration_ < settings_.iterations) {
            applyOneRewrite();
            ++iteration_;
            status_ = "Rewrite iteration " + std::to_string(iteration_) +
                      " / " + std::to_string(settings_.iterations) +
                      "  (string length " + std::to_string(current_.size()) + ")";
            return { false, false, status_ };
        }
        // Start rendering.
        renderCursor_ = 0;
        phase_        = Phase::Render;
        status_       = "Rendering 0%";
        return { false, false, status_ };
    }

    if (phase_ == Phase::Render) {
        const uint32_t budget = std::clamp<uint32_t>(settings_.cellsPerStep, 1u, 4096u);
        bool changed = false;
        for (uint32_t i = 0; i < budget && renderCursor_ < current_.size(); ++i, ++renderCursor_) {
            renderOneSymbol(map, current_[renderCursor_]);
            changed = true;
        }
        if (renderCursor_ >= current_.size()) {
            clearCurrentMarker(map);
            phase_  = Phase::Done;
            status_ = "Done (" + std::to_string(current_.size()) + " symbols)";
            return { changed, true, status_ };
        }
        const int pct = static_cast<int>(100.0 * renderCursor_ / current_.size());
        status_ = "Rendering " + std::to_string(pct) + "%";
        return { changed, false, status_ };
    }

    return { false, true, status_ };
}

void registerLSystemGenerator(AlgorithmRegistry& registry) {
    registry.registerGenerator({
        .id          = "lsystem",
        .name        = "L-System Turtle",
        .description = "Lindenmayer system with a turtle renderer on the cell grid. Four built-in "
                       "presets (Dragon, Hilbert, Koch Square, Plant). Phase 1 applies one rewrite "
                       "per step; phase 2 draws cellsPerStep symbols via the turtle, with [ ] "
                       "branching supported.",
        .category    = "Grammar",
        .family      = "Lindenmayer",
        .useCase     = "Fractal curves, branching plants, deterministic procedural art.",
        .priority    = 18,
        .create      = [] { return std::make_unique<LSystemGenerator>(); }
    });
}

} // namespace mgv
