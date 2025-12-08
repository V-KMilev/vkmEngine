#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNorm;
layout (location = 2) in vec2 aUV;

uniform mat4 u_modelMatrix;
uniform mat4 u_viewProjectionMatrix;

out vec3 vertexColor;

void main() {
    gl_Position = u_viewProjectionMatrix * u_modelMatrix * vec4(aPos, 1.0);
    vertexColor = abs(aNorm);
}
