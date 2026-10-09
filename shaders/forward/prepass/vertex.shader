// Depth + G-buffer prepass. Position must be bit-identical to the forward
// vertex shader (same math + invariant) so depth matches under LEQUAL.
// SKINNED (forward/prepass_skinned) poses position and normal by the bone palette first.
layout(location = ATTR_POSITION) in vec3 aPos;
layout(location = ATTR_NORMAL) in vec3 aNormal;
#include "../../instancing.glsl"
#ifdef SKINNED
#include "../../skinning.glsl"
#include "../../skinning_instanced.glsl"
#endif
#include "../../camera.glsl"
#include "../../material.glsl"
#include "../../ocean.glsl"

out vec3 vViewNormal;

invariant gl_Position;

void main() {
    const mat4 model = instanceModel();
#ifdef SKINNED
    const uint base = instanceSkinBase();
    vec4 worldPos   = skinnedWorldPosition(model, base);
    vec3 normal     = mat3(skinMatrix(base)) * aNormal;
#else
    vec4 worldPos   = model * vec4(aPos, 1.0);
    vec3 normal     = aNormal;
#endif

    // The fragment stage normalises.
    vec3 worldN = normalMatrix(model) * normal;
#ifndef SKINNED
    // The ocean's water, moved as the forward pass moves it (forward/pbr/vertex.shader).
    if (u_material.waves != 0 && oceanPresent()) {
        const OceanPoint water = oceanAt(worldPos.xz);
        worldPos.xyz += water.offset;
        worldN       = cross(water.alongZ, water.alongX);
    }
#endif
    vViewNormal = mat3(u_camera.view) * worldN;

    gl_Position = u_camera.viewProjection * worldPos;
}
