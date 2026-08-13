/**
 * Froxel fog - light injection.
 *
 * One invocation per froxel: reconstruct its world position, evaluate the
 * height-falloff medium density, and scatter the froxel's cluster lights through
 * it (Henyey-Greenstein phase). Writes in-scattered light (rgb) + extinction (a)
 * into the scatter volume for the integration pass to march.
 *
 * The sun is sampled against the same CSM cascades that shadow the geometry, so
 * shadowed volume goes dark and light shafts fall out of the integration.
 */

#include "../../_generated/engine_config.glsl"  // CLUSTER_*, MAX_LIGHTS, MAX_LIGHTS_PER_CLUSTER
#include "../../_common/depth.glsl"
#include "../../_common/lights.glsl"            // Light + LightsBlock + cluster grid + LIGHT_* + falloff
#include "../../_common/shadows.glsl"           // ShadowBlock + sampleCSM: the sun's cascades

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(binding = 0, rgba16f) uniform writeonly image3D u_scatter;

uniform mat4  u_invView;        // view -> world
uniform mat4  u_invProjection;  // clip -> view
uniform vec3  u_cameraPos;
uniform float u_zNear;
uniform float u_zFar;
uniform ivec3 u_froxelDims;     // froxel grid resolution (runtime; fog quality)

// Fog medium.
uniform float u_density;
uniform float u_height;
uniform float u_heightFalloff;
uniform float u_anisotropy;
uniform vec3  u_albedo;

#include "../../_common/constants.glsl"

// Henyey-Greenstein phase for scattering angle cosine @p cosT, asymmetry @p g.
float phaseHG(float cosT, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosT, 1.5));
}

// Which light cluster a froxel (screen tile + depth) maps to.
int clusterOf(uvec2 tile, float viewDepth) {
    uint cx = uint(float(tile.x) / float(u_froxelDims.x) * float(CLUSTER_X));
    uint cy = uint(float(tile.y) / float(u_froxelDims.y) * float(CLUSTER_Y));
    float slice = viewDepthToSlice(viewDepth, u_zNear, u_zFar, float(CLUSTER_Z));
    uint cz = uint(clamp(floor(slice), 0.0, float(CLUSTER_Z - 1)));
    return int(cx + cy * uint(CLUSTER_X) + cz * uint(CLUSTER_X * CLUSTER_Y));
}

void main() {
    ivec3 froxel = ivec3(gl_GlobalInvocationID);
    if (froxel.x >= u_froxelDims.x || froxel.y >= u_froxelDims.y || froxel.z >= u_froxelDims.z) return;

    // Froxel centre -> view space -> world.
    vec2  uv    = (vec2(froxel.xy) + 0.5) / vec2(float(u_froxelDims.x), float(u_froxelDims.y));
    float depth = sliceToViewDepth(float(froxel.z) + 0.5, u_zNear, u_zFar, float(u_froxelDims.z));
    vec4  clip  = vec4(uv * 2.0 - 1.0, -1.0, 1.0);
    vec4  nearH = u_invProjection * clip;
    vec3  ray   = nearH.xyz / nearH.w;                   // near-plane point (the perspective divide matters)
    vec3  viewP = ray * (depth / u_zNear);               // scaled to this slice's depth
    vec3  worldP = (u_invView * vec4(viewP, 1.0)).xyz;

    // Height-falloff medium.
    float density = u_density * exp(-max(worldP.y - u_height, 0.0) * u_heightFalloff);

    // Scatter the froxel's cluster lights.
    vec3 V  = normalize(u_cameraPos - worldP);
    int  ci = clusterOf(uvec2(froxel.xy), depth);
    uint n  = u_clusters.clusters[ci].count;

    vec3 inScatter = vec3(0.0);
    for (uint k = 0u; k < n; ++k) {
        Light light = u_lights.lights[u_clusters.clusters[ci].indices[k]];

        vec3  L;
        float atten = light.color.w;  // intensity
        if (int(light.position.w) == LIGHT_DIRECTIONAL) {
            L = normalize(-light.direction.xyz);
            // A set sun stops lighting the volume: fade across the horizon band
            // (matching the sky's sun-disc fade) so dusk doesn't pop and no
            // phantom glow hangs in the sky opposite a below-horizon sun.
            atten *= smoothstep(-0.05, 0.05, L.y);
            if (atten <= 0.0) continue;
            // Shadow the sun through its cascades. N = 0 skips the normal-offset
            // bias, which is meaningless for a volume sample (no surface).
            atten *= sampleCSM(worldP, vec3(0.0), 1.0, u_cameraPos);
            if (atten <= 0.0) continue;
        } else {
            vec3  toL  = light.position.xyz - worldP;
            float d2   = dot(toL, toL);
            float r    = light.direction.w;
            if (d2 > r * r) continue;                    // out of range
            float dist = sqrt(d2);
            L      = toL / dist;
            // The same windowed inverse-square the surface lighting uses, so a
            // light's fog glow fades exactly like the light itself.
            atten *= distanceAttenuation(dist, r);
        }
        // The phase angle is between the photon's travel direction (-L) and the
        // scatter direction toward the camera (V): -dot(V, L). Forward
        // scattering then peaks when looking into the light, not away from it.
        inScatter += light.color.xyz * atten * phaseHG(-dot(V, L), u_anisotropy);
    }

    vec3 scattering = u_albedo * density * inScatter;
    imageStore(u_scatter, froxel, vec4(scattering, density));
}
