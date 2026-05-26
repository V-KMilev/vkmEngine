#version 420 core

layout (location = 0) in vec3 a_position;

layout(std140, binding = 2) uniform CameraBlock {
    mat4 viewProjection;
    vec4 cameraPosition;  // xyz = position, w = exposure
    vec4 ambient;         // xyz = color, w = intensity
} u_camera;

uniform mat4 u_model;

out vec3 v_worldPos;

void main() {
    vec4 worldPos = u_model * vec4(a_position, 1.0);
    v_worldPos = worldPos.xyz;
    gl_Position = u_camera.viewProjection * worldPos;
}
