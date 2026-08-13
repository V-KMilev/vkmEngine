/**
 * Forward+ cluster light cull.
 *
 * One invocation per cluster: build the cluster's view-space AABB, test every
 * light against it (directional lights hit all clusters), and write the surviving
 * light indices into the cluster grid the forward pass reads. Single pass, no
 * atomics - each cluster owns its own slot in the grid.
 */

#include "../_generated/engine_config.glsl"  // CLUSTER_X/Y/Z, NUM_CLUSTERS, MAX_LIGHTS, MAX_LIGHTS_PER_CLUSTER
#include "../_common/depth.glsl"

// This pass fills the grid: take the writeonly cluster-grid variant.
#define CLUSTER_GRID_WRITE
#include "../_common/lights.glsl"            // Light + LightsBlock + cluster grid + LIGHT_*

layout(local_size_x = 64) in;

uniform mat4  u_view;           // world -> view (light positions -> view space)
uniform mat4  u_invProjection;  // clip -> view (tile corners -> view rays)
uniform vec2  u_screenSize;     // viewport pixels
uniform float u_zNear;
uniform float u_zFar;


// Screen pixel -> a point on the near plane in view space (a ray from the eye).
vec3 screenToView(vec2 px) {
    vec2 ndc  = px / u_screenSize * 2.0 - 1.0;
    vec4 clip = vec4(ndc, -1.0, 1.0);
    vec4 view = u_invProjection * clip;
    return view.xyz / view.w;
}

// Point on the eye ray through @p dir at view-space depth @p z (negative).
vec3 zPlaneIntersect(vec3 dir, float z) {
    return dir * (z / dir.z);
}

float sqDistPointAABB(vec3 p, vec3 mn, vec3 mx) {
    vec3 d = max(max(mn - p, p - mx), vec3(0.0));
    return dot(d, d);
}

void main() {
    uint ci = gl_GlobalInvocationID.x;
    if (ci >= uint(NUM_CLUSTERS)) return;

    // Decode the cluster's (x, y, z) grid coordinate.
    uint x = ci % uint(CLUSTER_X);
    uint y = (ci / uint(CLUSTER_X)) % uint(CLUSTER_Y);
    uint z = ci / uint(CLUSTER_X * CLUSTER_Y);

    // Screen-tile corner rays (view space, from the eye).
    vec2 tileSize = u_screenSize / vec2(CLUSTER_X, CLUSTER_Y);
    vec3 minRay = screenToView(vec2(x,      y)      * tileSize);
    vec3 maxRay = screenToView(vec2(x + 1u, y + 1u) * tileSize);

    // Exponential depth slice: view-space near/far Z of this slice (negative).
    float zNearV = -sliceToViewDepth(float(z),      u_zNear, u_zFar, float(CLUSTER_Z));
    float zFarV  = -sliceToViewDepth(float(z + 1u), u_zNear, u_zFar, float(CLUSTER_Z));

    // AABB over the tile's four corner rays at the slice's near + far planes.
    vec3 p0 = zPlaneIntersect(minRay, zNearV);
    vec3 p1 = zPlaneIntersect(minRay, zFarV);
    vec3 p2 = zPlaneIntersect(maxRay, zNearV);
    vec3 p3 = zPlaneIntersect(maxRay, zFarV);
    vec3 mn = min(min(p0, p1), min(p2, p3));
    vec3 mx = max(max(p0, p1), max(p2, p3));

    uint count = 0u;
    for (int i = 0; i < u_lights.lightCount && i < MAX_LIGHTS; ++i) {
        Light L = u_lights.lights[i];
        bool inside;
        if (int(L.position.w) == LIGHT_DIRECTIONAL) {
            inside = true;  // no position/range: affects the whole frustum
        } else {
            vec3  posV   = (u_view * vec4(L.position.xyz, 1.0)).xyz;
            float radius = L.direction.w;
            inside = sqDistPointAABB(posV, mn, mx) <= radius * radius;
        }
        if (inside && count < uint(MAX_LIGHTS_PER_CLUSTER)) {
            u_clusters.clusters[ci].indices[count] = uint(i);
            ++count;
        }
    }
    u_clusters.clusters[ci].count = count;
}
