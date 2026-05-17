#version 450

layout(set = 0, binding = 0) uniform UBO {
    mat4 view;
    mat4 proj;
    vec4 lightDir;   // xyz = light direction, w = ambient
} ubo;

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vNormal;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-ubo.lightDir.xyz);
    float diff    = max(dot(N, L), 0.0);
    float ambient = ubo.lightDir.w;
    vec3 shaded   = vColor * (ambient + diff * (1.0 - ambient));
    outColor = vec4(shaded, 1.0);
}
