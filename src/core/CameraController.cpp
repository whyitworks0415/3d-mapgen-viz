#include "core/CameraController.h"

#include "core/Camera.h"
#include "core/Window.h"
#include "map/MapData.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace mgv {

CameraController* CameraController::s_instance = nullptr;

CameraController::CameraController(Camera& camera, Window& window)
    : camera_(camera), window_(window) {
    if (s_instance) {
        throw std::runtime_error("Only one CameraController instance is supported");
    }
    s_instance = this;

    // Chain after ImGui's callbacks so ImGui still sees every event but we
    // can decide whether to act on it (suppressed when ImGui WantsCapture).
    prevScroll_ = glfwSetScrollCallback(window_.handle(), &scrollCallback);
    prevKey_    = glfwSetKeyCallback   (window_.handle(), &keyCallback);
}

CameraController::~CameraController() {
    s_instance = nullptr;
    // We intentionally don't restore callbacks: ImGui's own shutdown does
    // that for us when ImGuiLayer is destroyed next.
}

void CameraController::scrollCallback(GLFWwindow* w, double x, double y) {
    if (s_instance && s_instance->prevScroll_) s_instance->prevScroll_(w, x, y);
    if (s_instance) s_instance->onScroll(x, y);
}

void CameraController::keyCallback(GLFWwindow* w, int key, int sc, int action, int mods) {
    if (s_instance && s_instance->prevKey_) s_instance->prevKey_(w, key, sc, action, mods);
    if (s_instance) s_instance->onKey(key, sc, action, mods);
}

void CameraController::onScroll(double /*dx*/, double dy) {
    if (ImGui::GetIO().WantCaptureMouse) return;
    scrollAccum_ += dy;
}

void CameraController::onKey(int key, int /*sc*/, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    constexpr float kStepOrbit = 0.26179939f; // 15 deg
    const bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;

    switch (key) {
        case GLFW_KEY_KP_1: ctrl ? camera_.setBackView()   : camera_.setFrontView(); break;
        case GLFW_KEY_KP_3: ctrl ? camera_.setLeftView()   : camera_.setRightView(); break;
        case GLFW_KEY_KP_7: ctrl ? camera_.setBottomView() : camera_.setTopView();   break;
        case GLFW_KEY_KP_5: camera_.toggleProjection(); break;
        case GLFW_KEY_KP_4: camera_.orbit( kStepOrbit, 0.0f); break;
        case GLFW_KEY_KP_6: camera_.orbit(-kStepOrbit, 0.0f); break;
        case GLFW_KEY_KP_8: camera_.orbit(0.0f,  kStepOrbit); break;
        case GLFW_KEY_KP_2: camera_.orbit(0.0f, -kStepOrbit); break;
        case GLFW_KEY_KP_ADD:      camera_.zoom( 2.0f); break;
        case GLFW_KEY_KP_SUBTRACT: camera_.zoom(-2.0f); break;
        case GLFW_KEY_F:
        case GLFW_KEY_HOME:
        case GLFW_KEY_KP_DECIMAL:
            focusMap();
            break;
        default: break;
    }
}

void CameraController::focusMap() {
    if (!map_) return;

    glm::vec3 center(map_->width()  * 0.5f,
                     map_->depth()  * 0.5f,
                     map_->layers() * 0.5f);
    float radius = 0.65f * std::max({
        static_cast<float>(map_->width()),
        static_cast<float>(map_->depth()),
        static_cast<float>(map_->layers()) });
    camera_.focusOn(center, radius);
}

void CameraController::update(float dt) {
    GLFWwindow* w = window_.handle();

    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(w, &mx, &my);
    glm::dvec2 cur(mx, my);

    if (firstFrame_) {
        lastCursor_ = cur;
        firstFrame_ = false;
    }
    glm::dvec2 delta = cur - lastCursor_;
    lastCursor_ = cur;

    const bool shift = glfwGetKey(w, GLFW_KEY_LEFT_SHIFT)  == GLFW_PRESS
                    || glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    const bool ctrl  = glfwGetKey(w, GLFW_KEY_LEFT_CONTROL)  == GLFW_PRESS
                    || glfwGetKey(w, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    const bool alt   = glfwGetKey(w, GLFW_KEY_LEFT_ALT)  == GLFW_PRESS
                    || glfwGetKey(w, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;

    const bool mmbDrag = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
    const bool altLmbDrag = alt &&
        glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    if (!ImGui::GetIO().WantCaptureMouse && (mmbDrag || altLmbDrag)) {
        if (shift) {
            camera_.pan(static_cast<float>(delta.x),
                        static_cast<float>(delta.y));
        } else if (ctrl) {
            constexpr float kDollyGain = 0.03f;
            camera_.zoom(-static_cast<float>(delta.y) * kDollyGain);
        } else {
            constexpr float kOrbitGain = 0.005f;
            camera_.orbit(-static_cast<float>(delta.x) * kOrbitGain,
                          -static_cast<float>(delta.y) * kOrbitGain);
        }
    }

    if (!ImGui::GetIO().WantCaptureKeyboard) {
        float right = 0.0f;
        float forward = 0.0f;
        float up = 0.0f;

        if (glfwGetKey(w, GLFW_KEY_D) == GLFW_PRESS) right += 1.0f;
        if (glfwGetKey(w, GLFW_KEY_A) == GLFW_PRESS) right -= 1.0f;
        if (glfwGetKey(w, GLFW_KEY_W) == GLFW_PRESS) forward += 1.0f;
        if (glfwGetKey(w, GLFW_KEY_S) == GLFW_PRESS) forward -= 1.0f;
        if (glfwGetKey(w, GLFW_KEY_E) == GLFW_PRESS) up += 1.0f;
        if (glfwGetKey(w, GLFW_KEY_Q) == GLFW_PRESS) up -= 1.0f;

        glm::vec3 movement(right, forward, up);
        if (glm::length(movement) > 1e-4f) {
            movement = glm::normalize(movement);
            float speed = baseMoveSpeed_ + camera_.distance() * 0.35f;
            if (shift) speed *= 3.0f;
            if (ctrl)  speed *= 0.25f;

            camera_.moveLocal(movement.x * speed * dt,
                              movement.y * speed * dt,
                              movement.z * speed * dt);
        }
    }

    if (std::abs(scrollAccum_) > 1e-4) {
        camera_.zoom(static_cast<float>(scrollAccum_));
        scrollAccum_ = 0.0;
    }

    int fbW = 0, fbH = 0;
    window_.getFramebufferSize(fbW, fbH);
    if (fbW > 0 && fbH > 0) {
        camera_.setAspect(static_cast<float>(fbW) / static_cast<float>(fbH));
        camera_.setViewportSize(static_cast<float>(fbW), static_cast<float>(fbH));
    }
}

} // namespace mgv
