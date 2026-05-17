#pragma once

#include "algorithms/IMapGenerator.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace mgv {

// Metadata for an algorithm shown in the UI browser.
// `category` is the top-level grouping ("Maze", "Dungeon", "Terrain", ...).
// `family`   is a sub-grouping inside the category ("Spanning Tree", "Cave", ...).
// `useCase`  is a one-liner shown in the selected-algorithm card.
// `priority` controls ordering inside a category (lower shown first).
//   - 0..19  : "Featured" — real step-by-step state machines (Phase 3..5)
//   - 20..99 : "Planned"  — scripted but step-replayed (Future set)
//   - 1000+  : "Catalog"  — generic implementations parsed from mapAlgorithmList.txt
struct AlgorithmDescriptor {
    std::string id;
    std::string name;
    std::string description;
    std::string category;
    std::string family;
    std::string useCase;
    int         priority = 1000;
    std::function<std::unique_ptr<IMapGenerator>()> create;
};

class AlgorithmRegistry {
public:
    static AlgorithmRegistry& instance();

    void registerGenerator(AlgorithmDescriptor descriptor);

    const std::vector<AlgorithmDescriptor>& algorithms() const { return algorithms_; }
    const AlgorithmDescriptor* findById(std::string_view id) const;
    std::unique_ptr<IMapGenerator> create(std::string_view id) const;

private:
    std::vector<AlgorithmDescriptor> algorithms_;
};

void registerBuiltInAlgorithms(AlgorithmRegistry& registry);

} // namespace mgv
