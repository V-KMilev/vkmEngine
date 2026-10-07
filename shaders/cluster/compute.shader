/**
 * Forward+ cluster light cull.
 *
 * One invocation per cluster: build the cluster's view-space AABB, test every
 * light against it (directional lights hit all clusters), and write the surviving
 * light indices into the cluster grid. Single pass, no atomics - each cluster
 * owns its own slot in the grid.
 */

#include "../depth.glsl"

// This pass fills the grid: take the writeonly cluster-grid variant.
#define CLUSTER_GRID_WRITE
#include "../lights.glsl"

layout(local_size_x = GROUP_CLUSTERS) in;

#include "../camera.glsl"

// Screen pixel -> a point on the near plane in view space (a ray from the eye).
vec3 screenToView(vec2 px) {
    vec2 ndc  = px / u_camera.viewport * 2.0 - 1.0;
    vec4 clip = vec4(ndc, -1.0, 1.0);
    vec4 view = u_camera.invProjection * clip;
    return view.xyz / view.w;
}

// Point on the eye ray through @p dir at view-space depth @p z (negative). An
// orthographic ray runs straight down -Z from its near-plane point.
vec3 zPlaneIntersect(vec3 dir, float z) {
    return cameraIsPerspective() ? dir * (z / dir.z) : vec3(dir.xy, z);
}

float sqDistPointAABB(vec3 p, vec3 mn, vec3 mx) {
    vec3 d = max(max(mn - p, p - mx), vec3(0.0));
    return dot(d, d);
}

// Whether a spot's cone - apex @p apex, axis @p axis, half-angle cos/sin, length @p range -
// misses the sphere @p centre, @p radius (Wronski, "Cull that cone"): beside the cone, past its
// end, or behind its apex.
bool coneMissesSphere(vec3 apex, vec3 axis, float cosA, float sinA, float range, vec3 centre, float radius) {
    vec3  v       = centre - apex;
    float along   = dot(v, axis);
    float across  = sqrt(max(dot(v, v) - along * along, 0.0));
    float closest = cosA * across - along * sinA;
    return closest > radius || along > radius + range || along < -radius;
}

void main() {
    uint ci = gl_GlobalInvocationID.x;
    if (ci >= uint(NUM_CLUSTERS)) return;

    // Decode the cluster's (x, y, z) grid coordinate.
    uint x = ci % uint(CLUSTER_X);
    uint y = (ci / uint(CLUSTER_X)) % uint(CLUSTER_Y);
    uint z = ci / uint(CLUSTER_X * CLUSTER_Y);

    // Screen-tile corner rays (view space, from the eye).
    vec2 tileSize = u_camera.viewport / vec2(CLUSTER_X, CLUSTER_Y);
    vec3 minRay = screenToView(vec2(x,      y)      * tileSize);
    vec3 maxRay = screenToView(vec2(x + 1u, y + 1u) * tileSize);

    // Exponential depth slice: view-space near/far Z of this slice (negative).
    float zNearV = -sliceToViewDepth(float(z),      u_camera.zNear, u_camera.zFar, float(CLUSTER_Z));
    float zFarV  = -sliceToViewDepth(float(z + 1u), u_camera.zNear, u_camera.zFar, float(CLUSTER_Z));

    // AABB over the tile's two opposite corner rays at the slice's near + far
    // planes; on a plane of constant depth the other two corners lie between them.
    vec3 p0 = zPlaneIntersect(minRay, zNearV);
    vec3 p1 = zPlaneIntersect(minRay, zFarV);
    vec3 p2 = zPlaneIntersect(maxRay, zNearV);
    vec3 p3 = zPlaneIntersect(maxRay, zFarV);
    vec3 mn = min(min(p0, p1), min(p2, p3));
    vec3 mx = max(max(p0, p1), max(p2, p3));

    vec3  centre = 0.5 * (mn + mx);
    float bound  = 0.5 * length(mx - mn);

    uint count = 0u;
    for (int i = 0; i < u_lights.lightCount && i < MAX_LIGHTS; ++i) {
        Light L = u_lights.lights[i];
        int   type = int(L.position.w);
        bool inside;
        if (type == LIGHT_DIRECTIONAL) {
            inside = true;  // no position/range: affects the whole frustum
        } else {
            vec3  posV   = (u_camera.view * vec4(L.position.xyz, 1.0)).xyz;
            float radius = L.direction.w;
            inside = sqDistPointAABB(posV, mn, mx) <= radius * radius;
            if (inside && type == LIGHT_SPOT) {
                // spot.xy is the cone's scale and offset, -cos(outer) * scale.
                float cosA = clamp(-L.spot.y / max(L.spot.x, 1e-6), -1.0, 1.0);
                vec3  axis = normalize(mat3(u_camera.view) * L.direction.xyz);
                inside = !coneMissesSphere(posV, axis, cosA, sqrt(1.0 - cosA * cosA), radius, centre, bound);
            }
        }
        if (inside && count < uint(MAX_LIGHTS_PER_CLUSTER)) {
            u_clusters.clusters[ci].indices[count] = uint(i);
            ++count;
        }
    }
    u_clusters.clusters[ci].count = count;
}
