/**
 * Froxel fog - light injection.
 *
 * One invocation per froxel: height-falloff density, with the froxel's cluster lights scattered
 * through it (Henyey-Greenstein). Writes in-scattered light (rgb) + extinction (a) for integration.
 *
 * The sun is shadowed by the geometry's CSM cascades, so light shafts fall out of the integration.
 * The environment scatters in too, so fog in shade or indoors is lit as a wall there is, not black.
 */

#include "../../constants.glsl"
#include "../../depth.glsl"
#include "../../lights.glsl"
#include "../../shadows.glsl"  // ShadowBlock + sampleCSM
#include "../../camera.glsl"
#include "../../ambient.glsl"  // environmentIrradiance

layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE, local_size_z = 1) in;

layout(binding = 0, rgba16f) uniform writeonly image3D u_scatter;

uniform ivec3 u_froxelDims;  // froxel grid resolution (runtime; fog quality)
uniform float u_fogDepth;    // view depth the slices reach: maxDistance, or the far plane if nearer

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

void main() {
    ivec3 froxel = ivec3(gl_GlobalInvocationID);
    if (froxel.x >= u_froxelDims.x || froxel.y >= u_froxelDims.y || froxel.z >= u_froxelDims.z) return;

    // Froxel centre -> view space -> world.
    vec2  uv    = (vec2(froxel.xy) + 0.5) / vec2(float(u_froxelDims.x), float(u_froxelDims.y));
    float depth = sliceToViewDepth(float(froxel.z) + 0.5, u_camera.zNear, u_fogDepth, float(u_froxelDims.z));
    vec4  clip  = vec4(uv * 2.0 - 1.0, -1.0, 1.0);
    vec4  nearH = u_camera.invProjection * clip;
    vec3  ray   = nearH.xyz / nearH.w;  // near-plane point (the perspective divide matters)
    vec3  viewP = cameraIsPerspective()
        ? ray * (depth / u_camera.zNear)  // scaled to this slice's depth
        : vec3(ray.xy, -depth);           // an orthographic ray does not spread
    vec3  worldP = (u_camera.invView * vec4(viewP, 1.0)).xyz;

    float density = u_density * exp(-max(worldP.y - u_height, 0.0) * u_heightFalloff);

    vec3 V  = normalize(u_camera.cameraPosition.xyz - worldP);
    // The cluster under the froxel's centre: on a froxel grid that is not a multiple of the
    // cluster grid, a froxel's corner can sit in the cluster beside the one it mostly covers.
    int  ci = clusterIndex(uv, depth, u_camera.zNear, u_camera.zFar);
    uint n  = u_clusters.clusters[ci].count;

    vec3 inScatter = vec3(0.0);
    for (uint k = 0u; k < n; ++k) {
        Light light = u_lights.lights[u_clusters.clusters[ci].indices[k]];

        // Area lights scatter from their centre, as a point would.
        int   type    = int(light.position.w);
        int   falloff = (type == LIGHT_RECT || type == LIGHT_DISK) ? LIGHT_POINT : type;
        vec3  L;
        float atten = light.color.w * punctualAttenuation(light, falloff, worldP, L);
        if (atten <= 0.0) continue;
        // spot.w is a directional's cascade base, -1 (no cascades) scatters unshadowed.
        // N = 0 skips the normal-offset bias: a volume has no surface to offset.
        if (type == LIGHT_DIRECTIONAL && int(light.spot.w) >= 0) {
            atten *= sampleCSM(worldP, vec3(0.0), 1.0);
            if (atten <= 0.0) continue;
        }
        // Phase angle between the photon's travel (-L) and the scatter toward the eye (V), so
        // forward scattering peaks looking into the light.
        inScatter += light.color.xyz * atten * phaseHG(-dot(V, L), u_anisotropy);
    }

    // The environment, scattered once: forward scattering brings most of it from beyond the
    // froxel, so the lobe facing away from the eye stands in for the phase-weighted sphere.
    inScatter += environmentIrradiance(worldP, -V);

    vec3 scattering = u_albedo * density * inScatter;
    imageStore(u_scatter, froxel, vec4(scattering, density));
}
