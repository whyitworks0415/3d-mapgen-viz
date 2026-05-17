#pragma once

#include <glm/glm.hpp>

namespace mgv {

// Orbit camera. Z is "up" in world space; this matches the MapData convention
// (map sits on the XY plane, height stacks along +Z) and feels natural in a
// Blender-style viewport.
//
// State:
//   target_, distance_, yaw_, pitch_  parameterise the camera in spherical
//   coordinates around the look-at point. Projection is either perspective
//   (FOV) or orthographic (orthoSize_ derived from distance_).
class Camera {
public:
    enum class Projection { Perspective, Orthographic };

    Camera();

    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix() const;
    glm::vec3 position() const;

    void orbit(float dYaw, float dPitch);
    void pan(float dxPx, float dyPx);          // screen-space pixel deltas
    void zoom(float wheelTicks);               // positive = zoom in
    void moveLocal(float right, float forward, float up);

    void setFrontView();
    void setBackView();
    void setRightView();
    void setLeftView();
    void setTopView();
    void setBottomView();
    void toggleProjection();
    void focusOn(glm::vec3 center, float radius);

    void setAspect(float aspect)            { aspect_ = aspect; }
    void setViewportSize(float w, float h)  { viewportW_ = w; viewportH_ = h; }

    Projection projection() const           { return proj_; }
    const char* projectionName() const {
        return proj_ == Projection::Perspective ? "Perspective" : "Orthographic";
    }
    float            distance() const { return distance_; }
    const glm::vec3& target()   const { return target_;   }
    float            yaw()      const { return yaw_;      }
    float            pitch()    const { return pitch_;    }

private:
    glm::vec3 forwardVec() const;
    glm::vec3 rightVec()   const;
    glm::vec3 upVec()      const;
    glm::vec3 upReference() const;             // glm::lookAt up-vector, gimbal-safe

    glm::vec3 target_   { 0.0f };
    float     distance_ = 30.0f;
    float     yaw_      = -0.78539816f;        // -45 deg
    float     pitch_    =  0.52359878f;        //  30 deg

    Projection proj_      = Projection::Perspective;
    float      fovY_      = 1.04719755f;       // 60 deg
    float      orthoSize_ = 20.0f;
    float      aspect_    = 16.0f / 9.0f;
    float      near_      = 0.1f;
    float      far_       = 2000.0f;

    float      viewportW_ = 1280.0f;
    float      viewportH_ =  720.0f;

    static constexpr float kPitchLimit = 1.55334303f;   // ~89 deg
};

} // namespace mgv
