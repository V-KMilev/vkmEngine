/*
 * The Forward+ cluster grid: each cluster's light list, and the cluster a fragment falls in.
 * Bindings, MAX_LIGHTS_PER_CLUSTER and CLUSTER_* come from the shader prelude.
 *
 * Define CLUSTER_GRID_WRITE before including for a writeonly grid.
 */

#include "depth.glsl"

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
