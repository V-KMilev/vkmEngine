#version 430 core

// Minimal vertex stage for the debug Phong shader. Same attribute layout and
// camera UBO as the forward shader, so it drops into the forward pass unchanged.
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
