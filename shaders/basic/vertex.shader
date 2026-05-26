#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNorm;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aTangent;

uniform mat4 u_model;
uniform mat4 u_viewProjection;

out vec3 vertexColor;

void main() {
    gl_Position = u_viewProjection * u_model * vec4(aPos, 1.0);
    vertexColor = abs(aNorm);
}
