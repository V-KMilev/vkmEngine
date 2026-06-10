#version 430 core

// Depth prepass: lay down opaque + alpha-masked depth so the forward pass can
// early-Z out hidden fragments. Position must be bit-identical to the forward
// vertex shader (same math + invariant) so the winning depth matches under
// LEQUAL. UV is passed for the alpha-mask discard.
layout (location = 0) in vec3 aPos;
layout (location = 2) in vec2 aUV;

layout(std140, binding = 2) uniform CameraBlock {
    mat4 viewProjection;
    vec4 cameraPosition;
} u_camera;

uniform mat4 u_model;

out vec2 vUV;

invariant gl_Position;

void main() {
    vec4 worldPos = u_model * vec4(aPos, 1.0);
    vUV = aUV;
    gl_Position = u_camera.viewProjection * worldPos;
}
