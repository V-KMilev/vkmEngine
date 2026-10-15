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
#include "../../viewmodel.glsl"
#include "../../material.glsl"
#include "../../ocean.glsl"

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vTangent;
out float vHandedness;   // the fragment rebuilds B from it
out vec2  vRest;         // where the ocean's water rests: where the fragment reads its ripples and reach
out float vHeight;       // how high the ocean has lifted it
out vec3  vAlongX;       // the waves' tangents there (OceanPoint): the pixel's frame and squeeze
out vec3  vAlongZ;
out float vLingering;    // the foam a crest left there (oceanLingering)
flat out uint vViewmodel; // drawn through the viewmodel projection: its lights are found by vWorldPos

// Bit-exact with the depth prepass so the two agree under GLDepth::EYE_TEST_EQUAL.
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
    vNormal = normalize(normalMatrix(model) * normal);

    // A surface direction, so the model matrix, not the normal matrix.
    vTangent = normalize(mat3(model) * tangent);
    // A mirroring model turns the cross product the fragment rebuilds B with, so it turns the sign.
    vHandedness = aTangent.w * sign(determinant(mat3(model)));

    vUV   = aUV;
    vRest   = worldPos.xz;
    vHeight = 0.0;
    vAlongX = vec3(1.0, 0.0, 0.0);
    vAlongZ = vec3(0.0, 0.0, 1.0);
    vLingering = 0.0;
#ifndef SKINNED
    // The ocean's water: moved by the waves, its frame theirs, its maps laid over it in world
    // space with v along +Z - so B = -cross(N, T). As in the prepass, term for term.
    if (u_material.waves != 0 && oceanPresent()) {
        const OceanPoint water = oceanAt(worldPos.xz);
        vUV          = oceanUV(worldPos.xz);
        worldPos.xyz += water.offset;
        vNormal      = normalize(cross(water.alongZ, water.alongX));
        vTangent     = normalize(water.alongX);
        vHandedness  = -1.0;
        vHeight      = water.offset.y;
        vAlongX      = water.alongX;
        vAlongZ      = water.alongZ;
        vLingering   = max(oceanLingering(vRest), oceanSurgeFoam(vRest));
    }
#endif
    vWorldPos  = worldPos.xyz;
    vViewmodel = instanceIsViewmodel() ? 1u : 0u;

    gl_Position = eyeClip(worldPos);
}
