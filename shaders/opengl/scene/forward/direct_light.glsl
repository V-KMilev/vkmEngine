/*
 * The direct lights on a forward fragment: each with its attenuation and its shadow, baked or live,
 * reflected by the lobes of the program that includes this (lightReflected).
 */

#include "../../include/lights.glsl"
#include "../../include/cluster_grid.glsl"
#include "../../include/shadows.glsl"
#include "../../include/lightmap.glsl"
#include "shading.glsl"

uniform int u_useClusters;  // 1 = per-cluster light list; 0 = every light, for an eye without clusters

// One light's reflected radiance toward the viewer, arriving along @p L with @p radiance, by the
// program's lobes. @p sourceTan is the tan of the light's angular radius (0 for a point); a sphere
// light's lobe is toward @p Ls, renormalized by @p sphereNorm (sphereLobe).
vec3 lightReflected(Shading sh, Surface s, vec3 L, vec3 radiance, float sourceTan, vec3 Ls, float sphereNorm);

// A point or spot of radius r is a sphere (Karis 2013): its lobe points at the sphere's point
// nearest the mirror ray (@p Ls, else L), renormalized by the (a / a')^2 returned, a' = a + r / 2d
// (lighting.md).
float sphereLobe(Shading sh, Surface s, Light light, inout vec3 Ls) {
    float sphereR = light.bake.z;
    if (sphereR <= 0.0) return 1.0;
    vec3  toLight = light.position.xyz - sh.P;
    vec3  R       = reflect(-sh.V, sh.N);
    vec3  toRay   = dot(toLight, R) * R - toLight;
    vec3  closest = toLight + toRay * clamp(sphereR / max(length(toRay), 1e-4), 0.0, 1.0);
    Ls = normalize(closest);
    float a     = max(s.roughness * s.roughness, 1e-3);
    float aWide = clamp(a + 0.5 * sphereR / max(length(toLight), 1e-4), 0.0, 1.0);
    return (a / aWide) * (a / aWide);
}

// Every light of the fragment's cluster - or every light, for a capture without clusters - on
// @p s. @p count is how many were listed. Only a surface @p litFromBehind takes a light behind it;
// the others skip one before its shadow.
vec3 directLight(Shading sh, Surface s, bool litFromBehind, out uint count) {
    // The fixed lights' baked shadows here, a channel each, read once for every light; the texels
    // a pixel spans are taken outside the branch, where derivatives hold.
#ifdef BAKED
    const bool lightmapped    = sh.lightmapPage >= 0;
    const vec2 lightmapTexels = fwidth(sh.lightmapUV) * vec2(textureSize(u_lightmapMask, 0).xy);
    vec4       bakedShadows   = vec4(1.0);
    if (lightmapped) bakedShadows = lightmapShadows(sh.lightmapUV, sh.lightmapPage, lightmapTexels);
#endif

    int ci = 0;
    if (u_useClusters == 1) {
        vec2  clusterUV    = gl_FragCoord.xy / u_camera.viewport;
        float clusterDepth = linearizeViewDepth(gl_FragCoord.z, u_camera.invProjection);
        // Its lights are where it is, not where its own projection drew it.
        if (sh.viewmodel) {
            clusterUV    = screenUvOf(sh.P);
            clusterDepth = viewDepthOf(sh.P);
        }
        ci = clusterIndex(clusterUV, clusterDepth, u_camera.zNear, u_camera.zFar);
        count = u_clusters.clusters[ci].count;
    } else {
        count = uint(min(u_lights.lightCount, MAX_LIGHTS));
    }

    vec3 Lo = vec3(0.0);
    for (uint k = 0u; k < count; ++k) {
        uint  li    = (u_useClusters == 1) ? u_clusters.clusters[ci].indices[k] : k;
        Light light = u_lights.lights[li];
        int   type  = int(light.position.w);

        // Without clusters, a light out of its radius is passed over here, as a cluster leaves it out.
        if (u_useClusters == 0 && type != LIGHT_DIRECTIONAL) {
            vec3 away = sh.P - light.position.xyz;
            if (dot(away, away) > light.direction.w * light.direction.w) continue;
        }

        vec3  L;
        float atten = punctualAttenuation(light, type, sh.P, L);
        if (atten <= 0.0) continue;
        if (!litFromBehind && dot(sh.N, L) <= 0.0) continue;

        // The bias follows the geometric surface. On a fixed mesh a fixed light's shadow is baked; where
        // the bake saw lit the live map draws what moved, and the sun's thin penumbra stays live.
        const float ndotlGeo   = dot(sh.Ng, L);
        bool        live       = true;
        float       sunBeyond  = 1.0;
        float       visibility = 1.0;
#ifdef BAKED
        const int maskChannel = int(light.bake.x);
        if (lightmapped && maskChannel >= 0) {
            const float baked = bakedShadows[maskChannel];
            // The cascaded sun's alone: another fixed directional light has no live map to draw a
            // penumbra with.
            const bool sun = type == LIGHT_DIRECTIONAL && int(light.spot.w) >= 0;
            if (baked < BAKED_LIT && sun && baked > BAKED_DARK) {
                sunBeyond = baked;
            } else if (baked < BAKED_LIT) {
                live       = false;
                visibility = baked;
            }
        }
#endif
        if (live) {
            visibility = surfaceShadow(
                shadowTypeOf(light, type),
                int(light.spot.w),
                sh.P,
                sh.Ng,
                ndotlGeo,
                sunBeyond
            );
        }

        // Micro-shadowing (Chan): the material's own AO is the cone a cavity
        // stays open over, so a grazing light leaves mortar and creases dark rather than flat.
        // Front-lit only; the back-lights through it are the subsurface's and transmission's.
        float NoLs = dot(sh.N, L);
        if (s.ao < 1.0 && NoLs > 0.0) {
            float micro = clamp(NoLs * inversesqrt(1.0 - min(s.ao, 0.9999)), 0.0, 1.0);
            visibility *= micro * micro;
        }

        vec3  Ls         = L;
        float sphereNorm = sphereLobe(sh, s, light, Ls);

        // spot.z: tan of a directional's disc, 0 otherwise.
        vec3 radiance = light.color.xyz * light.color.w * atten * visibility;
        Lo += lightReflected(sh, s, L, radiance, light.spot.z, Ls, sphereNorm);
    }
    return Lo;
}
