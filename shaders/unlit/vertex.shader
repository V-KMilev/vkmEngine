/**
 * Unlit vertex shader - instanced.
 *
 * Same per-vertex (0-3) / per-instance model matrix (4-7) layout as the PBR
 * shader. Emits only what the unlit fragment needs: clip position and UV.
 */
#version 420 core

layout (location = 0) in vec3 aPos;
layout (location = 2) in vec2 aUV;

layout (location = 4) in vec4 aModelCol0;
layout (location = 5) in vec4 aModelCol1;
layout (location = 6) in vec4 aModelCol2;
layout (location = 7) in vec4 aModelCol3;

layout(std140, binding = 2) uniform CameraBlock {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;
} u_camera;

out vec2 vUV;

void main() {
    mat4 model = mat4(aModelCol0, aModelCol1, aModelCol2, aModelCol3);
    vUV = aUV;
    gl_Position = u_camera.viewProjection * model * vec4(aPos, 1.0);
}
