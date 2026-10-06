/**
 * Depth of field - circle-of-confusion disk blur.
 *
 * A golden-angle disk blur whose radius scales with each pixel's distance from the focus distance;
 * in-focus pixels take a single tap.
 */
in vec2 vUV;
out vec4 FragColor;

layout(binding = POST_SLOT_SCENE_COLOR) uniform sampler2D u_sceneColor;  // resolved HDR scene
layout(binding = POST_SLOT_SCENE_DEPTH) uniform sampler2D u_sceneDepth;  // resolved scene depth

uniform float u_focusDistance;
uniform float u_amount;      // 0 = off .. 1 = full
uniform float u_maxRadius;   // max blur radius in pixels

const int TAPS = 16;

#include "../constants.glsl"
#include "../depth.glsl"
#include "../camera.glsl"

void main() {
    vec3 centre = texture(u_sceneColor, vUV).rgb;

    float viewDepth = linearizeViewDepth(texture(u_sceneDepth, vUV).r, u_camera.invProjection);

    // Circle of confusion: 0 at the focus plane, saturating away from it.
    float coc = clamp(abs(viewDepth - u_focusDistance) / max(u_focusDistance, 1e-3), 0.0, 1.0) * u_amount;
    if (coc < 0.01) { FragColor = vec4(centre, 1.0); return; }

    float radius = coc * u_maxRadius;
    vec2  texel  = 1.0 / u_camera.viewport;
    vec3  sum    = centre;
    for (int i = 1; i < TAPS; ++i) {
        float a = float(i) * GOLDEN_ANGLE;
        float r = sqrt(float(i) / float(TAPS)) * radius;
        sum += texture(u_sceneColor, vUV + vec2(cos(a), sin(a)) * r * texel).rgb;
    }

    FragColor = vec4(sum / float(TAPS), 1.0);
}
