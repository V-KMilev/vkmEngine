/*
 * The GTAO depth mip chain: positive linear view depth, the sky SKY_DEPTH
 * (depth.glsl's linearSceneDepth).
 */

#include "../depth.glsl"

// Screen UV + linear view depth -> view-space position, for a symmetric
// projection; p00 / p11 are projection[0][0] / projection[1][1].
vec3 viewPosFromLinearDepth(vec2 uv, float z, float p00, float p11, bool perspective) {
    vec2 spread = perspective ? vec2(z / p00, z / p11) : vec2(1.0 / p00, 1.0 / p11);
    return vec3((uv * 2.0 - 1.0) * spread, -z);
}
