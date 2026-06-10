#version 430 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aTangent;  // xyz = tangent, w = handedness

layout(std140, binding = 2) uniform CameraBlock {
    mat4 viewProjection;
    vec4 cameraPosition;  // xyz = world position
} u_camera;

uniform mat4 u_model;
uniform mat3 u_normalMatrix;  // transpose(inverse(mat3(model))), computed CPU-side per draw

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vTangent;
out vec3 vBitangent;

// Bit-exact position across programs so the depth prepass and this pass agree
// under LEQUAL early-Z (the prepass declares gl_Position invariant too).
invariant gl_Position;

void main() {
    vec4 worldPos = u_model * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;

    // Normal matrix is the precomputed inverse-transpose - keeps normals correct
    // under non-uniform scale without a per-vertex matrix inverse.
    vNormal    = normalize(u_normalMatrix * aNormal);

    // Tangent is a surface direction (model matrix); bitangent from the stored
    // handedness. The fragment shader re-normalises and builds the TBN basis.
    vTangent   = normalize(mat3(u_model) * aTangent.xyz);
    vBitangent = cross(vNormal, vTangent) * aTangent.w;

    vUV = aUV;
    gl_Position = u_camera.viewProjection * worldPos;
}
