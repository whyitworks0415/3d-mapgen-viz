#include "algorithms/AlgorithmRegistry.h"

#include "algorithms/AldousBroderMazeGenerator.h"
#include "algorithms/BSPDungeonGenerator.h"
#include "algorithms/CatalogAlgorithmGenerators.h"
#include "algorithms/CellularAutomataCaveGenerator.h"
#include "algorithms/DFSMazeGenerator.h"
#include "algorithms/DiamondSquareTerrainGenerator.h"
#include "algorithms/DrunkardWalkCaveGenerator.h"
#include "algorithms/EllerMazeGenerator.h"
#include "algorithms/FaultFormationTerrainGenerator.h"
#include "algorithms/HuntAndKillMazeGenerator.h"
#include "algorithms/KruskalMazeGenerator.h"
#include "algorithms/PerlinNoiseHeightmapGenerator.h"
#include "algorithms/PlannedAlgorithmGenerators.h"
#include "algorithms/PoissonDiskSamplingGenerator.h"
#include "algorithms/PrimMazeGenerator.h"
#include "algorithms/RandomRoomDungeonGenerator.h"
#include "algorithms/VoronoiDiagramGenerator.h"
#include "algorithms/WFCGenerator.h"
#include "algorithms/WilsonMazeGenerator.h"

#include <algorithm>

namespace mgv {

AlgorithmRegistry& AlgorithmRegistry::instance() {
    static AlgorithmRegistry registry;
    return registry;
}

void AlgorithmRegistry::registerGenerator(AlgorithmDescriptor descriptor) {
    auto existing = std::find_if(algorithms_.begin(), algorithms_.end(),
        [&](const AlgorithmDescriptor& candidate) {
            return candidate.id == descriptor.id;
        });

    if (existing != algorithms_.end()) {
        *existing = std::move(descriptor);
        return;
    }

    algorithms_.push_back(std::move(descriptor));
}

const AlgorithmDescriptor* AlgorithmRegistry::findById(std::string_view id) const {
    auto it = std::find_if(algorithms_.begin(), algorithms_.end(),
        [&](const AlgorithmDescriptor& candidate) {
            return candidate.id == id;
        });
    return it == algorithms_.end() ? nullptr : &*it;
}

std::unique_ptr<IMapGenerator> AlgorithmRegistry::create(std::string_view id) const {
    if (const AlgorithmDescriptor* descriptor = findById(id)) {
        return descriptor->create();
    }
    return nullptr;
}

void registerBuiltInAlgorithms(AlgorithmRegistry& registry) {
    static bool registered = false;
    if (registered) return;

    // Featured (true step-by-step state machines).
    registerDFSMazeGenerator(registry);
    registerRandomRoomDungeonGenerator(registry);
    registerPerlinNoiseHeightmapGenerator(registry);
    registerBSPDungeonGenerator(registry);
    registerCellularAutomataCaveGenerator(registry);
    registerWFCGenerator(registry);
    registerEllerMazeGenerator(registry);
    registerVoronoiDiagramGenerator(registry);
    registerPoissonDiskSamplingGenerator(registry);
    registerPrimMazeGenerator(registry);
    registerKruskalMazeGenerator(registry);
    registerWilsonMazeGenerator(registry);
    registerAldousBroderMazeGenerator(registry);
    registerHuntAndKillMazeGenerator(registry);
    registerDrunkardWalkCaveGenerator(registry);
    registerDiamondSquareTerrainGenerator(registry);
    registerFaultFormationTerrainGenerator(registry);

    // Planned (scripted-replay algorithms).
    registerPlannedAlgorithmGenerators(registry);

    // Catalog (family-generic placeholders for the full mapAlgorithmList.txt).
    registerCatalogAlgorithmGenerators(registry);
    registered = true;
}

} // namespace mgv
