/**
 * Depth of field - circle-of-confusion disk blur.
 *
 * Derives a CoC from how far each pixel's linear view depth sits from the
 * camera's focus distance, then blurs with a golden-angle disk whose radius
 * scales with the CoC. In-focus pixels short-circuit to a single tap, so the
 * cost is paid only where the image is actually defocused.
 */

in vec2 vUV;
out vec4 FragColor;

layout(binding = 18) uniform sampler2D u_sceneColor;  // resolved HDR scene
layout(binding = 19) uniform sampler2D u_sceneDepth;  // resolved scene depth

uniform mat4  u_projection;
uniform float u_focusDistance;
uniform float u_amount;      // 0 = off .. 1 = full
uniform float u_maxRadius;   // max blur radius in pixels
uniform vec2  u_texel;       // 1 / viewport size

const int   TAPS         = 16;
#include "../_common/constants.glsl"
#include "../_common/depth.glsl"

void main() {
    vec3 centre = texture(u_sceneColor, vUV).rgb;

    float viewDepth = linearizeViewDepth(texture(u_sceneDepth, vUV).r, u_projection);

    // Circle of confusion: 0 at the focus plane, saturating away from it.
    float coc = clamp(abs(viewDepth - u_focusDistance) / max(u_focusDistance, 1e-3), 0.0, 1.0) * u_amount;
    if (coc < 0.01) { FragColor = vec4(centre, 1.0); return; }

    float radius = coc * u_maxRadius;
    vec3  sum    = centre;
    for (int i = 1; i < TAPS; ++i) {
        float a = float(i) * GOLDEN_ANGLE;
        float r = sqrt(float(i) / float(TAPS)) * radius;
        sum += texture(u_sceneColor, vUV + vec2(cos(a), sin(a)) * r * u_texel).rgb;
    }

    FragColor = vec4(sum / float(TAPS), 1.0);
}
