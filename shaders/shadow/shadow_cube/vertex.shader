#version 430 core

// Point-light cube shadow depth: rasterise into one cube face. The fragment
// stage writes linear distance-to-light, so the world position is passed down.
layout (location = 0) in vec3 aPos;

uniform mat4 u_faceVP;
uniform mat4 u_model;

out vec3 vWorldPos;

void main() {
    vec4 wp   = u_model * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    gl_Position = u_faceVP * wp;
}
