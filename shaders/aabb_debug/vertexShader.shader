#version 420 core

layout (location = 0) in vec3 aPos;
layout (location = 4) in mat4 a_model;

layout(std140, binding = 2) uniform CameraBlock {
    mat4 viewProjection;
    vec4 cameraPosition;  // xyz = position, w = exposure
    vec4 ambient;         // xyz = color, w = intensity
} u_camera;

void main() {
    gl_Position = u_camera.viewProjection * a_model * vec4(aPos, 1.0);
}
