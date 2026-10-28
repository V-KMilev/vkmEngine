// Depth + G-buffer prepass. Position must be bit-identical to the forward vertex shader (same
// math + invariant) so depth matches under GLDepth::EYE_TEST_EQUAL.
// SKINNED (forward/prepass_skinned) poses position and normal by the bone palette first.
layout(location = ATTR_POSITION) in vec3 aPos;
layout(location = ATTR_NORMAL) in vec3 aNormal;
#include "../../instancing.glsl"
#include "../../camera.glsl"
#include "../../viewmodel.glsl"
#include "../../material.glsl"
#include "../../ocean.glsl"

out vec3 vViewNormal;

invariant gl_Position;

void main() {
    const mat4 model = instanceModel();
    vec4 worldPos = instanceWorldPosition();
#ifdef SKINNED
    vec3 normal = mat3(skinMatrix(instanceSkinBase())) * aNormal;
#else
    vec3 normal = aNormal;
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

    gl_Position = eyeClip(worldPos);
}
