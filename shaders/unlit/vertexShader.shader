/**
 * Unlit Vertex Shader - Instanced Rendering
 *
 * Same vertex layout as PBR but only passes position and UV to fragment.
 */
#version 420 core

// Per-vertex attributes (from mesh VBO)
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNorm;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aTangent;

// Per-instance model matrix (from instance buffer, divisor=1)
layout (location = 4) in vec4 aModelCol0;
layout (location = 5) in vec4 aModelCol1;
layout (location = 6) in vec4 aModelCol2;
layout (location = 7) in vec4 aModelCol3;

uniform mat4 u_viewProjection;

out vec2 TexCoords;

void main() {
    mat4 model = mat4(aModelCol0, aModelCol1, aModelCol2, aModelCol3);
    TexCoords = aUV;
    gl_Position = u_viewProjection * model * vec4(aPos, 1.0);
}
