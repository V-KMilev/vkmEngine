// SKINNED (the skinned programs) poses position, normal and tangent by the bone palette first.
layout(location = ATTR_POSITION) in vec3 aPos;
layout(location = ATTR_NORMAL) in vec3 aNormal;
layout(location = ATTR_UV) in vec2 aUV;
layout(location = ATTR_TANGENT) in vec4 aTangent;  // xyz = tangent, w = handedness
#ifndef SKINNED
layout(location = ATTR_LIGHTMAP_UV) in vec2 aLightmapUV;  // the static layout's alone
#endif
#include "../../../include/instancing.glsl"
#include "../../../include/camera.glsl"
#include "../../../include/viewmodel.glsl"
#include "../../../include/material.glsl"
#ifdef BAKED
#include "../../../include/lightmap.glsl"
#endif

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vTangent;
out float vHandedness;   // the fragment rebuilds B from it
flat out uint vViewmodel; // drawn through the viewmodel projection: its lights are found by vWorldPos
flat out uint vFade;      // how the eye's draw fades it (fade.glsl)
#ifdef BAKED
out vec2      vLightmapUV;   // where in its lightmap page
flat out int  vLightmapPage; // which page, -1 for none
#endif

// Bit-exact with the depth prepass so the two agree under GLDepth::EYE_TEST_EQUAL.
invariant gl_Position;

void main() {
    const mat4 model = instanceModel();
    vec4 worldPos = instanceWorldPosition();
#ifdef SKINNED
    const mat3 skin3   = mat3(skinMatrix(instanceSkinBase()));
    vec3       normal  = skin3 * aNormal;
    vec3       tangent = skin3 * aTangent.xyz;
#else
    vec3 normal  = aNormal;
    vec3 tangent = aTangent.xyz;
#endif
    vNormal = normalize(normalMatrix(model) * normal);

    // A surface direction, so the model matrix, not the normal matrix.
    vTangent = normalize(mat3(model) * tangent);
    // A mirroring model turns the cross product the fragment rebuilds B with, so it turns the sign.
    vHandedness = aTangent.w * sign(determinant(mat3(model)));

    vUV   = materialUV(aUV);
#ifndef SKINNED
    if (u_material.boxMapping > 0.0) {
        vec3 faceTangent;
        vUV         = materialUV(boxUV(model, aPos, aNormal, faceTangent));
        vTangent    = normalize(mat3(model) * faceTangent);
        vHandedness = sign(determinant(mat3(model)));
    }
#endif
    vWorldPos  = worldPos.xyz;
    vViewmodel = instanceIsViewmodel() ? 1u : 0u;
    vFade      = instanceFade();

#ifdef BAKED
    vLightmapUV   = vec2(0.0);
    vLightmapPage = -1;
    if (u_hasLightmaps == 1) {
        const ObjectLightmap lightmap = b_lightmaps[instanceObject()];
        vLightmapUV   = pageUV(lightmap, aLightmapUV);
        vLightmapPage = lightmap.page.x;
    }
#endif

    gl_Position = eyeClip(worldPos);
}
