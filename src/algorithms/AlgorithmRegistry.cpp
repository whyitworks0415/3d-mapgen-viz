#include "algorithms/AlgorithmRegistry.h"

#include "algorithms/CatalogAlgorithmGenerators.h"
#include "algorithms/DFSMazeGenerator.h"
#include "algorithms/PerlinNoiseHeightmapGenerator.h"
#include "algorithms/PlannedAlgorithmGenerators.h"
#include "algorithms/RandomRoomDungeonGenerator.h"

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

    registerDFSMazeGenerator(registry);
    registerPlannedAlgorithmGenerators(registry);
    registerRandomRoomDungeonGenerator(registry);
    registerPerlinNoiseHeightmapGenerator(registry);
    registerCatalogAlgorithmGenerators(registry);
    registered = true;
}

} // namespace mgv
