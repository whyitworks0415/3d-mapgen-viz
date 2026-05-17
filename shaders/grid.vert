#version 450

layout(set = 0, binding = 0) uniform UBO {
    mat4 view;
    mat4 proj;
    vec4 lightDir;   // xyz = direction toward light, w = ambient term
} ubo;

// Per-vertex attributes (unit cube).
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

// Per-instance attributes.
layout(location = 2) in vec3 inInstanceTranslation;
layout(location = 3) in vec3 inInstanceScale;
layout(location = 4) in vec3 inInstanceColor;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec3 vNormal;

void main() {
    // Non-uniform scale of axis-aligned cube. Cube face normals are axis
    // aligned so they stay correct without an inverse-transpose.
    vec3 worldPos = inPosition * inInstanceScale + inInstanceTranslation;
    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);

    vColor  = inInstanceColor;
    vNormal = inNormal;
}
