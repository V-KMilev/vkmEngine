// Water's depth and G-buffer normal, placed as forward/water is (moveWater, invariant).
layout(location = ATTR_POSITION) in vec3 aPos;
layout(location = ATTR_NORMAL) in vec3 aNormal;
#include "../../../../include/instancing.glsl"
#include "../../../../include/camera.glsl"
#include "../../../../include/viewmodel.glsl"
#include "../../../../include/waves.glsl"

out vec3 vViewNormal;
flat out uint vFade;  // how the eye's draw fades it (fade.glsl)

invariant gl_Position;

void main() {
    const mat4 model = instanceModel();
    vec4 worldPos = instanceWorldPosition();

    // The fragment stage normalizes.
    vec3      worldN = normalMatrix(model) * aNormal;
    WavePoint water;
    if (moveWater(objectWaterBody(instanceObject()), worldPos, water) > 0.0) {
        worldN = cross(water.alongZ, water.alongX);
    }
    vViewNormal = mat3(u_camera.view) * worldN;
    vFade       = instanceFade();

    gl_Position = eyeClip(worldPos);
}
