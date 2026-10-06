// SKINNED (forward/pbr_skinned) poses position, normal and tangent by the bone palette first.
layout(location = ATTR_POSITION) in vec3 aPos;
layout(location = ATTR_NORMAL) in vec3 aNormal;
layout(location = ATTR_UV) in vec2 aUV;
layout(location = ATTR_TANGENT) in vec4 aTangent;  // xyz = tangent, w = handedness
#include "../../instancing.glsl"
#ifdef SKINNED
#include "../../skinning.glsl"
#include "../../skinning_instanced.glsl"
#endif
#include "../../camera.glsl"

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vTangent;
out float vHandedness;   // the fragment rebuilds B from it

// Bit-exact with the depth prepass so the two agree under LEQUAL early-Z.
invariant gl_Position;

void main() {
    const mat4 model = instanceModel();
#ifdef SKINNED
    const uint base  = instanceSkinBase();
    const mat3 skin3 = mat3(skinMatrix(base));
    vec4 worldPos    = skinnedWorldPosition(model, base);
    vec3 normal      = skin3 * aNormal;
    vec3 tangent     = skin3 * aTangent.xyz;
#else
    vec4 worldPos    = model * vec4(aPos, 1.0);
    vec3 normal      = aNormal;
    vec3 tangent     = aTangent.xyz;
#endif
    vWorldPos = worldPos.xyz;

    vNormal = normalize(normalMatrix(model) * normal);

    // A surface direction, so the model matrix, not the normal matrix.
    vTangent = normalize(mat3(model) * tangent);
    vHandedness = aTangent.w;

    vUV = aUV;
    gl_Position = u_camera.viewProjection * worldPos;
}
