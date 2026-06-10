#version 430 core

// Depth-only shadow pass: project geometry into the light's clip space. Only
// position is needed; the mesh's other attributes are ignored. The draw path
// sets u_model per object (non-instanced), matching the forward pass.
layout (location = 0) in vec3 aPos;

uniform mat4 u_lightVP;
uniform mat4 u_model;

void main() {
    gl_Position = u_lightVP * u_model * vec4(aPos, 1.0);
}
