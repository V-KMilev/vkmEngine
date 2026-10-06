/*
 * The GPU light SSBO, the Forward+ cluster grid and lookup, and punctual falloff.
 * Must match GpuLight (gl_lights.h); bindings, MAX_LIGHTS* and LIGHT_* come from
 * the shader prelude.
 *
 * Define CLUSTER_GRID_WRITE before including for a writeonly grid.
 */
struct Light {
    vec4 position;   // xyz = world position, w = type
    vec4 color;      // xyz = rgb,            w = intensity
    vec4 direction;  // xyz = unit world dir, w = attenuation radius
    // x, y = cone scale, offset (see spotAttenuation), z = tan of a
    // directional's disc radius, w = shadowSlot (-1 = none)
    vec4 spot;
    vec4 axisU;      // xyz = half-right world axis (Rect/Disk), w = twoSided
    vec4 axisV;      // xyz = half-up    world axis (Rect/Disk), w = unused
};

#include "depth.glsl"

layout(std430, binding = SSBO_LIGHTS) readonly buffer LightsBlock {
    int   lightCount;
    int   _lp0; int _lp1; int _lp2;
    Light lights[MAX_LIGHTS];
} u_lights;

// Forward+ per-cluster light lists.
struct ClusterLights {
    uint count;
    uint indices[MAX_LIGHTS_PER_CLUSTER];
};

#ifdef CLUSTER_GRID_WRITE
layout(std430, binding = SSBO_CLUSTER_GRID) writeonly buffer ClusterGrid {
    ClusterLights clusters[];
} u_clusters;
#else
layout(std430, binding = SSBO_CLUSTER_GRID) readonly buffer ClusterGrid {
    ClusterLights clusters[];
} u_clusters;
#endif

// The cluster a screen position (0..1) at a linear view depth falls in, by the
// same tiles and slicing as the cull.
int clusterIndex(vec2 screenUV, float viewDepth, float zNear, float zFar) {
    uint  tx    = uint(clamp(screenUV.x * float(CLUSTER_X), 0.0, float(CLUSTER_X - 1)));
    uint  ty    = uint(clamp(screenUV.y * float(CLUSTER_Y), 0.0, float(CLUSTER_Y - 1)));
    float slice = viewDepthToSlice(viewDepth, zNear, zFar, float(CLUSTER_Z));
    uint  tz    = uint(clamp(floor(slice), 0.0, float(CLUSTER_Z - 1)));
    return int(tx + ty * uint(CLUSTER_X) + tz * uint(CLUSTER_X * CLUSTER_Y));
}

// The smooth window that takes a light to exactly zero at its radius.
float distanceWindow(float dist, float radius) {
    float window = clamp(1.0 - pow(dist / max(radius, 1e-3), 4.0), 0.0, 1.0);
    return window * window;
}

// Windowed inverse-square falloff: physically based with a finite range.
float distanceAttenuation(float dist, float radius) {
    return distanceWindow(dist, radius) / max(dist * dist, 1e-4);
}

// 1 inside the inner angle, 0 past the outer, squared between. spot.xy are
// 1 / (cos inner - cos outer) and -cos outer times that: one multiply-add.
float spotAttenuation(Light light, vec3 L) {
    float cone = clamp(dot(light.direction.xyz, -L) * light.spot.x + light.spot.y, 0.0, 1.0);
    return cone * cone;
}

// A directional, point or spot light's falloff at p, and the unit L toward it.
float punctualAttenuation(Light light, int type, vec3 p, out vec3 L) {
    if (type == LIGHT_DIRECTIONAL) {
        L = -light.direction.xyz;
        return 1.0;
    }
    vec3  toLight = light.position.xyz - p;
    float dist    = length(toLight);
    L = toLight / max(dist, 1e-4);
    float atten = distanceAttenuation(dist, light.direction.w);
    if (type == LIGHT_SPOT) atten *= spotAttenuation(light, L);
    return atten;
}
