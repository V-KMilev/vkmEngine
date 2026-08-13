/*
 * Shared light declarations: the GPU light struct + LightsBlock SSBO, the
 * Forward+ cluster grid, the light-type constants, and the punctual falloff.
 *
 * Must match GpuLight (gl_lights.h) and the LightType enum (light.h); the
 * SSBO binding points mirror GLBindings::SSBOBindingPoints. Include AFTER
 * engine_config.glsl (MAX_LIGHTS, MAX_LIGHTS_PER_CLUSTER).
 *
 * The cluster grid is declared readonly for its consumers (forward, fog
 * inject); the cull compute that fills it defines CLUSTER_GRID_WRITE before
 * including this to get the writeonly variant. Both use the instance name
 * u_clusters.
 */

// LightType (light.h).
#define LIGHT_DIRECTIONAL 0
#define LIGHT_POINT       1
#define LIGHT_SPOT        2
#define LIGHT_RECT        3
#define LIGHT_DISK        4

struct Light {
    vec4 position;   // xyz = world position, w = type
    vec4 color;      // xyz = rgb,            w = intensity
    vec4 direction;  // xyz = world dir,      w = attenuation radius
    vec4 spot;       // x = inner cone, y = outer cone (radians), z = unused, w = shadowSlot (-1 = none)
    vec4 axisU;      // xyz = half-right world axis (Rect/Disk), w = twoSided
    vec4 axisV;      // xyz = half-up    world axis (Rect/Disk), w = unused
};

layout(std430, binding = 0) readonly buffer LightsBlock {
    int   lightCount;
    int   _lp0; int _lp1; int _lp2;
    Light lights[MAX_LIGHTS];
} u_lights;

// Forward+ per-cluster light lists (filled by the cluster-cull compute pass).
struct ClusterLights {
    uint count;
    uint indices[MAX_LIGHTS_PER_CLUSTER];
};

#ifdef CLUSTER_GRID_WRITE
layout(std430, binding = 1) writeonly buffer ClusterGrid {
    ClusterLights clusters[];
} u_clusters;
#else
layout(std430, binding = 1) readonly buffer ClusterGrid {
    ClusterLights clusters[];
} u_clusters;
#endif

// Smooth windowed inverse-square falloff (physically based, finite range).
// The one punctual falloff - surface lighting and volumetric fog share it so
// a light's glow fades exactly like the light itself.
float distanceAttenuation(float dist, float radius) {
    float invSqr = 1.0 / max(dist * dist, 1e-4);
    float window = clamp(1.0 - pow(dist / max(radius, 1e-3), 4.0), 0.0, 1.0);
    return invSqr * window * window;
}
