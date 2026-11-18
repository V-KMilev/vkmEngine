/**
 * Froxel fog - light injection.
 *
 * One invocation per froxel: height-falloff density, with the froxel's cluster lights scattered
 * through it (Henyey-Greenstein). Writes in-scattered light (rgb) + extinction (a) for integration.
 *
 * Every shadowed light casts through its shadow, so light shafts fall out of the integration.
 * The environment scatters in too, so fog in shade or indoors is lit as a wall there is, not black.
 */

#include "../../../include/constants.glsl"
#include "../../../include/depth.glsl"
#include "../../../include/lights.glsl"
#include "../../../include/cluster_grid.glsl"
#include "../../../include/shadows.glsl"  // pointShadow
#include "../../../include/camera.glsl"
#include "../../../include/ambient.glsl"  // environmentIrradiance

layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE, local_size_z = 1) in;

layout(binding = 0, rgba16f) uniform writeonly image3D u_scatter;

uniform float u_fogDepth;  // view depth the slices reach: maxDistance, or the far plane if nearer

// Fog medium.
uniform float u_density;
uniform float u_height;
uniform float u_heightFalloff;
uniform float u_anisotropy;
uniform vec3  u_albedo;

// Henyey-Greenstein phase for scattering angle cosine @p cosT, asymmetry @p g.
float phaseHG(float cosT, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(1.0 + g2 - 2.0 * g * cosT, 1.5));
}

// The light's phase: the authored Henyey-Greenstein lobe with a share scattered back, so
// sunlit fog keeps its light with the sun behind the eye.
const float BACK_G     = -0.3;
const float BACK_SHARE = 0.2;

void main() {
    ivec3 dims   = imageSize(u_scatter);
    ivec3 froxel = ivec3(gl_GlobalInvocationID);
    if (froxel.x >= dims.x || froxel.y >= dims.y || froxel.z >= dims.z) return;

    // Froxel center -> view space -> world.
    vec2  uv    = (vec2(froxel.xy) + 0.5) / vec2(float(dims.x), float(dims.y));
    float depth = sliceToViewDepth(float(froxel.z) + 0.5, u_camera.zNear, u_fogDepth, float(dims.z));
    vec3  viewP  = viewPointOnRay(nearPlaneAt(uv), depth);
    vec3  worldP = (u_camera.invView * vec4(viewP, 1.0)).xyz;

    float density = u_density * exp(-max(worldP.y - u_height, 0.0) * u_heightFalloff);

    vec3 V  = toViewer(worldP);
    // The cluster under the froxel's center: on a froxel grid that is not a multiple of the
    // cluster grid, a froxel's corner can sit in the cluster beside the one it mostly covers.
    int  ci = clusterIndex(uv, depth, u_camera.zNear, u_camera.zFar);
    uint n  = u_clusters.clusters[ci].count;

    vec3 inScatter = vec3(0.0);
    for (uint k = 0u; k < n; ++k) {
        Light light = u_lights.lights[u_clusters.clusters[ci].indices[k]];

        int   type  = int(light.position.w);
        vec3  L;
        float atten = light.color.w * punctualAttenuation(light, type, worldP, L);
        if (atten <= 0.0) continue;
        // spot.w is a directional's cascade base or a light's shadow slot. Every shadowed light is
        // shadowed here, or a lamp scatters through the wall in front of it.
        atten *= pointShadow(shadowTypeOf(light, type), int(light.spot.w), worldP);
        if (atten <= 0.0) continue;
        // Phase angle between the photon's travel (-L) and the scatter toward the eye (V), so
        // forward scattering peaks looking into the light.
        float cosT = -dot(V, L);
        float phase = mix(phaseHG(cosT, u_anisotropy), phaseHG(cosT, BACK_G), BACK_SHARE);
        inScatter += light.color.xyz * atten * phase;
    }

    // The environment, scattered once: the phase's first moment over the cosine lobe's (2/3)
    // is how far the lobe facing away from the eye stands in for the phase-weighted sphere,
    // toward the sphere's mean as the phase nears isotropic.
    vec3  up      = environmentIrradiance(worldP, vec3(0.0, 1.0, 0.0));
    vec3  down    = environmentIrradiance(worldP, vec3(0.0, -1.0, 0.0));
    vec3  around  = 0.5 * (up + down);
    float forward = clamp(1.5 * mix(u_anisotropy, BACK_G, BACK_SHARE), 0.0, 1.0);
    inScatter += mix(around, environmentIrradiance(worldP, -V), forward);

    // Held to a half float's range: one infinite froxel would poison the column behind it.
    vec3 scattering = min(u_albedo * density * inScatter, vec3(HALF_MAX));
    imageStore(u_scatter, froxel, vec4(scattering, density));
}
