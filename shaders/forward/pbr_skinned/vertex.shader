// The lit forward vertex stage for skinned geometry: shaders/forward/pbr with
// position, normal and tangent posed by the frame's bone palette first. The
// fragment stage is not a copy of the static one - it is the same file.
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;
layout (location = 3) in vec4 aTangent;       // xyz = tangent, w = handedness
#include "../../_common/instancing.glsl"
#include "../../_common/instancing_normal.glsl"
#include "../../_common/skinning.glsl"
#include "../../_common/skinning_instanced.glsl"
#include "../../_common/camera.glsl"

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vTangent;
out vec3 vBitangent;

// Bit-exact position across programs so the depth prepass and this pass agree
// under LEQUAL early-Z (prepass_skinned declares gl_Position invariant too, and
// reaches it through the same skinnedWorldPosition).
invariant gl_Position;

void main() {
    const mat4 model = instanceModel();
    const uint base  = instanceSkinBase();

    vec4 worldPos = skinnedWorldPosition(model, base);
    vWorldPos = worldPos.xyz;

    // The skin matrix rotates the surface basis into the posed shape; the
    // per-instance normal matrix (precomputed inverse-transpose) then takes it
    // to world space, exactly as the static path does.
    const mat3 skin3 = mat3(skinMatrix(base));
    vNormal    = normalize(instanceNormalMatrix() * (skin3 * aNormal));

    vTangent   = normalize(mat3(model) * (skin3 * aTangent.xyz));
    vBitangent = cross(vNormal, vTangent) * aTangent.w;

    vUV = aUV;
    gl_Position = u_camera.viewProjection * worldPos;
}
