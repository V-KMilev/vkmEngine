/**
 * Shadow depth shader - instanced, shared by 2D-array + cube-array passes.
 *
 * Writes projected depth into the depth attachment the FBO currently points
 * at; the per-layer / per-face selection is done CPU-side via
 * glFramebufferTextureLayer before each draw. Both targets use compare-mode
 * sampling, so we just need the standard projected gl_Position.
 */
#version 420 core

layout (location = 0) in vec3 aPos;

// Per-instance model matrix (locations 1-3 unused for depth-only).
layout (location = 4) in vec4 aModelCol0;
layout (location = 5) in vec4 aModelCol1;
layout (location = 6) in vec4 aModelCol2;
layout (location = 7) in vec4 aModelCol3;

uniform mat4 u_lightSpace;

void main() {
    mat4 model = mat4(aModelCol0, aModelCol1, aModelCol2, aModelCol3);
    gl_Position = u_lightSpace * model * vec4(aPos, 1.0);
}
