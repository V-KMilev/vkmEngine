/*
 * How the screen-space reflection reads the scene.
 * Needs u_sceneDepth declared.
 */

#include "../../include/depth.glsl"
#include "../../include/camera.glsl"
#include "../../include/scene_gbuffer.glsl"

// It reflects something a trace replaces: the forward pass zeroes the weight of a material too
// rough for one (traceFade).
bool tracesReflection(vec4 weight) {
    return dot(weight.rgb, vec3(1.0)) > 0.0;
}

// Linear view depth at a texel, the sky past anything a ray reaches.
float sceneDepthAt(ivec2 texel) {
    return linearSceneDepth(texelFetch(u_sceneDepth, texel, 0).r, u_camera.invProjection);
}

// View-space position at a texel of a picture this size.
vec3 sceneViewPosAt(ivec2 texel, vec2 size) {
    const vec2  uv    = (vec2(texel) + 0.5) / size;
    const float depth = texelFetch(u_sceneDepth, texel, 0).r;
    const vec4  p     = u_camera.invProjection * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return p.xyz / p.w;
}

// How far off a surface at view position @p p its depth can resolve it.
float depthTolerance(vec3 p) {
    return 0.01 + 0.002 * -p.z;
}
