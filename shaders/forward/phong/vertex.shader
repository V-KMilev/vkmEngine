#version 430 core

// Minimal vertex stage for the reference Blinn-Phong shader. Not loaded by the
// runtime (the forward pass only builds forward/pbr) - kept as a readable, cheap
// alternative. It uses the same camera UBO and a subset of the forward vertex
// attributes (position/normal/uv, no tangent), so it could be swapped in.
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

layout(std140, binding = 2) uniform CameraBlock {
    mat4 viewProjection;
    vec4 cameraPosition;  // xyz = world position
} u_camera;

uniform mat4 u_model;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 worldPos = u_model * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal   = normalize(transpose(inverse(mat3(u_model))) * aNormal);
    vUV       = aUV;
    gl_Position = u_camera.viewProjection * worldPos;
}
