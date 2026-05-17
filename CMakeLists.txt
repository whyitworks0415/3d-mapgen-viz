cmake_minimum_required(VERSION 3.20)
project(MapGenViz LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Project-wide settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ---------------------------------------------------------------------------
# Vulkan (system SDK)
# ---------------------------------------------------------------------------
find_package(Vulkan REQUIRED)

# glslangValidator ships with the Vulkan SDK; we compile shaders at build time.
find_program(GLSLANG_VALIDATOR
    NAMES glslangValidator glslang
    HINTS ENV VULKAN_SDK
    PATH_SUFFIXES Bin Bin32 bin)
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR
        "glslangValidator not found. Install the Vulkan SDK and ensure VULKAN_SDK is set.")
endif()

# ---------------------------------------------------------------------------
# External deps via FetchContent
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# GLFW
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)

# GLM
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE)

# Dear ImGui (docking branch)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5-docking
    GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(glfw glm imgui)

# ImGui has no native CMakeLists — build it here as a static lib.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
set(MAPGENVIZ_SOURCES
    src/main.cpp
    src/algorithms/AlgorithmRegistry.cpp
    src/algorithms/BSPDungeonGenerator.cpp
    src/algorithms/CatalogAlgorithmGenerators.cpp
    src/algorithms/CellularAutomataCaveGenerator.cpp
    src/algorithms/DFSMazeGenerator.cpp
    src/algorithms/EllerMazeGenerator.cpp
    src/algorithms/PerlinNoiseHeightmapGenerator.cpp
    src/algorithms/PlannedAlgorithmGenerators.cpp
    src/algorithms/PoissonDiskSamplingGenerator.cpp
    src/algorithms/RandomRoomDungeonGenerator.cpp
    src/algorithms/VoronoiDiagramGenerator.cpp
    src/algorithms/WFCGenerator.cpp
    src/app/Application.cpp
    src/core/Window.cpp
    src/core/Camera.cpp
    src/core/CameraController.cpp
    src/map/MapData.cpp
    src/renderer/VulkanContext.cpp
    src/renderer/VulkanUtils.cpp
    src/renderer/Swapchain.cpp
    src/renderer/Renderer.cpp
    src/renderer/GridRenderer.cpp
    src/ui/ImGuiLayer.cpp
)

add_executable(MapGenViz ${MAPGENVIZ_SOURCES})

target_include_directories(MapGenViz PRIVATE src)

target_link_libraries(MapGenViz PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
    imgui
)

target_compile_definitions(MapGenViz PRIVATE
    $<$<CONFIG:Debug>:MGV_DEBUG=1>
    GLFW_INCLUDE_NONE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    MGV_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
    MGV_ALGORITHM_LIST_FILE="${CMAKE_SOURCE_DIR}/mapAlgorithmList.txt"
)

target_compile_options(MapGenViz PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /Zc:preprocessor>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

# ---------------------------------------------------------------------------
# Shaders — compile each GLSL → SPIR-V at build time.
# ---------------------------------------------------------------------------
set(SHADER_BIN_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BIN_DIR})

set(SHADER_SOURCES
    ${CMAKE_SOURCE_DIR}/shaders/grid.vert
    ${CMAKE_SOURCE_DIR}/shaders/grid.frag
)

set(SHADER_OUTPUTS "")
foreach(SHADER IN LISTS SHADER_SOURCES)
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SHADER_OUT ${SHADER_BIN_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT  ${SHADER_OUT}
        COMMAND ${GLSLANG_VALIDATOR} -V ${SHADER} -o ${SHADER_OUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER_NAME} -> ${SHADER_OUT}"
        VERBATIM
    )
    list(APPEND SHADER_OUTPUTS ${SHADER_OUT})
endforeach()

add_custom_target(MapGenVizShaders DEPENDS ${SHADER_OUTPUTS})
add_dependencies(MapGenViz MapGenVizShaders)

# Run from the build directory so ./shaders/*.spv resolves naturally.
if(MSVC)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                 PROPERTY VS_STARTUP_PROJECT MapGenViz)
    set_target_properties(MapGenViz PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
