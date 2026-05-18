#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace mgv {

class MapData;

struct DFSSettings {
    uint32_t corridorWidth = 1;
    uint32_t cellSpacing = 2;
    uint32_t startMode = 0;          // 0 random, 1 center, 2 top-left
    bool randomizeDirections = true;
    bool showBacktrackTrail = true;
    bool clearVisitOverlayOnFinish = true;
    float braidChance = 0.0f;        // chance to open a dead-end during backtracking
    float wallHeight = 1.0f;
    float floorHeight = 0.08f;
    float visitedHeight = 0.16f;
    float currentHeight = 0.30f;
};

struct RandomRoomDungeonSettings {
    uint32_t targetRooms = 12;
    uint32_t maxAttempts = 144;
    uint32_t minRoomWidth = 4;
    uint32_t maxRoomWidth = 14;
    uint32_t minRoomHeight = 4;
    uint32_t maxRoomHeight = 12;
    uint32_t roomPadding = 1;
    uint32_t corridorWidth = 1;
    uint32_t connectorMode = 0;      // 0 sorted chain, 1 nearest chain
    bool randomBendOrder = true;
    bool allowRoomOverlap = false;
    bool showRejectedRooms = true;
    bool showAcceptedPreview = true;
    float roomHeight = 0.10f;
    float corridorHeight = 0.12f;
    float wallHeight = 1.0f;
    float candidateHeight = 0.24f;
    float currentHeight = 0.32f;
};

struct PerlinNoiseSettings {
    float baseFrequency = 0.055f;
    uint32_t octaves = 5;
    float persistence = 0.50f;
    float lacunarity = 2.0f;
    float heightScale = 8.0f;
    float baseHeight = 0.15f;
    float heightPower = 1.25f;
    float seaLevel = 0.34f;
    float beachLevel = 0.42f;
    float mountainLevel = 0.72f;
    float islandFalloff = 0.15f;
    uint32_t terraceSteps = 0;
    bool ridged = false;
    bool invert = false;
    bool useIslandFalloff = false;
    bool colorize = true;
    uint32_t cellsPerStep = 64;
};

struct ClassicMazeSettings {
    uint32_t corridorWidth = 1;
    uint32_t cellSpacing = 2;
    uint32_t startMode = 0;          // 0 random, 1 center, 2 top-left
    uint32_t cellsPerStep = 1;
    float wallHeight = 1.0f;
    float floorHeight = 0.08f;
    float frontierHeight = 0.24f;
    float currentHeight = 0.32f;
    float horizontalBias = 0.55f;    // Binary Tree / Sidewinder
    float loopChance = 0.0f;
    bool showFrontier = true;
    bool randomizeEdges = true;
};

struct BSPDungeonSettings {
    uint32_t minLeafSize = 10;
    uint32_t maxDepth = 5;
    uint32_t minRoomSize = 4;
    uint32_t roomPadding = 2;
    uint32_t corridorWidth = 1;
    uint32_t cellsPerStep = 8;
    float splitJitter = 0.25f;
    float roomHeight = 0.10f;
    float corridorHeight = 0.12f;
    float wallHeight = 1.0f;
    bool showPartitions = true;
    bool connectSiblings = true;
};

struct CellularAutomataSettings {
    float initialWallChance = 0.45f;
    uint32_t iterations = 5;
    uint32_t birthLimit = 5;
    uint32_t deathLimit = 4;
    uint32_t cellsPerStep = 256;
    float wallHeight = 1.0f;
    float floorHeight = 0.08f;
    bool edgeWalls = true;
    bool showIterations = true;
};

struct DrunkardWalkSettings {
    uint32_t walkerCount = 1;
    uint32_t brushRadius = 1;
    uint32_t maxSteps = 6000;
    uint32_t cellsPerStep = 16;
    float targetFill = 0.42f;
    float turnChance = 0.35f;
    float wallHeight = 1.0f;
    float floorHeight = 0.08f;
    bool spawnFromCenter = true;
};

struct SimplexNoiseSettings {
    float baseFrequency = 0.055f;
    uint32_t octaves = 5;
    float persistence = 0.50f;
    float lacunarity = 2.0f;
    float heightScale = 8.0f;
    float baseHeight = 0.15f;
    float seaLevel = 0.34f;
    float mountainLevel = 0.72f;
    float domainWarp = 0.0f;
    bool ridged = false;
    bool colorize = true;
    uint32_t cellsPerStep = 64;
};

struct DiamondSquareSettings {
    float roughness = 0.55f;
    float heightScale = 9.0f;
    float baseHeight = 0.12f;
    float seaLevel = 0.32f;
    float mountainLevel = 0.72f;
    uint32_t cellsPerStep = 64;
    bool wrapEdges = false;
    bool colorize = true;
};

