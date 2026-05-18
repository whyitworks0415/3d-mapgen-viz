#pragma once

#include "algorithms/IMapGenerator.h"

#include <memory>
#include <string>
#include <vector>

namespace mgv {

class Window;
class VulkanContext;
class Swapchain;
class Renderer;
class ImGuiLayer;
class Camera;
class CameraController;
class MapData;
class GridRenderer;
struct AlgorithmDescriptor;

// Top-level owner. Builds the stack in dependency order and tears it down in
// reverse via member-declaration order.
class Application {
public:
    Application();
    ~Application();

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    // ---- UI ----
    void drawUI();
    void drawMainMenuBar();
    void drawAlgorithmBrowser();
    void drawSelectedAlgorithmPanel();
    void drawScenePanel();
    void drawPlaybackSection();
    void drawAlgorithmSettings(const std::string& algorithmId);
    void drawVisualizationGuide(const std::string& algorithmId);

    // ---- Input ----
    void handleShortcuts();
    void togglePlayback();

    // ---- Generator plumbing ----
    GeneratorConfig currentGeneratorConfig() const;
    void resetGenerator();
    void stepGenerator();
    void advancePlayback(float dt);
    void uploadMapToRenderer();
    void focusCameraOnMap();

    // ---- Browser support ----
    struct AlgorithmGroup {
        std::string category;
        std::vector<const AlgorithmDescriptor*> items;
        bool featured = false;
        bool catalog  = false;
    };
    void rebuildAlgorithmGroupsIfNeeded();
    bool matchesSearch(const AlgorithmDescriptor& descriptor) const;
    void selectAlgorithm(int globalIndex);

    // ---- Owned objects ----
    std::unique_ptr<Window>           window_;
    std::unique_ptr<VulkanContext>    context_;
    std::unique_ptr<Swapchain>        swapchain_;
    std::unique_ptr<Renderer>         renderer_;
    std::unique_ptr<ImGuiLayer>       imguiLayer_;

    std::unique_ptr<MapData>          map_;
    std::unique_ptr<IMapGenerator>    generator_;
    std::unique_ptr<Camera>           camera_;
    std::unique_ptr<GridRenderer>     gridRenderer_;
    std::unique_ptr<CameraController> cameraController_;

    // ---- UI state ----
    int   mapWidth_     = 48;
    int   mapDepth_     = 48;
    int   mapLayers_    = 1;
    int   seed_         = 1337;
    int   selectedAlgorithmIndex_ = 0;

    DFSSettings              dfsSettings_;
    RandomRoomDungeonSettings roomDungeonSettings_;
    PerlinNoiseSettings      perlinSettings_;
    ClassicMazeSettings      classicMazeSettings_;
    BSPDungeonSettings       bspDungeonSettings_;
    CellularAutomataSettings cellularAutomataSettings_;
    DrunkardWalkSettings     drunkardWalkSettings_;
    SimplexNoiseSettings     simplexSettings_;
    DiamondSquareSettings    diamondSquareSettings_;
    FaultFormationSettings   faultFormationSettings_;
    WFCSettings              wfcSettings_;
    CatalogSettings          catalogSettings_;
    EllerSettings            ellerSettings_;
    VoronoiSettings          voronoiSettings_;
    PoissonSettings          poissonSettings_;
    LSystemSettings          lsystemSettings_;

    bool        playbackRunning_     = false;
    float       playbackSpeed_       = 20.0f;
    float       playbackAccumulator_ = 0.0f;
    std::string playbackMessage_     = "Ready";
    bool        showVisualizationGuide_ = true;
    bool        showDemo_  = false;
    bool        showAbout_ = false;
    bool        showShortcuts_ = false;

    // Playback shortcut edge-detection state.
    bool prevSpace_         = false;
    bool prevR_             = false;
    bool prevN_             = false;
    bool prevRightArrow_    = false;
    bool prevLeftBracket_   = false;
    bool prevRightBracket_  = false;

    // Browser
    std::vector<AlgorithmGroup> groups_;
    size_t                      groupsRegistryRevision_ = 0;
    char                        searchBuffer_[128] = {};
};

} // namespace mgv
