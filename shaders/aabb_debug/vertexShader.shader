#version 420 core

layout (location = 0) in vec3 aPos;

uniform mat4 u_viewProjection;
uniform mat4 u_model;

void main() {
    gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
}

