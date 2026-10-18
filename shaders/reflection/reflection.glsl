/*
 * How the screen-space reflection reads the scene, and where the planar reflectors lie on it.
 * Needs u_sceneDepth declared.
 */

#include "../depth.glsl"
#include "../camera.glsl"
#include "../scene_gbuffer.glsl"

// It reflects something, smoothly enough that a trace replaces probes and sky.
bool tracesReflection(vec4 weight, float maxRoughness) {
    return weight.a < maxRoughness && dot(weight.rgb, vec3(1.0)) > 0.0;
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

// This frame's planar reflectors, in view space: the plane (unit normal, distance), the rectangle's
// centre and its axes over its half-extents.
uniform int   u_planarCount;
uniform vec4  u_planarPlane[MAX_PLANAR_REFLECTORS];
uniform vec3  u_planarCenter[MAX_PLANAR_REFLECTORS];
uniform vec3  u_planarU[MAX_PLANAR_REFLECTORS];
uniform vec3  u_planarV[MAX_PLANAR_REFLECTORS];

// The planar reflector the surface at view position @p p with normal @p n lies on, or -1.
int planarAt(vec3 p, vec3 n) {
    for (int i = 0; i < u_planarCount && i < MAX_PLANAR_REFLECTORS; ++i) {
        const vec4 plane = u_planarPlane[i];
        // On the plane, as far as the depth resolves it there, and facing its way.
        if (abs(dot(plane.xyz, p) + plane.w) > depthTolerance(p)) continue;
        if (dot(n, plane.xyz) < 0.7) continue;
        const vec3 off = p - u_planarCenter[i];
        if (abs(dot(off, u_planarU[i])) > 1.0 || abs(dot(off, u_planarV[i])) > 1.0) continue;
        return i;
    }
    return -1;
}