struct FaultFormationSettings {
    uint32_t iterations = 80;
    float displacement = 0.06f;
    float smoothing = 0.20f;
    float heightScale = 9.0f;
    float baseHeight = 0.12f;
    float seaLevel = 0.30f;
    float mountainLevel = 0.72f;
    uint32_t cellsPerStep = 64;
    bool colorize = true;
};

struct WFCSettings {
    uint32_t cellsPerStep = 1;
    uint32_t waterWeight = 2;
    uint32_t floorWeight = 6;
    uint32_t wallWeight = 3;
    uint32_t mountainWeight = 1;
    float floorHeight = 0.10f;
    float wallHeight = 1.0f;
    float waterHeight = 0.05f;
    float mountainHeight = 2.5f;
    bool allowWaterNearMountain = false;
    bool preferConnectedFloors = true;
};

struct CatalogSettings {
    uint32_t cellsPerStep = 256;
    uint32_t featureSize = 6;
    uint32_t roomCount = 14;
    uint32_t iterations = 5;
    uint32_t corridorWidth = 1;
    float density = 0.45f;
    float complexity = 0.55f;
    float heightScale = 6.0f;
    float waterLevel = 0.32f;
    bool colorize = true;
    bool use3DLayers = true;
};

struct EllerSettings {
    uint32_t corridorWidth = 1;
    uint32_t cellSpacing = 2;
    uint32_t cellsPerStep = 1;
    float horizontalMergeChance = 0.45f;  // chance to merge adjacent sets in a row
    float verticalCarryChance   = 0.45f;  // extra carry-down chance per cell (>= 1 always forced)
    float wallHeight    = 1.0f;
    float floorHeight   = 0.08f;
    float currentHeight = 0.32f;
};

struct VoronoiSettings {
    uint32_t numSeeds          = 32;
    uint32_t lloydIterations   = 0;
    uint32_t cellsPerStep      = 96;
    bool     showSeeds         = true;
    bool     terrainMode       = true;   // small regions = water, large = land/mountain
    float    waterFraction     = 0.35f;
    float    mountainFraction  = 0.20f;
    float    floorHeight       = 0.12f;
    float    waterHeight       = 0.05f;
    float    mountainHeight    = 1.6f;
    float    seedMarkerHeight  = 0.50f;
};

struct PoissonSettings {
    float    minDistance       = 4.0f;
    uint32_t kAttempts         = 30;
    uint32_t brushRadius       = 1;
    uint32_t cellsPerStep      = 1;
    bool     spawnFromCenter   = true;
    bool     paintGround       = true;
    float    groundHeight      = 0.06f;
    float    sampleHeight      = 0.9f;
};

struct LSystemSettings {
    uint32_t preset       = 0;     // 0=Dragon 1=Hilbert 2=KochSquare 3=Plant
    uint32_t iterations   = 6;
    uint32_t cellsPerStep = 32;
    uint32_t stepLength   = 1;
    float    pathHeight    = 0.20f;
    float    branchHeight  = 0.30f;
    float    currentHeight = 0.45f;
    bool     startCentered = true;
    bool     showStack     = true;   // mark push/pop points in Frontier colour
};

struct GeneratorConfig {
    uint32_t width  = 48;
    uint32_t depth  = 48;
    uint32_t layers = 1;
    uint32_t seed   = 1337;
    DFSSettings dfs;
    RandomRoomDungeonSettings roomDungeon;
    PerlinNoiseSettings perlin;
    ClassicMazeSettings classicMaze;
    BSPDungeonSettings bspDungeon;
    CellularAutomataSettings cellularAutomata;
    DrunkardWalkSettings drunkardWalk;
    SimplexNoiseSettings simplex;
    DiamondSquareSettings diamondSquare;
    FaultFormationSettings faultFormation;
    WFCSettings wfc;
    CatalogSettings catalog;
    EllerSettings    eller;
    VoronoiSettings  voronoi;
    PoissonSettings  poisson;
    LSystemSettings  lsystem;
};

struct GeneratorStep {
    bool        mapChanged = false;
    bool        completed  = false;
    std::string message;
};

class IMapGenerator {
public:
    virtual ~IMapGenerator() = default;

    virtual std::string_view id() const = 0;
    virtual std::string_view name() const = 0;
    virtual std::string_view description() const = 0;

    virtual void reset(MapData& map, const GeneratorConfig& config) = 0;
    virtual GeneratorStep step(MapData& map) = 0;

    virtual bool finished() const = 0;
    virtual uint64_t stepsTaken() const = 0;
    virtual std::string_view status() const = 0;
};

} // namespace mgv
