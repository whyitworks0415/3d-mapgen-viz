# 3D Map Generation Algorithm Visualizer

C++20 / Vulkan / GLFW / Dear ImGui desktop app that visualises map-generation
algorithms in 3D with step-by-step playback. Blender-style camera, instanced
cube rendering, and a plugin-style `IMapGenerator` interface that already
hosts 17 real algorithms plus a 388-entry catalog browser.

## Highlights

- **17 true step-by-step state machines** — each step advances one algorithmic
  decision so you can watch frontier sets grow, walks erase loops, BSP leaves
  split, fault lines tilt the heightmap, and WFC tiles collapse one at a time.
- **5 scripted-replay generators** for cases where pre-computation + cell
  playback works just as well visually.
- **388-entry catalog browser** parsed from `mapAlgorithmList.txt`, grouped
  by 20 categories. Items already implemented as real generators are skipped
  from the catalog to avoid duplicates.
- **Blender-style camera**: MMB orbit, Shift+MMB pan, Ctrl+MMB dolly, wheel
  zoom, numpad 1/3/7 ± Ctrl for the six views, numpad 5 toggles
  perspective/orthographic, numpad 2/4/6/8 step-orbit, numpad +/- zoom,
  WASD+QE for target movement, F/Home/Numpad-decimal to focus on the map.
- **Vulkan 1.2** stack: RAII wrappers around instance/device/swapchain/render
  pass, validation layers in debug, depth buffer, instanced cube
  `GridRenderer`, ImGui inside the same render pass.
- **Dockable ImGui UI** with three panels: an algorithm browser with search +
  category groups, a selected-algorithm card with playback controls and
  per-algorithm settings, and a scene panel for map size / camera / render /
  stats.

## Algorithm coverage

### Featured (real step-by-step state machines)

