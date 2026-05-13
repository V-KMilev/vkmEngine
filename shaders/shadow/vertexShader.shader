/**
 * 2D shadow depth shader - instanced, used for directional + spot maps.
 *
 * Writes depth into one layer of a sampler2DArrayShadow target. The matching
 * layer is selected on the CPU side via glFramebufferTextureLayer; the
 * vertex stage just needs the per-caster lightSpace matrix as a uniform.
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
