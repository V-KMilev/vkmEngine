// Depth + G-buffer prepass. Position must be bit-identical to the forward vertex shader (same
// math + invariant) so depth matches under GLDepth::EYE_TEST_EQUAL.
// SKINNED (the skinned prepass) poses position and normal by the bone palette first.
layout(location = ATTR_POSITION) in vec3 aPos;
layout(location = ATTR_NORMAL) in vec3 aNormal;
#include "../../../include/instancing.glsl"
#include "../../../include/camera.glsl"
#include "../../../include/viewmodel.glsl"

out vec3 vViewNormal;
flat out uint vFade;  // how the eye's draw fades it (fade.glsl)

invariant gl_Position;

void main() {
    const mat4 model = instanceModel();
    vec4 worldPos = instanceWorldPosition();
#ifdef SKINNED
    vec3 normal = mat3(skinMatrix(instanceSkinBase())) * aNormal;
#else
    vec3 normal = aNormal;
#endif

    // The fragment stage normalizes.
    vec3 worldN = normalMatrix(model) * normal;
    vViewNormal = mat3(u_camera.view) * worldN;
    vFade       = instanceFade();

    gl_Position = eyeClip(worldPos);
}