| Category | Algorithm |
|---|---|
| Maze | **DFS / Recursive Backtracker**, **Eller's**, **Prim**, **Kruskal**, **Wilson**, **Aldous-Broder**, **Hunt-and-Kill** |
| Dungeon | **Random Room**, **BSP** |
| Cave | **Cellular Automata**, **Drunkard's Walk** |
| Terrain | **Perlin Noise Heightmap**, **Diamond-Square**, **Fault Formation** |
| Graph | **Voronoi Diagram** (+ optional Lloyd relaxation + terrain classification) |
| Sampling | **Poisson Disk Sampling** (Bridson's algorithm) |
| Tile / Constraint | **Simple Tiled WFC** |

### Scripted-replay (algorithm logic precomputed, played back cell-by-cell)

Binary Tree, Sidewinder, Recursive Division, Growing Tree, Simplex Noise.

### Catalog

Every other entry in `mapAlgorithmList.txt` (20 categories, ~370 unique
items after dedup). Each one routes to a family-generic generator
(Maze / Dungeon / Terrain / Tile / Cellular / Graph / Noise / Sampling /
Partition / City / World / Road / Grammar / Agent / Optimization / AIML /
Voxel / Planet / Placement / Hybrid). These are placeholders so the design
space is browsable; replacing one with a bespoke implementation is the
standard contribution pattern.

## Architecture

```
src/
├── app/         Application — wires the stack and drives main loop / UI
├── core/        Window, Camera, CameraController, Logger, VulkanCheck
├── map/         Cell, MapData (2D/3D grid container with forEachNonEmpty)
├── renderer/    VulkanContext, Swapchain, Renderer, GridRenderer, depth utils
├── ui/          ImGuiLayer (Vulkan + GLFW backends)
└── algorithms/  IMapGenerator interface + concrete generators + AlgorithmRegistry
shaders/         GLSL → SPIR-V at build time (glslangValidator)
```

Key contracts:

```cpp
class IMapGenerator {
public:
    virtual ~IMapGenerator() = default;
    virtual std::string_view id() const = 0;
    virtual std::string_view name() const = 0;
    virtual std::string_view description() const = 0;

    virtual void  reset(MapData& map, const GeneratorConfig& config) = 0;
    virtual GeneratorStep step(MapData& map) = 0;

    virtual bool finished() const = 0;
    virtual uint64_t stepsTaken() const = 0;
    virtual std::string_view status() const = 0;
};
```

A new generator is one class + one `register<X>(AlgorithmRegistry&)` free
function that fills in the `AlgorithmDescriptor` metadata (id, name,
category, family, useCase, priority, create) — then add the file to
`CMakeLists.txt`'s source list and call the register function from
`AlgorithmRegistry::registerBuiltInAlgorithms`.

## Build

### Prerequisites

- Vulkan SDK (LunarG) installed, with `VULKAN_SDK` env var set so
  `glslangValidator` resolves. The validation layers ship with the SDK.
- CMake ≥ 3.20
- Windows: Visual Studio 2022 (MSVC) or Ninja from a Developer Command Prompt
- Linux / macOS: Clang or GCC with C++20 + Vulkan loader + GLFW deps available

GLFW 3.4, GLM 1.0.1 and Dear ImGui v1.91.5-docking are fetched automatically
via `FetchContent` on first configure.

### Windows

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\MapGenViz.exe
```

Or with Ninja from a Developer Command Prompt:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
.\build\MapGenViz.exe
```

The Visual Studio debugger launch directory is set to the CMake binary
directory so `./shaders/grid.vert.spv` resolves correctly. `MGV_SHADER_DIR`
is also baked in as a compile-time fallback if you run the binary from a
different CWD.

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/MapGenViz
```

## Controls

| Action | Mouse / Key |
|---|---|
| Orbit | MMB drag — or Alt+LMB drag |
| Pan | Shift + MMB drag |
| Dolly zoom | Ctrl + MMB drag |
| Zoom | Wheel — or Numpad +/- |
| Front / Right / Top | Numpad 1 / 3 / 7 |
| Back / Left / Bottom | Ctrl + Numpad 1 / 3 / 7 |
| Toggle perspective / orthographic | Numpad 5 |
| Step orbit (15°) | Numpad 4 / 6 / 8 / 2 |
| Move target (camera-local) | W A S D, Q (down) / E (up) |
| Modifiers while moving | Shift = fast, Ctrl = slow |
| Focus on map | F — or Home — or Numpad . |

Playback (mouse on Selected panel or `Playback` menu): Start / Pause / Step /
Reset and a speed slider in steps-per-second.

## Cell types

`enum class CellType`: Empty, Wall, Floor, Room, Corridor, Water, Mountain,
**Visited**, **Frontier**, **Current**, **Path**. The last four are reserved
for algorithm-state visualisation; the visualization guide panel shows the
colour mapping per algorithm family.

## Repo layout

```
.
├── CMakeLists.txt
├── README.md                          ← this file
├── mapAlgorithmList.txt               ← catalog source (20 categories, 388 items)
├── algorithmImplementationChecklist.md
├── shaders/
│   ├── grid.vert
│   └── grid.frag
└── src/
    ├── algorithms/                    ← IMapGenerator + every generator
    ├── app/Application.{h,cpp}
    ├── core/{Window,Camera,CameraController,Logger,VulkanCheck}.{h,cpp}
    ├── map/{Cell.h, MapData.{h,cpp}}
    ├── renderer/{VulkanContext,Swapchain,Renderer,GridRenderer,VulkanUtils}.{h,cpp}
    └── ui/ImGuiLayer.{h,cpp}
```

## Roadmap

- Promote the remaining 5 scripted-replay generators to true state machines
  where the extra visualisation is interesting (Growing Tree is the next
  best candidate).
- Replace catalog placeholders with bespoke implementations a few at a time:
  L-system, hexagonal-grid mazes, Eller-on-hex, weighted-WFC, Wave Function
  Collapse with neighbour-frequency learning.
- Optional render polish: wireframe toggle (needs `fillModeNonSolid`),
  contour lines, isobath shading for terrain, animated water tiles.
- Save / load `GeneratorConfig` as JSON.
- Headless mode for non-interactive batch generation + screenshots.

Contributions welcome — copy any of the existing
`src/algorithms/*Generator.{h,cpp}` pairs as a template, fill in `reset()` /
`step()` for your algorithm, register it, add the file to `CMakeLists.txt`,
and you're in.
