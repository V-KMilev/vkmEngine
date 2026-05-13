/**
 * Cube shadow depth shader - vertex stage.
 *
 * Writes one face of a samplerCubeArrayShadow target per draw. The cube
 * face is selected on the CPU side via glFramebufferTextureLayer; the
 * vertex stage passes the world-space fragment position to the fragment
 * stage so it can store a linear distance-based depth.
 */
#version 420 core

layout (location = 0) in vec3 aPos;

layout (location = 4) in vec4 aModelCol0;
layout (location = 5) in vec4 aModelCol1;
layout (location = 6) in vec4 aModelCol2;
layout (location = 7) in vec4 aModelCol3;

uniform mat4 u_lightSpace;

out vec3 v_worldPos;

void main() {
    mat4 model = mat4(aModelCol0, aModelCol1, aModelCol2, aModelCol3);
    vec4 wp = model * vec4(aPos, 1.0);
    v_worldPos = wp.xyz;
    gl_Position = u_lightSpace * wp;
}
