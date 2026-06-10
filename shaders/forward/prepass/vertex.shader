#version 430 core

// Depth + G-buffer prepass: lay down opaque + alpha-masked depth so the forward
// pass can early-Z, and write a view-space normal for SSR. Position must be
// bit-identical to the forward vertex shader (same math + invariant) so the
// winning depth matches under LEQUAL. UV is passed for the alpha-mask discard.
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

layout(std140, binding = 2) uniform CameraBlock {
    mat4 viewProjection;
    vec4 cameraPosition;
} u_camera;

uniform mat4 u_model;
uniform mat4 u_view;   // world -> view, for the G-buffer normal

out vec2 vUV;
out vec3 vViewNormal;

invariant gl_Position;

void main() {
    vec4 worldPos = u_model * vec4(aPos, 1.0);
    vUV = aUV;

    // World normal via the model's normal matrix, then into view space for SSR.
    vec3 worldN = mat3(transpose(inverse(u_model))) * aNormal;
    vViewNormal = mat3(u_view) * worldN;

    gl_Position = u_camera.viewProjection * worldPos;
}
