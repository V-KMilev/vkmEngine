/**
 * Depth/normal prepass vertex shader - instanced.
 *
 * Emits view-space position and normal for screen-space AO. u_view and
 * u_projection are set per frame by GLPrepass (no CameraBlock dependency).
 */
#version 420 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

layout (location = 4) in vec4 aModelCol0;
layout (location = 5) in vec4 aModelCol1;
layout (location = 6) in vec4 aModelCol2;
layout (location = 7) in vec4 aModelCol3;

uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 vViewPos;
out vec3 vViewNormal;

void main() {
    mat4 model = mat4(aModelCol0, aModelCol1, aModelCol2, aModelCol3);
    mat4 modelView = u_view * model;

    vec4 viewPos = modelView * vec4(aPos, 1.0);
    vViewPos = viewPos.xyz;
    vViewNormal = normalize(mat3(modelView) * aNormal);

    gl_Position = u_projection * viewPos;
}
