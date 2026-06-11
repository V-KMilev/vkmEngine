#version 430 core

// Depth-only shadow pass: project geometry into the light's clip space. Only
// position is needed; the model matrix arrives per-instance (the shadow pass
// batches casters by mesh and draws them instanced).
layout (location = 0) in vec3 aPos;
layout (location = 4) in mat4 aModel;  // per-instance model matrix (loc 4-7, divisor 1)

uniform mat4 u_lightVP;

void main() {
    gl_Position = u_lightVP * aModel * vec4(aPos, 1.0);
}
