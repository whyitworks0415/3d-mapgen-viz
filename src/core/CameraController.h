#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

// GLFW callback typedefs.
using GLFWscrollfun = void (*)(GLFWwindow*, double, double);
using GLFWkeyfun    = void (*)(GLFWwindow*, int, int, int, int);

namespace mgv {

class Camera;
class Window;
class MapData;

// Bridges GLFW input → Camera. Handles:
//   - MMB drag      → orbit
//   - Shift + MMB   → pan
//   - Wheel         → zoom
//   - Numpad 1/3/7  → front / right / top
//   - Numpad 5      → toggle perspective / orthographic
//   - F             → focus on map
//
// ImGui input is respected: while ImGui has hover/keyboard focus, viewport
// input is suppressed.
//
// Phase 2 constraint: only one CameraController instance is supported, since
// the GLFW callbacks dispatch through a static pointer. Lift this when we
// need multi-viewport input.
class CameraController {
public:
    CameraController(Camera& camera, Window& window);
    ~CameraController();

    CameraController(const CameraController&)            = delete;
    CameraController& operator=(const CameraController&) = delete;

    void setMap(const MapData* map) { map_ = map; }
    void update(float dt);

private:
    static CameraController* s_instance;

    static void scrollCallback(GLFWwindow*, double, double);
    static void keyCallback   (GLFWwindow*, int, int, int, int);

    void onScroll(double dx, double dy);
    void onKey   (int key, int scancode, int action, int mods);
    void focusMap();

    Camera&        camera_;
    Window&        window_;
    const MapData* map_ = nullptr;

    glm::dvec2 lastCursor_ { 0.0, 0.0 };
    bool       firstFrame_ = true;
    double     scrollAccum_ = 0.0;
    float      baseMoveSpeed_ = 12.0f;

    // Chain pointers — the callbacks installed by ImGui's GLFW backend.
    GLFWscrollfun prevScroll_ = nullptr;
    GLFWkeyfun    prevKey_    = nullptr;
};

} // namespace mgv
