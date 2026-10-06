/*
 * How the screen-space reflection reads the scene. Needs u_sceneDepth and
 * u_sceneGBuffer declared.
 */

#include "../depth.glsl"
#include "../camera.glsl"
#include "../normal_codec.glsl"

// It reflects something, smoothly enough that a trace replaces probes and sky.
bool tracesReflection(vec4 weight, float maxRoughness) {
    return weight.a < maxRoughness && dot(weight.rgb, vec3(1.0)) > 0.0;
}

// Linear view depth at a texel, the sky past anything a ray reaches.
float sceneDepthAt(ivec2 texel) {
    return linearSceneDepth(texelFetch(u_sceneDepth, texel, 0).r, u_camera.invProjection);
}

// The prepass normal (no normal map). The sky holds the clear, zero, which no
// visible surface encodes to (it faces away from the eye). Alpha-masked geometry
// is not in the prepass, so under it a texel holds what is behind.
bool sceneHasNormal(ivec2 texel) {
    return texelFetch(u_sceneGBuffer, texel, 0).rg != vec2(0.0);
}

vec3 sceneNormalAt(ivec2 texel) {
    return octDecode(texelFetch(u_sceneGBuffer, texel, 0).rg);
}

// View-space position at a texel of a picture this size.
vec3 sceneViewPosAt(ivec2 texel, vec2 size) {
    const vec2  uv    = (vec2(texel) + 0.5) / size;
    const float depth = texelFetch(u_sceneDepth, texel, 0).r;
    const vec4  p     = u_camera.invProjection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
