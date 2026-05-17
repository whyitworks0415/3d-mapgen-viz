#include "app/Application.h"

#include "algorithms/AlgorithmRegistry.h"
#include "algorithms/IMapGenerator.h"
#include "core/Camera.h"
#include "core/CameraController.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "map/MapData.h"
#include "renderer/GridRenderer.h"
#include "renderer/Renderer.h"
#include "renderer/Swapchain.h"
#include "renderer/VulkanContext.h"
#include "ui/ImGuiLayer.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <string>

namespace mgv {

namespace {

std::string toLowerCopy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool containsCI(const std::string& haystack, const std::string& needleLower) {
    if (needleLower.empty()) return true;
    return toLowerCopy(haystack).find(needleLower) != std::string::npos;
}

// Visual band for a descriptor — drives icon + colour in the browser.
enum class Band { Featured, Planned, Catalog };

Band bandFor(int priority) {
    if (priority < 20)    return Band::Featured;
    if (priority < 1000)  return Band::Planned;
    return Band::Catalog;
}

ImVec4 bandColor(Band b) {
    switch (b) {
        case Band::Featured: return ImVec4(0.45f, 0.85f, 0.55f, 1.0f);
        case Band::Planned:  return ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
        case Band::Catalog:  return ImVec4(0.65f, 0.65f, 0.70f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

const char* bandLabel(Band b) {
    switch (b) {
        case Band::Featured: return "Featured";
        case Band::Planned:  return "Planned";
        case Band::Catalog:  return "Catalog";
    }
    return "?";
}

void drawTag(const char* text, ImVec4 color) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x * 0.25f, color.y * 0.25f, color.z * 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x * 0.25f, color.y * 0.25f, color.z * 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(color.x * 0.25f, color.y * 0.25f, color.z * 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::SmallButton(text);
    ImGui::PopStyleColor(4);
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
Application::Application() {
    window_     = std::make_unique<Window>(1280, 800,
                                           "3D Map Generation Algorithm Visualizer");
    context_    = std::make_unique<VulkanContext>(*window_, "MapGenViz");
    swapchain_  = std::make_unique<Swapchain>(*context_, *window_);
    renderer_   = std::make_unique<Renderer>(*context_, *swapchain_, *window_);
    imguiLayer_ = std::make_unique<ImGuiLayer>(*window_, *context_,
                                               *renderer_, *swapchain_);

    registerBuiltInAlgorithms(AlgorithmRegistry::instance());
    rebuildAlgorithmGroupsIfNeeded();

    map_           = std::make_unique<MapData>(mapWidth_, mapDepth_, mapLayers_);
    resetGenerator();

    camera_        = std::make_unique<Camera>();
    gridRenderer_  = std::make_unique<GridRenderer>(*context_, *renderer_, *swapchain_);
    gridRenderer_->updateFromMap(*map_);

    cameraController_ = std::make_unique<CameraController>(*camera_, *window_);
    cameraController_->setMap(map_.get());

    focusCameraOnMap();
}

Application::~Application() {
    if (context_) context_->waitIdle();
}

void Application::focusCameraOnMap() {
    if (!camera_ || !map_) return;
    glm::vec3 center(map_->width()  * 0.5f,
                     map_->depth()  * 0.5f,
                     map_->layers() * 0.5f);
    float radius = 0.65f * std::max({ static_cast<float>(map_->width()),
                                      static_cast<float>(map_->depth()),
                                      static_cast<float>(map_->layers()) });
    camera_->focusOn(center, radius);
}

// ---------------------------------------------------------------------------
// Generator plumbing
// ---------------------------------------------------------------------------
GeneratorConfig Application::currentGeneratorConfig() const {
    GeneratorConfig config;
    config.width  = static_cast<uint32_t>(std::max(5, mapWidth_));
    config.depth  = static_cast<uint32_t>(std::max(5, mapDepth_));
    config.layers = static_cast<uint32_t>(std::max(1, mapLayers_));
    config.seed   = static_cast<uint32_t>(seed_);
    config.dfs              = dfsSettings_;
    config.roomDungeon      = roomDungeonSettings_;
    config.perlin           = perlinSettings_;
    config.classicMaze      = classicMazeSettings_;
    config.bspDungeon       = bspDungeonSettings_;
    config.cellularAutomata = cellularAutomataSettings_;
    config.drunkardWalk     = drunkardWalkSettings_;
    config.simplex          = simplexSettings_;
    config.diamondSquare    = diamondSquareSettings_;
    config.faultFormation   = faultFormationSettings_;
    config.wfc              = wfcSettings_;
    config.catalog          = catalogSettings_;
    config.eller            = ellerSettings_;
    config.voronoi          = voronoiSettings_;
    config.poisson          = poissonSettings_;
    return config;
}

void Application::resetGenerator() {
    const auto& algorithms = AlgorithmRegistry::instance().algorithms();
    playbackRunning_     = false;
    playbackAccumulator_ = 0.0f;

    if (algorithms.empty()) {
        generator_.reset();
        map_->resize(static_cast<uint32_t>(mapWidth_),
                     static_cast<uint32_t>(mapDepth_),
                     static_cast<uint32_t>(mapLayers_));
        map_->fillSampleTerrain(static_cast<uint32_t>(seed_));
        playbackMessage_ = "No registered algorithms";
        uploadMapToRenderer();
        return;
    }

    selectedAlgorithmIndex_ = std::clamp(selectedAlgorithmIndex_, 0,
        static_cast<int>(algorithms.size()) - 1);

    const AlgorithmDescriptor& descriptor =
        algorithms[static_cast<size_t>(selectedAlgorithmIndex_)];
    generator_ = descriptor.create();

    if (!generator_) {
        playbackMessage_ = "Failed to create generator";
        uploadMapToRenderer();
        return;
    }

    generator_->reset(*map_, currentGeneratorConfig());
    playbackMessage_ = std::string(generator_->status());

    mapWidth_  = static_cast<int>(map_->width());
    mapDepth_  = static_cast<int>(map_->depth());
    mapLayers_ = static_cast<int>(map_->layers());

    uploadMapToRenderer();
}

void Application::stepGenerator() {
    if (!generator_) return;
    const GeneratorStep step = generator_->step(*map_);
    playbackMessage_ = step.message;
    if (step.completed)   playbackRunning_ = false;
    if (step.mapChanged)  uploadMapToRenderer();
}

void Application::advancePlayback(float dt) {
    if (!playbackRunning_ || !generator_) return;
    if (generator_->finished()) {
        playbackRunning_ = false;
        return;
    }

    playbackAccumulator_ += dt * playbackSpeed_;
    int stepsToRun = std::min(static_cast<int>(playbackAccumulator_), 256);
    if (stepsToRun <= 0) return;
    playbackAccumulator_ -= static_cast<float>(stepsToRun);

    bool mapChanged = false;
    for (int i = 0; i < stepsToRun; ++i) {
        const GeneratorStep step = generator_->step(*map_);
        playbackMessage_ = step.message;
        mapChanged = mapChanged || step.mapChanged;
        if (step.completed) {
            playbackRunning_     = false;
            playbackAccumulator_ = 0.0f;
            break;
        }
    }

    if (mapChanged) uploadMapToRenderer();
}

void Application::uploadMapToRenderer() {
    if (gridRenderer_)     gridRenderer_->updateFromMap(*map_);
    if (cameraController_) cameraController_->setMap(map_.get());
}

void Application::selectAlgorithm(int globalIndex) {
    const auto& algorithms = AlgorithmRegistry::instance().algorithms();
    if (algorithms.empty()) return;
    globalIndex = std::clamp(globalIndex, 0, static_cast<int>(algorithms.size()) - 1);
    if (globalIndex == selectedAlgorithmIndex_) return;
    selectedAlgorithmIndex_ = globalIndex;
    resetGenerator();
}

// ---------------------------------------------------------------------------
// Browser groups
// ---------------------------------------------------------------------------
void Application::rebuildAlgorithmGroupsIfNeeded() {
    const auto& algorithms = AlgorithmRegistry::instance().algorithms();
    if (groupsRegistryRevision_ == algorithms.size() && !groups_.empty()) return;
    groupsRegistryRevision_ = algorithms.size();
    groups_.clear();

    // Sort algorithms into a stable order: category -> priority -> name.
    std::map<std::string, std::vector<const AlgorithmDescriptor*>> byCategory;
    for (const auto& a : algorithms) {
        std::string cat = a.category.empty() ? "Other" : a.category;
        byCategory[cat].push_back(&a);
    }

    auto less = [](const AlgorithmDescriptor* a, const AlgorithmDescriptor* b) {
        if (a->priority != b->priority) return a->priority < b->priority;
        return a->name < b->name;
    };

    // Featured first: any category whose top-priority entry is < 20.
    auto isFeaturedCat = [&](const std::vector<const AlgorithmDescriptor*>& v) {
        return std::any_of(v.begin(), v.end(), [](const AlgorithmDescriptor* d) {
            return d->priority < 20;
        });
    };
    auto isCatalogCat = [&](const std::string& cat) {
        return cat.rfind("Catalog:", 0) == 0;
    };

    // Featured + Planned bands (non-catalog) first, alphabetical.
    for (auto& [cat, vec] : byCategory) {
        if (isCatalogCat(cat)) continue;
        std::sort(vec.begin(), vec.end(), less);
        AlgorithmGroup g;
        g.category = cat;
        g.items    = vec;
        g.featured = isFeaturedCat(vec);
        groups_.push_back(std::move(g));
    }
    // Catalog band last.
    for (auto& [cat, vec] : byCategory) {
        if (!isCatalogCat(cat)) continue;
        std::sort(vec.begin(), vec.end(), less);
        AlgorithmGroup g;
        g.category = cat;
        g.items    = vec;
        g.catalog  = true;
        groups_.push_back(std::move(g));
    }
}

bool Application::matchesSearch(const AlgorithmDescriptor& d) const {
    const std::string q = toLowerCopy(searchBuffer_);
    if (q.empty()) return true;
    return containsCI(d.name, q)
        || containsCI(d.category, q)
        || containsCI(d.family, q)
        || containsCI(d.useCase, q)
        || containsCI(d.description, q);
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void Application::run() {
    Logger::info("Entering main loop");
    double prevTime = glfwGetTime();

    while (!window_->shouldClose()) {
        window_->pollEvents();

        const double now = glfwGetTime();
        const float  dt  = static_cast<float>(now - prevTime);
        prevTime = now;

        cameraController_->update(dt);
        advancePlayback(dt);

        imguiLayer_->beginFrame();
        drawUI();
        imguiLayer_->endFrame();

        renderer_->drawFrame([this](VkCommandBuffer cmd, uint32_t /*imageIndex*/) {
            gridRenderer_->recordDraw(cmd, *camera_,
                                      swapchain_->extent(),
                                      renderer_->currentFrameIndex());
            imguiLayer_->recordDrawData(cmd);
        });
    }
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
void Application::drawUI() {
    rebuildAlgorithmGroupsIfNeeded();

    drawMainMenuBar();

    // Default layout (FirstUseEver — user can rearrange/resize freely).
    const float topY     = 28.0f;
    const float browserW = 330.0f;
    const float selectedW = 430.0f;
    const float sceneW   = 320.0f;
    const float windowH  = 740.0f;

    ImGui::SetNextWindowPos (ImVec2(10.0f, topY),                                ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(browserW, windowH),                          ImGuiCond_FirstUseEver);
    drawAlgorithmBrowser();

    ImGui::SetNextWindowPos (ImVec2(10.0f + browserW + 8.0f, topY),              ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(selectedW, windowH),                         ImGuiCond_FirstUseEver);
    drawSelectedAlgorithmPanel();

    ImGui::SetNextWindowPos (ImVec2(10.0f + browserW + selectedW + 16.0f, topY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(sceneW, windowH),                            ImGuiCond_FirstUseEver);
    drawScenePanel();

    if (showDemo_)  ImGui::ShowDemoWindow(&showDemo_);
    if (showAbout_) {
        ImGui::Begin("About", &showAbout_, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("3D Map Generation Algorithm Visualizer");
        ImGui::Separator();
        ImGui::TextWrapped("C++20 / Vulkan / GLFW / GLM / Dear ImGui (docking).");
        ImGui::TextWrapped("Algorithms are grouped by category. Featured entries are real "
                           "step-by-step state machines (DFS Maze, Random Room Dungeon, "
                           "Perlin Heightmap). Planned entries play back pre-computed actions. "
                           "Catalog entries come from mapAlgorithmList.txt and use family-generic "
                           "implementations.");
        ImGui::End();
    }
    if (showShortcuts_) {
        ImGui::Begin("Shortcuts", &showShortcuts_, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextUnformatted("Mouse");
        ImGui::BulletText("MMB drag / Alt+LMB drag : orbit");
        ImGui::BulletText("Shift + MMB             : pan");
        ImGui::BulletText("Ctrl + MMB              : dolly zoom");
        ImGui::BulletText("Wheel                   : zoom");
        ImGui::Separator();
        ImGui::TextUnformatted("Numpad");
        ImGui::BulletText("1 / 3 / 7         : front / right / top");
        ImGui::BulletText("Ctrl+1/3/7        : back / left / bottom");
        ImGui::BulletText("5                 : toggle perspective / orthographic");
        ImGui::BulletText("2 / 4 / 6 / 8     : step orbit");
        ImGui::BulletText("+ / -             : zoom");
        ImGui::BulletText(". / F / Home      : focus on map");
        ImGui::Separator();
        ImGui::TextUnformatted("Movement");
        ImGui::BulletText("W A S D       : move target on horizontal plane");
        ImGui::BulletText("Q / E         : down / up");
        ImGui::BulletText("Shift / Ctrl  : move fast / slow");
        ImGui::End();
    }
}

void Application::drawMainMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("ImGui Demo", nullptr, &showDemo_);
        ImGui::MenuItem("About",      nullptr, &showAbout_);
        ImGui::MenuItem("Shortcuts",  nullptr, &showShortcuts_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Playback")) {
        if (ImGui::MenuItem("Start / Pause", "Space (todo)")) {
            if (playbackRunning_) {
                playbackRunning_ = false;
            } else {
                if (!generator_ || generator_->finished()) resetGenerator();
                playbackRunning_ = true;
            }
        }
        if (ImGui::MenuItem("Step"))  { playbackRunning_ = false; stepGenerator(); }
        if (ImGui::MenuItem("Reset")) resetGenerator();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Camera")) {
        if (ImGui::MenuItem("Focus map (F)"))    focusCameraOnMap();
        ImGui::Separator();
        if (ImGui::MenuItem("Front  (NP 1)"))    camera_->setFrontView();
        if (ImGui::MenuItem("Right  (NP 3)"))    camera_->setRightView();
        if (ImGui::MenuItem("Top    (NP 7)"))    camera_->setTopView();
        if (ImGui::MenuItem("Back   (Ctrl+NP1)")) camera_->setBackView();
        if (ImGui::MenuItem("Left   (Ctrl+NP3)")) camera_->setLeftView();
        if (ImGui::MenuItem("Bottom (Ctrl+NP7)")) camera_->setBottomView();
        ImGui::Separator();
        if (ImGui::MenuItem("Toggle Perspective / Orthographic (NP 5)"))
            camera_->toggleProjection();
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

// ---- Window 1: Algorithm browser ------------------------------------------
void Application::drawAlgorithmBrowser() {
    ImGui::Begin("Algorithms");

    const auto& algorithms = AlgorithmRegistry::instance().algorithms();
    ImGui::Text("Total: %zu  |  Sel: #%d", algorithms.size(), selectedAlgorithmIndex_);
    ImGui::Spacing();

    ImGui::InputTextWithHint("##search", "Search name / category / family",
                             searchBuffer_, sizeof(searchBuffer_));
    if (searchBuffer_[0] != 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) searchBuffer_[0] = 0;
    }

    // Legend.
    drawTag("Featured", bandColor(Band::Featured));   ImGui::SameLine();
    drawTag("Planned",  bandColor(Band::Planned));    ImGui::SameLine();
    drawTag("Catalog",  bandColor(Band::Catalog));
    ImGui::Separator();

    const bool searching = (searchBuffer_[0] != 0);
    int currentSelected = selectedAlgorithmIndex_;

    auto drawItem = [&](const AlgorithmDescriptor* d) {
        // Determine global index for selection.
        int globalIndex = static_cast<int>(d - algorithms.data());
        const Band band = bandFor(d->priority);

        ImGui::PushStyleColor(ImGuiCol_Text, bandColor(band));
        const bool selected = (globalIndex == currentSelected);
        if (ImGui::Selectable(d->name.c_str(), selected,
                              ImGuiSelectableFlags_SpanAllColumns)) {
            selectAlgorithm(globalIndex);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered() && !d->useCase.empty()) {
            ImGui::SetTooltip("%s\n\n%s", d->name.c_str(), d->useCase.c_str());
        }
    };

    ImGui::BeginChild("##algo_scroll");

    if (searching) {
        size_t matches = 0;
        for (const auto& a : algorithms) {
            if (!matchesSearch(a)) continue;
            ImGui::PushID(static_cast<int>(matches));
            drawItem(&a);
            ImGui::SameLine();
            ImGui::TextDisabled("%s", a.category.c_str());
            ImGui::PopID();
            ++matches;
        }
        if (matches == 0) {
            ImGui::TextDisabled("No matches for \"%s\"", searchBuffer_);
        }
    } else {
        for (const auto& group : groups_) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed |
                                       ImGuiTreeNodeFlags_SpanAvailWidth;
            if (group.featured) flags |= ImGuiTreeNodeFlags_DefaultOpen;

            ImGui::PushID(group.category.c_str());
            const std::string header =
                group.category + "  (" + std::to_string(group.items.size()) + ")";
            const bool open = ImGui::TreeNodeEx(header.c_str(), flags);
            if (open) {
                for (const AlgorithmDescriptor* d : group.items) {
                    drawItem(d);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

// ---- Window 2: Selected algorithm ----------------------------------------
void Application::drawSelectedAlgorithmPanel() {
    ImGui::Begin("Selected");

    const auto& algorithms = AlgorithmRegistry::instance().algorithms();
    if (algorithms.empty()) {
        ImGui::TextDisabled("No algorithms registered.");
        ImGui::End();
        return;
    }

    const AlgorithmDescriptor& d =
        algorithms[static_cast<size_t>(std::clamp(selectedAlgorithmIndex_,
                                                  0, static_cast<int>(algorithms.size()) - 1))];

    // ---- Header card ----
    ImGui::PushFont(nullptr);
    ImGui::TextColored(bandColor(bandFor(d.priority)), "%s", d.name.c_str());
    ImGui::PopFont();

    drawTag(bandLabel(bandFor(d.priority)), bandColor(bandFor(d.priority)));
    ImGui::SameLine();
    if (!d.category.empty()) {
        drawTag(d.category.c_str(), ImVec4(0.55f, 0.75f, 1.0f, 1.0f));
        ImGui::SameLine();
    }
    if (!d.family.empty()) {
        drawTag(d.family.c_str(), ImVec4(0.85f, 0.65f, 1.0f, 1.0f));
    }

    ImGui::Spacing();

    if (!d.useCase.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.90f, 0.65f, 1.0f));
        ImGui::TextWrapped("Use case: %s", d.useCase.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::TextWrapped("%s", d.description.c_str());

    ImGui::Separator();
    drawPlaybackSection();

    ImGui::Separator();
    drawAlgorithmSettings(d.id);

    ImGui::Separator();
    ImGui::Checkbox("Show visualization guide", &showVisualizationGuide_);
    if (showVisualizationGuide_) drawVisualizationGuide(d.id);

    ImGui::End();
}

void Application::drawPlaybackSection() {
    const bool isFinished = generator_ && generator_->finished();
    const char* runLabel  = playbackRunning_ ? "Pause" : (isFinished ? "Restart" : "Start");

    const float btnH = 32.0f;
    const float groupW = ImGui::GetContentRegionAvail().x;
    const float btnW = (groupW - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;

    if (ImGui::Button(runLabel, ImVec2(btnW, btnH))) {
        if (playbackRunning_) {
            playbackRunning_ = false;
        } else {
            if (!generator_ || isFinished) resetGenerator();
            playbackRunning_ = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Step", ImVec2(btnW, btnH))) {
        playbackRunning_ = false;
        stepGenerator();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset", ImVec2(btnW, btnH))) {
        resetGenerator();
    }

    ImGui::SliderFloat("Speed", &playbackSpeed_, 1.0f, 240.0f, "%.1f steps/sec");

    const uint64_t steps = generator_ ? generator_->stepsTaken() : 0;
    ImGui::Text("Steps:  %llu", static_cast<unsigned long long>(steps));
    ImGui::TextWrapped("Status: %s", playbackMessage_.c_str());
}

// ---- Window 3: Scene -----------------------------------------------------
void Application::drawScenePanel() {
    ImGui::Begin("Scene");

    if (ImGui::CollapsingHeader("Map", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderInt("Width",  &mapWidth_,  5, 256);
        ImGui::SliderInt("Depth",  &mapDepth_,  5, 256);
        ImGui::SliderInt("Layers", &mapLayers_, 1, 32);
        ImGui::InputInt ("Seed",   &seed_);
        if (ImGui::Button("Apply / Reset", ImVec2(-1.0f, 28.0f))) {
            resetGenerator();
            focusCameraOnMap();
        }
        ImGui::Text("Instances: %u", gridRenderer_->instanceCount());
    }

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Projection: %s", camera_->projectionName());
        ImGui::Text("Distance:   %.2f", camera_->distance());
        const auto& t = camera_->target();
        ImGui::Text("Target:     %.1f, %.1f, %.1f", t.x, t.y, t.z);
        ImGui::Text("Yaw/Pitch:  %.1f / %.1f",
                    camera_->yaw()   * 57.2958f,
                    camera_->pitch() * 57.2958f);

        if (ImGui::Button("Front", ImVec2(75, 0))) camera_->setFrontView();
        ImGui::SameLine();
        if (ImGui::Button("Right", ImVec2(75, 0))) camera_->setRightView();
        ImGui::SameLine();
        if (ImGui::Button("Top",   ImVec2(75, 0))) camera_->setTopView();

        if (ImGui::Button("Back",   ImVec2(75, 0))) camera_->setBackView();
        ImGui::SameLine();
        if (ImGui::Button("Left",   ImVec2(75, 0))) camera_->setLeftView();
        ImGui::SameLine();
        if (ImGui::Button("Bottom", ImVec2(75, 0))) camera_->setBottomView();

        if (ImGui::Button("Toggle Perspective/Ortho", ImVec2(-1.0f, 0))) {
            camera_->toggleProjection();
        }
        if (ImGui::Button("Focus on map (F)", ImVec2(-1.0f, 0))) focusCameraOnMap();

        ImGui::TextDisabled("Open View menu for full shortcut list.");
    }

    if (ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& s = gridRenderer_->settings();
        ImGui::Checkbox("Draw grid", &s.drawGrid);
        ImGui::SliderFloat3("Light dir", &s.lightDir.x, -1.0f, 1.0f);
        ImGui::SliderFloat ("Ambient",   &s.ambient,    0.0f, 1.0f);
        auto& c = renderer_->clearColor();
        ImGui::ColorEdit3  ("Clear color", c.data());
    }

    if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("FPS:        %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Frame time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Algorithm-specific settings (unchanged content, lifted into its own method)
// ---------------------------------------------------------------------------
void Application::drawAlgorithmSettings(const std::string& algorithmId) {
    auto sliderUint = [](const char* label, uint32_t& value, int minValue, int maxValue) {
        int v = static_cast<int>(value);
        const bool changed = ImGui::SliderInt(label, &v, minValue, maxValue);
        if (changed) value = static_cast<uint32_t>(std::clamp(v, minValue, maxValue));
        return changed;
    };

    if (!ImGui::TreeNodeEx("Generator Settings", ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (algorithmId == "dfs_maze") {
        sliderUint("Corridor width", dfsSettings_.corridorWidth, 1, 12);
        sliderUint("Cell spacing", dfsSettings_.cellSpacing,
                   static_cast<int>(dfsSettings_.corridorWidth) + 1, 16);
        const char* startModes[] = { "Random", "Center", "Top-left" };
        int startMode = static_cast<int>(dfsSettings_.startMode);
        if (ImGui::Combo("Start", &startMode, startModes, 3))
            dfsSettings_.startMode = static_cast<uint32_t>(startMode);
        ImGui::Checkbox("Randomize directions", &dfsSettings_.randomizeDirections);
        ImGui::Checkbox("Show backtrack trail", &dfsSettings_.showBacktrackTrail);
        ImGui::Checkbox("Clear overlay on finish", &dfsSettings_.clearVisitOverlayOnFinish);
        ImGui::SliderFloat("Braid chance", &dfsSettings_.braidChance, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Wall height", &dfsSettings_.wallHeight, 0.05f, 8.0f, "%.2f");
        ImGui::SliderFloat("Floor height", &dfsSettings_.floorHeight, 0.01f, 2.0f, "%.2f");
        ImGui::SliderFloat("Visited height", &dfsSettings_.visitedHeight, 0.01f, 3.0f, "%.2f");
        ImGui::SliderFloat("Current height", &dfsSettings_.currentHeight, 0.01f, 4.0f, "%.2f");
    } else if (algorithmId == "randomized_prim_maze" ||
               algorithmId == "randomized_kruskal_maze" ||
               algorithmId == "binary_tree_maze" ||
               algorithmId == "sidewinder_maze" ||
               algorithmId == "wilson_maze" ||
               algorithmId == "aldous_broder_maze" ||
               algorithmId == "hunt_and_kill_maze" ||
               algorithmId == "growing_tree_maze" ||
               algorithmId == "recursive_division_maze") {
        sliderUint("Corridor width", classicMazeSettings_.corridorWidth, 1, 12);
        sliderUint("Cell spacing", classicMazeSettings_.cellSpacing,
                   static_cast<int>(classicMazeSettings_.corridorWidth) + 1, 16);
        sliderUint("Cells per step", classicMazeSettings_.cellsPerStep, 1, 256);
        const char* startModes[] = { "Random", "Center", "Top-left" };
        int startMode = static_cast<int>(classicMazeSettings_.startMode);
        if (ImGui::Combo("Start", &startMode, startModes, 3))
            classicMazeSettings_.startMode = static_cast<uint32_t>(startMode);
        if (algorithmId == "binary_tree_maze" || algorithmId == "sidewinder_maze")
            ImGui::SliderFloat("Horizontal bias", &classicMazeSettings_.horizontalBias, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Loop chance", &classicMazeSettings_.loopChance, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("Show frontier", &classicMazeSettings_.showFrontier);
        ImGui::Checkbox("Randomize edges", &classicMazeSettings_.randomizeEdges);
        ImGui::SliderFloat("Wall height", &classicMazeSettings_.wallHeight, 0.05f, 8.0f, "%.2f");
        ImGui::SliderFloat("Floor height", &classicMazeSettings_.floorHeight, 0.01f, 2.0f, "%.2f");
        ImGui::SliderFloat("Frontier height", &classicMazeSettings_.frontierHeight, 0.01f, 4.0f, "%.2f");
        ImGui::SliderFloat("Current height", &classicMazeSettings_.currentHeight, 0.01f, 4.0f, "%.2f");
    } else if (algorithmId == "random_room_dungeon") {
        sliderUint("Target rooms", roomDungeonSettings_.targetRooms, 1, 128);
        sliderUint("Max attempts", roomDungeonSettings_.maxAttempts,
                   static_cast<int>(roomDungeonSettings_.targetRooms), 4096);
        sliderUint("Min room width", roomDungeonSettings_.minRoomWidth, 2, 64);
        sliderUint("Max room width", roomDungeonSettings_.maxRoomWidth,
                   static_cast<int>(roomDungeonSettings_.minRoomWidth), 96);
        sliderUint("Min room height", roomDungeonSettings_.minRoomHeight, 2, 64);
        sliderUint("Max room height", roomDungeonSettings_.maxRoomHeight,
                   static_cast<int>(roomDungeonSettings_.minRoomHeight), 96);
        sliderUint("Room padding", roomDungeonSettings_.roomPadding, 0, 12);
        sliderUint("Corridor width", roomDungeonSettings_.corridorWidth, 1, 12);
        const char* connectorModes[] = { "Sorted chain", "Nearest chain" };
        int connectorMode = static_cast<int>(roomDungeonSettings_.connectorMode);
        if (ImGui::Combo("Connector mode", &connectorMode, connectorModes, 2))
            roomDungeonSettings_.connectorMode = static_cast<uint32_t>(connectorMode);
        ImGui::Checkbox("Random bend order", &roomDungeonSettings_.randomBendOrder);
        ImGui::Checkbox("Allow overlap", &roomDungeonSettings_.allowRoomOverlap);
        ImGui::Checkbox("Show rejected rooms", &roomDungeonSettings_.showRejectedRooms);
        ImGui::Checkbox("Show accepted preview", &roomDungeonSettings_.showAcceptedPreview);
        ImGui::SliderFloat("Wall height", &roomDungeonSettings_.wallHeight, 0.05f, 8.0f, "%.2f");
        ImGui::SliderFloat("Room height", &roomDungeonSettings_.roomHeight, 0.01f, 3.0f, "%.2f");
        ImGui::SliderFloat("Corridor height", &roomDungeonSettings_.corridorHeight, 0.01f, 3.0f, "%.2f");
        ImGui::SliderFloat("Candidate height", &roomDungeonSettings_.candidateHeight, 0.01f, 4.0f, "%.2f");
        ImGui::SliderFloat("Current height", &roomDungeonSettings_.currentHeight, 0.01f, 4.0f, "%.2f");
    } else if (algorithmId == "bsp_dungeon") {
        sliderUint("Min leaf size", bspDungeonSettings_.minLeafSize, 6, 64);
        sliderUint("Max depth", bspDungeonSettings_.maxDepth, 1, 9);
        sliderUint("Min room size", bspDungeonSettings_.minRoomSize, 3, 32);
        sliderUint("Room padding", bspDungeonSettings_.roomPadding, 1, 12);
        sliderUint("Corridor width", bspDungeonSettings_.corridorWidth, 1, 12);
        sliderUint("Cells per step", bspDungeonSettings_.cellsPerStep, 1, 1024);
        ImGui::SliderFloat("Split jitter", &bspDungeonSettings_.splitJitter, 0.0f, 0.9f, "%.2f");
        ImGui::Checkbox("Show partitions", &bspDungeonSettings_.showPartitions);
        ImGui::Checkbox("Connect siblings", &bspDungeonSettings_.connectSiblings);
        ImGui::SliderFloat("Wall height", &bspDungeonSettings_.wallHeight, 0.05f, 8.0f, "%.2f");
        ImGui::SliderFloat("Room height", &bspDungeonSettings_.roomHeight, 0.01f, 3.0f, "%.2f");
        ImGui::SliderFloat("Corridor height", &bspDungeonSettings_.corridorHeight, 0.01f, 3.0f, "%.2f");
    } else if (algorithmId == "cellular_automata_cave") {
        ImGui::SliderFloat("Initial wall chance", &cellularAutomataSettings_.initialWallChance, 0.0f, 1.0f, "%.2f");
        sliderUint("Iterations", cellularAutomataSettings_.iterations, 0, 20);
        sliderUint("Birth limit", cellularAutomataSettings_.birthLimit, 0, 8);
        sliderUint("Death limit", cellularAutomataSettings_.deathLimit, 0, 8);
        sliderUint("Cells per step", cellularAutomataSettings_.cellsPerStep, 1, 4096);
        ImGui::Checkbox("Edge walls", &cellularAutomataSettings_.edgeWalls);
        ImGui::Checkbox("Show iterations", &cellularAutomataSettings_.showIterations);
        ImGui::SliderFloat("Wall height", &cellularAutomataSettings_.wallHeight, 0.05f, 8.0f, "%.2f");
        ImGui::SliderFloat("Floor height", &cellularAutomataSettings_.floorHeight, 0.01f, 2.0f, "%.2f");
    } else if (algorithmId == "drunkard_walk_cave") {
        sliderUint("Walker count", drunkardWalkSettings_.walkerCount, 1, 32);
        sliderUint("Brush radius", drunkardWalkSettings_.brushRadius, 0, 8);
        sliderUint("Max steps", drunkardWalkSettings_.maxSteps, 100, 50000);
        sliderUint("Cells per step", drunkardWalkSettings_.cellsPerStep, 1, 4096);
        ImGui::SliderFloat("Target fill", &drunkardWalkSettings_.targetFill, 0.01f, 0.95f, "%.2f");
        ImGui::SliderFloat("Turn chance", &drunkardWalkSettings_.turnChance, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("Spawn from center", &drunkardWalkSettings_.spawnFromCenter);
        ImGui::SliderFloat("Wall height", &drunkardWalkSettings_.wallHeight, 0.05f, 8.0f, "%.2f");
        ImGui::SliderFloat("Floor height", &drunkardWalkSettings_.floorHeight, 0.01f, 2.0f, "%.2f");
    } else if (algorithmId == "perlin_noise_heightmap") {
        ImGui::SliderFloat("Base frequency", &perlinSettings_.baseFrequency, 0.001f, 0.25f, "%.3f");
        sliderUint("Octaves", perlinSettings_.octaves, 1, 10);
        ImGui::SliderFloat("Persistence", &perlinSettings_.persistence, 0.05f, 1.0f, "%.2f");
        ImGui::SliderFloat("Lacunarity", &perlinSettings_.lacunarity, 1.01f, 5.0f, "%.2f");
        ImGui::SliderFloat("Height scale", &perlinSettings_.heightScale, 0.1f, 40.0f, "%.2f");
        ImGui::SliderFloat("Base height", &perlinSettings_.baseHeight, 0.01f, 4.0f, "%.2f");
        ImGui::SliderFloat("Height power", &perlinSettings_.heightPower, 0.10f, 5.0f, "%.2f");
        ImGui::SliderFloat("Sea level", &perlinSettings_.seaLevel, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Beach level", &perlinSettings_.beachLevel, perlinSettings_.seaLevel, 1.0f, "%.2f");
        ImGui::SliderFloat("Mountain level", &perlinSettings_.mountainLevel, perlinSettings_.beachLevel, 1.0f, "%.2f");
        ImGui::SliderFloat("Island falloff", &perlinSettings_.islandFalloff, 0.0f, 1.0f, "%.2f");
        sliderUint("Terrace steps", perlinSettings_.terraceSteps, 0, 32);
        sliderUint("Cells per step", perlinSettings_.cellsPerStep, 1, 4096);
        ImGui::Checkbox("Ridged noise", &perlinSettings_.ridged);
        ImGui::Checkbox("Invert height", &perlinSettings_.invert);
        ImGui::Checkbox("Island falloff", &perlinSettings_.useIslandFalloff);
        ImGui::Checkbox("Colorize terrain", &perlinSettings_.colorize);
    } else if (algorithmId == "simplex_noise_heightmap") {
        ImGui::SliderFloat("Base frequency", &simplexSettings_.baseFrequency, 0.001f, 0.25f, "%.3f");
        sliderUint("Octaves", simplexSettings_.octaves, 1, 10);
        ImGui::SliderFloat("Persistence", &simplexSettings_.persistence, 0.05f, 1.0f, "%.2f");
        ImGui::SliderFloat("Lacunarity", &simplexSettings_.lacunarity, 1.01f, 5.0f, "%.2f");
        ImGui::SliderFloat("Height scale", &simplexSettings_.heightScale, 0.1f, 40.0f, "%.2f");
        ImGui::SliderFloat("Base height", &simplexSettings_.baseHeight, 0.01f, 4.0f, "%.2f");
        ImGui::SliderFloat("Sea level", &simplexSettings_.seaLevel, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Mountain level", &simplexSettings_.mountainLevel, simplexSettings_.seaLevel, 1.0f, "%.2f");
        ImGui::SliderFloat("Domain warp", &simplexSettings_.domainWarp, 0.0f, 4.0f, "%.2f");
        sliderUint("Cells per step", simplexSettings_.cellsPerStep, 1, 4096);
        ImGui::Checkbox("Ridged noise", &simplexSettings_.ridged);
        ImGui::Checkbox("Colorize terrain", &simplexSettings_.colorize);
    } else if (algorithmId == "diamond_square_terrain") {
        ImGui::SliderFloat("Roughness", &diamondSquareSettings_.roughness, 0.2f, 0.95f, "%.2f");
        ImGui::SliderFloat("Height scale", &diamondSquareSettings_.heightScale, 0.1f, 40.0f, "%.2f");
        ImGui::SliderFloat("Base height", &diamondSquareSettings_.baseHeight, 0.01f, 4.0f, "%.2f");
        ImGui::SliderFloat("Sea level", &diamondSquareSettings_.seaLevel, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Mountain level", &diamondSquareSettings_.mountainLevel, diamondSquareSettings_.seaLevel, 1.0f, "%.2f");
        sliderUint("Cells per step", diamondSquareSettings_.cellsPerStep, 1, 4096);
        ImGui::Checkbox("Wrap edges", &diamondSquareSettings_.wrapEdges);
        ImGui::Checkbox("Colorize terrain", &diamondSquareSettings_.colorize);
    } else if (algorithmId == "fault_formation_terrain") {
        sliderUint("Iterations", faultFormationSettings_.iterations, 1, 512);
        ImGui::SliderFloat("Displacement", &faultFormationSettings_.displacement, 0.001f, 0.5f, "%.3f");
        ImGui::SliderFloat("Smoothing", &faultFormationSettings_.smoothing, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Height scale", &faultFormationSettings_.heightScale, 0.1f, 40.0f, "%.2f");
        ImGui::SliderFloat("Base height", &faultFormationSettings_.baseHeight, 0.01f, 4.0f, "%.2f");
        ImGui::SliderFloat("Sea level", &faultFormationSettings_.seaLevel, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Mountain level", &faultFormationSettings_.mountainLevel, faultFormationSettings_.seaLevel, 1.0f, "%.2f");
        sliderUint("Cells per step", faultFormationSettings_.cellsPerStep, 1, 4096);
        ImGui::Checkbox("Colorize terrain", &faultFormationSettings_.colorize);
    } else if (algorithmId == "simple_tiled_wfc") {
        sliderUint("Cells per step", wfcSettings_.cellsPerStep, 1, 1024);
        sliderUint("Water weight", wfcSettings_.waterWeight, 1, 32);
        sliderUint("Floor weight", wfcSettings_.floorWeight, 1, 32);
        sliderUint("Wall weight", wfcSettings_.wallWeight, 1, 32);
        sliderUint("Mountain weight", wfcSettings_.mountainWeight, 1, 32);
        ImGui::Checkbox("Allow water near mountain", &wfcSettings_.allowWaterNearMountain);
        ImGui::Checkbox("Prefer connected floors", &wfcSettings_.preferConnectedFloors);
        ImGui::SliderFloat("Water height", &wfcSettings_.waterHeight, 0.01f, 2.0f, "%.2f");
        ImGui::SliderFloat("Floor height", &wfcSettings_.floorHeight, 0.01f, 3.0f, "%.2f");
        ImGui::SliderFloat("Wall height", &wfcSettings_.wallHeight, 0.05f, 8.0f, "%.2f");
        ImGui::SliderFloat("Mountain height", &wfcSettings_.mountainHeight, 0.1f, 12.0f, "%.2f");
    } else if (algorithmId == "eller_maze") {
        sliderUint("Corridor width", ellerSettings_.corridorWidth, 1, 12);
        sliderUint("Cell spacing", ellerSettings_.cellSpacing,
                   static_cast<int>(ellerSettings_.corridorWidth) + 1, 16);
        sliderUint("Cells per step", ellerSettings_.cellsPerStep, 1, 64);
        ImGui::SliderFloat("Horizontal merge chance", &ellerSettings_.horizontalMergeChance, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Vertical carry chance",   &ellerSettings_.verticalCarryChance,   0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Wall height",    &ellerSettings_.wallHeight,    0.05f, 8.0f, "%.2f");
        ImGui::SliderFloat("Floor height",   &ellerSettings_.floorHeight,   0.01f, 2.0f, "%.2f");
        ImGui::SliderFloat("Current height", &ellerSettings_.currentHeight, 0.01f, 4.0f, "%.2f");
    } else if (algorithmId == "voronoi_diagram") {
        sliderUint("Number of seeds", voronoiSettings_.numSeeds, 2, 256);
        sliderUint("Lloyd iterations", voronoiSettings_.lloydIterations, 0, 10);
        sliderUint("Cells per step", voronoiSettings_.cellsPerStep, 1, 4096);
        ImGui::Checkbox("Show seed markers", &voronoiSettings_.showSeeds);
        ImGui::Checkbox("Terrain mode (water / land / mountain)", &voronoiSettings_.terrainMode);
        if (voronoiSettings_.terrainMode) {
            ImGui::SliderFloat("Water fraction",    &voronoiSettings_.waterFraction,    0.0f, 0.7f, "%.2f");
            ImGui::SliderFloat("Mountain fraction", &voronoiSettings_.mountainFraction, 0.0f, 0.7f, "%.2f");
        }
        ImGui::SliderFloat("Floor height",       &voronoiSettings_.floorHeight,       0.01f, 3.0f, "%.2f");
        ImGui::SliderFloat("Water height",       &voronoiSettings_.waterHeight,       0.01f, 2.0f, "%.2f");
        ImGui::SliderFloat("Mountain height",    &voronoiSettings_.mountainHeight,    0.1f,  8.0f, "%.2f");
        ImGui::SliderFloat("Seed marker height", &voronoiSettings_.seedMarkerHeight,  0.1f,  4.0f, "%.2f");
    } else if (algorithmId == "poisson_disk_sampling") {
        ImGui::SliderFloat("Min distance", &poissonSettings_.minDistance, 2.0f, 32.0f, "%.1f");
        sliderUint("Attempts per active", poissonSettings_.kAttempts,   1, 64);
        sliderUint("Brush radius",        poissonSettings_.brushRadius, 0,  6);
        sliderUint("Cells per step",      poissonSettings_.cellsPerStep, 1, 64);
        ImGui::Checkbox("Spawn from centre",   &poissonSettings_.spawnFromCenter);
        ImGui::Checkbox("Paint ground floor",  &poissonSettings_.paintGround);
        ImGui::SliderFloat("Ground height", &poissonSettings_.groundHeight, 0.01f, 1.0f, "%.2f");
        ImGui::SliderFloat("Sample height", &poissonSettings_.sampleHeight, 0.1f,  6.0f, "%.2f");
    } else if (algorithmId.rfind("catalog_", 0) == 0) {
        sliderUint("Cells per step", catalogSettings_.cellsPerStep, 1, 4096);
        sliderUint("Feature size", catalogSettings_.featureSize, 2, 32);
        sliderUint("Room / node count", catalogSettings_.roomCount, 1, 128);
        sliderUint("Iterations", catalogSettings_.iterations, 0, 64);
        sliderUint("Corridor width", catalogSettings_.corridorWidth, 1, 12);
        ImGui::SliderFloat("Density", &catalogSettings_.density, 0.01f, 0.95f, "%.2f");
        ImGui::SliderFloat("Complexity", &catalogSettings_.complexity, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Height scale", &catalogSettings_.heightScale, 0.1f, 40.0f, "%.2f");
        ImGui::SliderFloat("Water level", &catalogSettings_.waterLevel, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("Colorize", &catalogSettings_.colorize);
        ImGui::Checkbox("Use 3D layers when relevant", &catalogSettings_.use3DLayers);
    } else {
        ImGui::TextDisabled("No tunable settings for this algorithm yet.");
    }

    ImGui::TreePop();
}

void Application::drawVisualizationGuide(const std::string& algorithmId) {
    if (!ImGui::TreeNodeEx("Visualization Guide", ImGuiTreeNodeFlags_DefaultOpen)) return;

    auto swatch = [](const char* label, ImVec4 color) {
        ImGui::ColorButton(label, color, ImGuiColorEditFlags_NoTooltip, ImVec2(16.0f, 16.0f));
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
    };

    swatch("Wall / blocked",            ImVec4(0.55f, 0.55f, 0.60f, 1.0f));
    swatch("Room / land / accepted",    ImVec4(0.40f, 0.58f, 0.36f, 1.0f));
    swatch("Corridor / carved",         ImVec4(0.50f, 0.45f, 0.40f, 1.0f));
    swatch("Frontier / candidate",      ImVec4(0.90f, 0.70f, 0.20f, 1.0f));
    swatch("Current / active step",     ImVec4(1.00f, 0.20f, 0.20f, 1.0f));
    swatch("Path / graph edge",         ImVec4(0.95f, 0.95f, 0.25f, 1.0f));
    swatch("Water / lowland",           ImVec4(0.18f, 0.40f, 0.78f, 1.0f));
    swatch("Mountain / highland",       ImVec4(0.65f, 0.65f, 0.65f, 1.0f));

    ImGui::Separator();

    if (algorithmId.find("maze") != std::string::npos ||
        algorithmId.find("wilson") != std::string::npos ||
        algorithmId.find("aldous") != std::string::npos ||
        algorithmId.find("hunt") != std::string::npos ||
        algorithmId.find("growing_tree") != std::string::npos) {
        ImGui::TextWrapped("Maze view: red is the active cell or walker, yellow shows candidates "
                           "or loop-erased walks, brown is committed carving.");
    } else if (algorithmId.find("dungeon") != std::string::npos ||
               algorithmId.find("room") != std::string::npos) {
        ImGui::TextWrapped("Dungeon view: yellow outlines proposed rooms or partitions, green "
                           "rooms are accepted space, brown corridors show connectivity.");
    } else if (algorithmId.find("terrain") != std::string::npos ||
               algorithmId.find("noise") != std::string::npos ||
               algorithmId.find("fault") != std::string::npos ||
               algorithmId.find("diamond") != std::string::npos ||
               algorithmId.find("heightmap") != std::string::npos) {
        ImGui::TextWrapped("Terrain view: height encodes elevation, blue is low/water, green is "
                           "land, gray is high terrain, red marks the active sample pass.");
    } else if (algorithmId.find("wfc") != std::string::npos ||
               algorithmId.find("tile") != std::string::npos) {
        ImGui::TextWrapped("Tile view: yellow cells are being considered, final tile colours "
                           "reflect the local constraint result.");
    } else if (algorithmId.find("graph") != std::string::npos ||
               algorithmId.find("road") != std::string::npos ||
               algorithmId.find("city") != std::string::npos) {
        ImGui::TextWrapped("Graph view: raised green nodes are sites, yellow paths are primary "
                           "links, brown links are secondary connections.");
    } else if (algorithmId.rfind("catalog_", 0) == 0) {
        ImGui::TextWrapped("Catalog view: the selected entry uses a family-generic generator so "
                           "the pattern is playable while it waits for a bespoke implementation.");
    } else {
        ImGui::TextWrapped("Generic view: cells are coloured by type; height encodes either "
                           "terrain elevation or per-state highlighting.");
    }

    ImGui::TreePop();
}

} // namespace mgv
