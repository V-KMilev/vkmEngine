// Water's surface (Water). One with waves is moved by its share of the scene's, its frame theirs and
// its maps laid over it in world space with v along +Z - so B = -cross(N, T). Placed as
// forward/prepass/water is (moveWater).
layout(location = ATTR_POSITION) in vec3 aPos;
layout(location = ATTR_NORMAL) in vec3 aNormal;
layout(location = ATTR_UV) in vec2 aUV;
layout(location = ATTR_TANGENT) in vec4 aTangent;  // xyz = tangent, w = handedness
#include "../../../include/instancing.glsl"
#include "../../../include/camera.glsl"
#include "../../../include/viewmodel.glsl"
#include "../../../include/material.glsl"
#include "../../../include/waves.glsl"

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vTangent;
out float vHandedness;   // the fragment rebuilds B from it
out vec2  vRest;         // where the water rests: where the fragment reads its ripples and reach
out float vHeight;       // how high the waves have lifted it
out vec3  vAlongX;       // the waves' tangents there (WavePoint): the pixel's frame and squeeze
out vec3  vAlongZ;
out float vLingering;    // the foam a crest left there (lingeringFoam)
flat out uint vViewmodel; // drawn through the viewmodel projection: its lights are found by vWorldPos
flat out uint vFade;      // how the eye's draw fades it (fade.glsl)
flat out uint vBody;      // its Water's place in b_waterBodies

// Bit-exact with the depth prepass so the two agree under GLDepth::EYE_TEST_EQUAL.
invariant gl_Position;

void main() {
    const mat4 model = instanceModel();
    vec4 worldPos = instanceWorldPosition();
    vNormal = normalize(normalMatrix(model) * aNormal);

    // A surface direction, so the model matrix, not the normal matrix.
    vTangent = normalize(mat3(model) * aTangent.xyz);
    // A mirroring model turns the cross product the fragment rebuilds B with, so it turns the sign.
    vHandedness = aTangent.w * sign(determinant(mat3(model)));

    vUV        = materialUV(aUV);
    vRest      = worldPos.xz;
    vHeight    = 0.0;
    vAlongX    = vec3(1.0, 0.0, 0.0);
    vAlongZ    = vec3(0.0, 0.0, 1.0);
    vLingering = 0.0;
    vBody      = objectWaterBody(instanceObject());
    WavePoint   water;
    const float share = moveWater(vBody, worldPos, water);
    if (share > 0.0) {
        vUV          = wavesUV(vRest);
        vNormal      = normalize(cross(water.alongZ, water.alongX));
        vTangent     = normalize(water.alongX);
        vHandedness  = -1.0;
        vHeight      = water.offset.y;
        vAlongX      = water.alongX;
        vAlongZ      = water.alongZ;
        vLingering   = max(lingeringFoam(vRest), surgeFoam(vRest)) * share;
    }
    vWorldPos  = worldPos.xyz;
    vViewmodel = instanceIsViewmodel() ? 1u : 0u;
    vFade      = instanceFade();

    gl_Position = eyeClip(worldPos);
}
