layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aTangent;       // xyz = tangent, w = handedness
#include "../../_common/instancing.glsl"
#include "../../_common/instancing_normal.glsl"
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
    const mat4 model = instanceModel();
    vec4 worldPos = model * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;

    // Per-instance normal matrix (precomputed inverse-transpose) - correct normals
    // under non-uniform scale, no per-vertex matrix inverse.
    vNormal    = normalize(instanceNormalMatrix() * aNormal);

    // Tangent is a surface direction (model matrix); bitangent from the stored
    // handedness. The fragment shader re-normalises and builds the TBN basis.
    vTangent   = normalize(mat3(model) * aTangent.xyz);
    vBitangent = cross(vNormal, vTangent) * aTangent.w;

    vUV = aUV;
    gl_Position = u_camera.viewProjection * worldPos;
}
