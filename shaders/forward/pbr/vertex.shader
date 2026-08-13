layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aTangent;       // xyz = tangent, w = handedness
layout (location = 4) in mat4 aModel;         // per-instance model matrix (loc 4-7, divisor 1)
layout (location = 8) in mat4 aNormalMatrix;  // per-instance normal matrix (loc 8-11; mat3 used)

#include "../../_common/camera.glsl"

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vTangent;
out vec3 vBitangent;

// Bit-exact position across programs so the depth prepass and this pass agree
// under LEQUAL early-Z (the prepass declares gl_Position invariant too).
invariant gl_Position;

void main() {
    vec4 worldPos = aModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;

    // Per-instance normal matrix (precomputed inverse-transpose) - correct normals
    // under non-uniform scale, no per-vertex matrix inverse.
    vNormal    = normalize(mat3(aNormalMatrix) * aNormal);

    // Tangent is a surface direction (model matrix); bitangent from the stored
    // handedness. The fragment shader re-normalises and builds the TBN basis.
    vTangent   = normalize(mat3(aModel) * aTangent.xyz);
    vBitangent = cross(vNormal, vTangent) * aTangent.w;

    vUV = aUV;
    gl_Position = u_camera.viewProjection * worldPos;
}
