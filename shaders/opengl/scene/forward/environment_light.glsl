/*
 * The environment's light on a forward fragment - the sky, probes and irradiance volume - through
 * GTAO and the material's occlusion, under the clear coat and sheen its lobes have (lobes.glsl);
 * and the reflection pass's inputs it writes. Split-sum IBL, gated by u_hasIBL (ambient.glsl), so
 * an unbaked frame falls back to flat ambient.
 */

#include "../../include/ambient.glsl"
#include "../../include/scene_ao.glsl"
#include "../../include/lightmap.glsl"
#include "output.glsl"
#include "lobes.glsl"

layout(binding = IBL_SLOT_PREFILTER) uniform samplerCube u_prefilter;  // roughness-prefiltered specular

// The diffuse light @p irradiance gives surface @p s, @p kD its share once the specular has taken
// its own: the program's.
vec3 environmentDiffuse(Surface s, vec3 irradiance, vec3 kD);

// The environment's light on @p s. Writes the reflection inputs (ReflectWeight, ReflectEnv);
// @p specularPart is what of the result is reflected, @p FssEss the single-scatter energy a trace
// can replace.
vec3 environmentLight(Shading sh, Surface s, vec2 dfg, out vec3 specularPart, out vec3 FssEss) {
    vec3 N  = sh.N;
    vec3 Ng = sh.Ng;
    vec3 V  = sh.V;

    // Irradiance follows GTAO's bent normal so creases do not over-collect. The
    // bend is from the geometric normal, so it carries over as an offset.
    vec2  screenUv = gl_FragCoord.xy / u_camera.viewport;
    float gtao     = aoAt(screenUv);
    vec3  bentN    = N;
    if (u_hasAO == 1) {
        vec3 bentWorld = normalize(mat3(u_camera.invView) * bentNormalAt(screenUv));
        bentN = normalize(N + (bentWorld - Ng));
    }
    // Both measure the same occlusion, so the stronger wins, not the product.
    float ao = min(s.ao, gtao);

    vec3 R = reflect(-V, N);

    // What the environment's specular takes, and the diffuse what is left (Fdez-Aguera, section 4).
    vec3 FmsEms;
    environmentEnergy(dfg, sh.f0, FssEss, FmsEms);
    vec3 kD = (1.0 - (FssEss + FmsEms)) * (1.0 - s.metallic);

    // Horizon occlusion drops reflections a normal map turns below the geometric surface.
    float horizon = min(1.0 + dot(R, Ng), 1.0);

    // Where a rough lobe peaks: GGX's dominant direction bends from R toward N as roughness
    // grows (Lagarde and de Rousiers, 4.9.3), so a rough floor at grazing does not
    // reflect the bright horizon R points at. The coat reflects off its smooth top (Rc).
    float rdA = s.roughness * s.roughness;
    vec3  Rd  = normalize(mix(N, R, (1.0 - rdA) * (sqrt(1.0 - rdA) + rdA)));

    // The single-scatter half, dimmed by a clear coat's Fresnel and a sheen's share, plus the coat's
    // own mirror, each by its share of the one trace (traceShare): traced at the sharper lobe, a
    // coated surface shows no probe reflection over the traced one, and a rough base under a
    // sharp coat keeps its own blur. Not occluded: a trace that hits has found the occluder
    // itself, so only the environment it replaces (ReflectEnv) carries the occlusion.
    vec3  reflectWeight = FssEss;
    float underSheen    = hasSheen() ? 1.0 - sheenTaken(sh.NdotV) : 1.0;
    bool  hasCoat       = coated(s);
    float coatNoV       = max(dot(Ng, V), 1e-4);
    float coatFresnel   = hasCoat ? fresnelSchlick(coatNoV, vec3(COAT_F0)).x * s.clearcoat : 0.0;
    vec3  Rc            = reflect(-V, Ng);
    float traceLobe  = hasCoat ? min(s.roughness, s.clearcoatRoughness) : s.roughness;
    reflectWeight *= (1.0 - coatFresnel) * underSheen * traceShare(s.roughness, traceLobe);
    float coatWeight = coatFresnel * traceShare(s.clearcoatRoughness, traceLobe);
    ReflectWeight = vec4(reflectWeight + vec3(coatWeight), traceLobe);

    // Without a baked sky: no reflection and a flat floor, which probes and the
    // irradiance volume still replace where they cover.
    float ccRough          = s.clearcoatRoughness;
    vec3  prefiltered      = vec3(0.0);
    vec3  coat             = vec3(0.0);
    vec3  sourceIrradiance = vec3(FLAT_AMBIENT);
    if (u_hasIBL == 1) {
        prefiltered      = textureLod(u_prefilter, Rd, s.roughness * MAX_REFLECTION_LOD).rgb;
        coat             = hasCoat ? textureLod(u_prefilter, Rc, ccRough * MAX_REFLECTION_LOD).rgb : vec3(0);
        sourceIrradiance = texture(u_irradiance, bentN).rgb;
    }

    // Probes blended over the sky before shading, so one shading serves all (probes.glsl): the
    // reflection as well as the irradiance, so the loop is this pass's own.
    float covered  = 0.0;
    float probeSky = 0.0;  // the sky the covering probes' centers see, by their shares
    if (u_probeCount > 0) {
        vec3  prefilteredSum = vec3(0.0);
        vec3  coatSum        = vec3(0.0);
        vec3  irradianceSum  = vec3(0.0);
        for (int p = 0; p < u_probeCount && p < MAX_PROBES && covered < 0.999; ++p) {
            float w = probeShare(p, sh.P, covered);
            if (w <= 0.0) continue;
            vec3  center  = u_probes.probes[p].center.xyz;
            vec3  extents = u_probes.probes[p].extents.xyz;
            float scaled  = w * u_probes.probes[p].params.y;
            float layer   = u_probes.probes[p].params.z;
            vec4  RpLayer = vec4(probeParallax(Rd, sh.P, center, extents), layer);
            prefilteredSum += textureLod(u_probePref, RpLayer, s.roughness * MAX_PROBE_LOD).rgb * scaled;
            if (hasCoat) {
                vec4 coatLayer = vec4(probeParallax(Rc, sh.P, center, extents), layer);
                coatSum += textureLod(u_probePref, coatLayer, ccRough * MAX_PROBE_LOD).rgb * scaled;
            }
            irradianceSum += texture(u_probeIrr, vec4(bentN, layer)).rgb * scaled;
            probeSky      += u_probes.probes[p].params.w * w;
        }
        prefiltered      = prefilteredSum + prefiltered * (1.0 - covered);
        coat             = coatSum + coat * (1.0 - covered);
        sourceIrradiance = irradianceSum + sourceIrradiance * (1.0 - covered);
    }
    // Scaled where the environment is read, so a traced reflection is not.
    prefiltered      *= u_iblIntensity;
    coat             *= u_iblIntensity;
    sourceIrradiance *= u_iblIntensity;

    // The volume, sampled here rather than at a probe's center or infinity, where it covers.
    vec3  irradiance = sourceIrradiance;
    float inVolume   = 0.0;
    if (u_hasIrradianceVolume == 1) {
        // Weighted where it is read, so a box fitted to a room's inside covers its walls.
        const vec3 lookup = irradianceVolumeLookup(sh.P, Ng);
        irradiance = blendIrradianceVolume(irradiance, lookup, bentN, u_iblIntensity);
        inVolume   = irradianceVolumeWeight(lookup);
    }
#ifdef BAKED
    // The sky a fixed surface sees, baked, occludes as GTAO does: the stronger wins, GTAO adding what
    // moves. A probe's capture already holds the sky its center sees, so under probes the bake takes
    // off only the sky they see and the texel does not; the volume's grid holds what its box shuts
    // out, so the bake gives way by its weight.
    if (sh.lightmapPage >= 0) {
        const float texelSky = lightmapSky(sh.lightmapUV, sh.lightmapPage);
        const float seen     = covered > 0.0 ? probeSky / covered : 0.0;
        const float baked    = mix(texelSky, clamp(1.0 - seen + texelSky, 0.0, 1.0), covered);
        ao = min(ao, mix(baked, 1.0, inVolume));
    }
#endif
    float specOcc = specularOcclusion(sh.NdotV, ao, s.roughness * s.roughness) * horizon * horizon;
    // The coat's top is never below its own horizon, so it takes no horizon term.
    float coatOcc = specularOcclusion(coatNoV, ao, s.clearcoatRoughness * s.clearcoatRoughness);

    // Reflection normalization (Lazarov): a capture lit more than this point is
    // dimmed by the irradiance ratio, never brightened (the sun is not in it).
    float normalization = min(luma(irradiance) / max(luma(sourceIrradiance), 1e-4), 1.0);
    prefiltered *= normalization;
    coat        *= normalization;

    vec3 diffuseIBL  = environmentDiffuse(s, irradiance, kD);
    vec3 specularIBL = prefiltered * FssEss + FmsEms * irradiance;

    vec3 ambient = diffuseIBL * multiBounceOcclusion(ao, s.albedo) + specularIBL * specOcc;
    specularPart = specularIBL * specOcc;

    // A sheen takes its share of the environment before the base does: its directional albedo of
    // the irradiance, since a lobe as broad as cloth's gathers nearly what a diffuse one does.
    if (hasSheen()) {
        vec3 sheenIBL = u_material.sheenColor.rgb * sheenAlbedo(sh.NdotV) * irradiance * ao;
        ambient      = ambient * underSheen + sheenIBL;
        specularPart = specularPart * underSheen;
    }

    // The coat layer dims everything beneath by its Fresnel.
    if (hasCoat) {
        ambient      = ambient * (1.0 - coatFresnel) + coat * (coatFresnel * coatOcc);
        specularPart = specularPart * (1.0 - coatFresnel) + coat * (coatFresnel * coatOcc);
    }

    // Zero where nothing was read, so a trace there only adds.
    ReflectEnv = vec4(reflectWeight * specOcc * prefiltered + coat * (coatWeight * coatOcc), 1.0);
    return ambient;
}
