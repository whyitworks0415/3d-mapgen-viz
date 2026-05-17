#include "core/Camera.h"

#define GLM_FORCE_RADIANS
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace mgv {

Camera::Camera() = default;

glm::vec3 Camera::position() const {
    const float cp = std::cos(pitch_);
    const float sp = std::sin(pitch_);
    const float cy = std::cos(yaw_);
    const float sy = std::sin(yaw_);
    return target_ + distance_ * glm::vec3(cp * cy, cp * sy, sp);
}

glm::vec3 Camera::upReference() const {
    // Near the zenith / nadir the world-Z up vector is parallel to the view
    // direction, so glm::lookAt would degenerate. Snap to a stable reference
    // so top/bottom presets behave like Blender (X right, Y up at top view).
    if (pitch_ >  1.4835f) return glm::vec3( 0.0f,  1.0f, 0.0f);   // top
    if (pitch_ < -1.4835f) return glm::vec3( 0.0f, -1.0f, 0.0f);   // bottom
    return glm::vec3(0.0f, 0.0f, 1.0f);
}

glm::vec3 Camera::forwardVec() const {
    return glm::normalize(target_ - position());
}

glm::vec3 Camera::rightVec() const {
    glm::vec3 r = glm::cross(forwardVec(), upReference());
    float len = glm::length(r);
    return (len > 1e-6f) ? r / len : glm::vec3(1, 0, 0);
}

glm::vec3 Camera::upVec() const {
    return glm::normalize(glm::cross(rightVec(), forwardVec()));
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position(), target_, upReference());
}

glm::mat4 Camera::projectionMatrix() const {
    glm::mat4 p;
    if (proj_ == Projection::Perspective) {
        p = glm::perspective(fovY_, aspect_, near_, far_);
    } else {
        const float halfH = orthoSize_;
        const float halfW = orthoSize_ * aspect_;
        p = glm::ortho(-halfW, halfW, -halfH, halfH, -2000.0f, 2000.0f);
    }
    // Vulkan clip space has Y pointing down vs. OpenGL — flip it once here so
    // shaders can stay GL-style.
    p[1][1] *= -1.0f;
    return p;
}

void Camera::orbit(float dYaw, float dPitch) {
    yaw_   += dYaw;
    pitch_  = std::clamp(pitch_ + dPitch, -kPitchLimit, kPitchLimit);
}

void Camera::pan(float dxPx, float dyPx) {
    // Convert pixel deltas to world units so panning feels right at any zoom.
    float scale;
    if (proj_ == Projection::Perspective) {
        scale = distance_ * std::tan(fovY_ * 0.5f) * 2.0f / std::max(viewportH_, 1.0f);
    } else {
        scale = orthoSize_ * 2.0f / std::max(viewportH_, 1.0f);
    }
    // Grab-the-scene convention: dragging right shifts the scene right
    // (camera moves left in world); dragging down shifts the scene down.
    target_ -= rightVec() * (dxPx * scale);
    target_ += upVec()    * (dyPx * scale);
}

void Camera::zoom(float wheelTicks) {
    constexpr float kZoomBase = 0.9f;
    distance_ *= std::pow(kZoomBase, wheelTicks);
    distance_  = std::clamp(distance_, 0.1f, 10000.0f);
    if (proj_ == Projection::Orthographic) {
        orthoSize_ = distance_ * 0.5f;
    }
}

void Camera::moveLocal(float right, float forward, float up) {
    const glm::vec3 delta = rightVec() * right +
                            forwardVec() * forward +
                            glm::vec3(0.0f, 0.0f, 1.0f) * up;
    target_ += delta;
}

void Camera::setFrontView() {
    yaw_   = -1.57079633f;     // -90 deg → eye at -Y
    pitch_ =  0.0f;
}

void Camera::setBackView() {
    yaw_   =  1.57079633f;     // eye at +Y
    pitch_ =  0.0f;
}

void Camera::setRightView() {
    yaw_   =  0.0f;            // eye at +X
    pitch_ =  0.0f;
}

void Camera::setLeftView() {
    yaw_   =  3.14159265f;     // eye at -X
    pitch_ =  0.0f;
}

void Camera::setTopView() {
    yaw_   = -1.57079633f;     // chosen so orbit-out is consistent
    pitch_ =  kPitchLimit;
}

void Camera::setBottomView() {
    yaw_   = -1.57079633f;
    pitch_ = -kPitchLimit;
}

void Camera::toggleProjection() {
    proj_ = (proj_ == Projection::Perspective)
              ? Projection::Orthographic : Projection::Perspective;
    if (proj_ == Projection::Orthographic) orthoSize_ = distance_ * 0.5f;
}

void Camera::focusOn(glm::vec3 center, float radius) {
    target_ = center;
    radius  = std::max(radius, 0.1f);
    if (proj_ == Projection::Perspective) {
        distance_ = radius / std::tan(fovY_ * 0.5f);
    } else {
        distance_ = radius * 2.0f;
        orthoSize_ = radius;
    }
}

} // namespace mgv
