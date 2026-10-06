/*
 * Forward PBR ubershader for the full MaterialAsset spec: metal-rough GGX base
 * (optionally anisotropic), clearcoat, sheen, subsurface, thin transmission,
 * POM, alpha mask and unlit; probes over the IBL. The reflection's weight and
 * radiance go beside the colour for the screen-space resolve
 * (reflection/resolve/fragment.shader). Each fragment, transparent ones too, is
 * fogged here at its own depth and never again; shading-split debug views are not.
 *
 * Optional lobes branch at runtime on the material UBO: one program for all.
 */

#include "../../constants.glsl"
#include "../../brdf.glsl"
#include "../../ltc.glsl"
#include "../../lights.glsl"
#include "../../depth.glsl"
#include "../../normal_codec.glsl"
#include "../../color.glsl"

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vTangent;
in float vHandedness;

layout(location = OUT_COLOR) out vec4 FragColor;

// The reflection pass's inputs (GLTarget, Layout::Scene). The product is stored,
// not the radiance, so the pass subtracts exactly what was added whatever the
// weight's quantising. A pixel reflecting nothing writes zero; alpha is what a
// transparent surface blends by.
// rgb: split-sum weight x specular occlusion; a: roughness.
layout(location = OUT_REFLECT_WEIGHT) out vec4 ReflectWeight;
layout(location = OUT_REFLECT_ENV) out vec4 ReflectEnv;  // rgb: that weight x the probe / sky radiance

#include "../../material.glsl"

#include "../../camera.glsl"
#include "../../fog.glsl"

#include "../../shadows.glsl"

layout(binding = MATERIAL_SLOT_ALBEDO) uniform sampler2D u_albedoTexture;
layout(binding = MATERIAL_SLOT_NORMAL) uniform sampler2D u_normalTexture;
layout(binding = MATERIAL_SLOT_METALLIC_ROUGHNESS) uniform sampler2D u_metallicRoughnessTexture;
layout(binding = MATERIAL_SLOT_AO) uniform sampler2D u_aoTexture;
layout(binding = MATERIAL_SLOT_EMISSION) uniform sampler2D u_emissionTexture;
layout(binding = MATERIAL_SLOT_HEIGHT) uniform sampler2D u_heightTexture;
layout(binding = MATERIAL_SLOT_CLEARCOAT) uniform sampler2D u_clearcoatTexture;
layout(binding = MATERIAL_SLOT_TRANSMISSION) uniform sampler2D u_transmissionTexture;
layout(binding = MATERIAL_SLOT_METALLIC) uniform sampler2D u_metallicTexture;
layout(binding = MATERIAL_SLOT_ROUGHNESS) uniform sampler2D u_roughnessTexture;
layout(binding = MATERIAL_SLOT_AO_METALLIC_ROUGHNESS) uniform sampler2D u_aoMetallicRoughnessTexture;

// Split-sum IBL, gated by u_hasIBL (ambient.glsl) so an unbaked frame falls
// back to flat ambient.
layout(binding = IBL_SLOT_PREFILTER)  uniform samplerCube u_prefilter;   // roughness-prefiltered specular
layout(binding = IBL_SLOT_BRDF_LUT)   uniform sampler2D   u_brdfLUT;     // split-sum BRDF/DFG LUT
uniform int u_renderMode;  // MODE_* debug view; 0 everywhere but the main view
uniform int u_wirePass;    // 1 on the Wireframe view's second draw, of its lines alone

const vec3  WIRE_COLOR = vec3(0.0);
const float WIRE_ALPHA = 0.55;

// Opaque + sky copy for transmission refraction, transparent bucket only.
layout(binding = POST_SLOT_SCENE_COLOR) uniform sampler2D u_sceneColor;
uniform int u_hasSceneColor;
uniform int u_useClusters;      // 1 = per-cluster light list, 0 = full list (GLSceneCapture)
uniform int u_alphaToCoverage;  // 1 while alpha-to-coverage is on for a cutout

// GTAO, for the indirect term. u_hasAO is set for the opaque bucket alone:
// GTAO saw only what the prepass drew.
layout(binding = POST_SLOT_AO) uniform sampler2D u_ao;
uniform int u_hasAO;

#include "../../ambient.glsl"

// Local reflection probes, a cube-array layer each. Weight-blended over the IBL
// for the reflection, and for the diffuse only where no irradiance volume covers.
layout(binding = PROBE_SLOT_IRRADIANCE) uniform samplerCubeArray u_probeIrr;
layout(binding = PROBE_SLOT_PREFILTER)  uniform samplerCubeArray u_probePref;
uniform int u_probeCount;

struct ProbeEntry {
    vec4 center;    // xyz world centre, w pad
    vec4 extents;   // xyz half-extents, w pad
    vec4 params;    // x falloff, y intensity, z layer, w pad
};
layout(std140, binding = UBO_PROBES) uniform ProbeBlock {
    ProbeEntry probes[MAX_PROBES];
} u_probes;

// Parallax box correction: the direction from the box centre to where R leaves
// the box, so a cube captured at one point does not reflect as if at infinity.
vec3 probeParallax(vec3 R, vec3 worldPos, vec3 center, vec3 extents) {
    vec3  boxMin = center - extents;
    vec3  boxMax = center + extents;
    vec3  invR   = 1.0 / R;
    vec3  t1     = (boxMin - worldPos) * invR;
    vec3  t2     = (boxMax - worldPos) * invR;
    vec3  tFar   = max(t1, t2);
    float t      = min(min(tFar.x, tFar.y), tFar.z);
    return (worldPos + R * t) - center;
}

// 1 deep inside, fading to 0 at the face over falloff. Falloff 0 is held a thousandth wide:
// smoothstep's edges must differ.
float probeWeight(vec3 worldPos, vec3 center, vec3 extents, float falloff) {
    vec3  d = abs(worldPos - center) / max(extents, vec3(1e-3));
    float m = max(max(d.x, d.y), d.z);
    return 1.0 - smoothstep(1.0 - max(falloff, 1e-3), 1.0, m);
}

// Depth below the top of the height field at uv, 0..1 (height maps are white
// where high). Explicit derivatives: the marches leave their loops per pixel,
// where implicit ones are undefined.
float heightDepth(vec2 uv, vec2 uvDx, vec2 uvDy) {
    return 1.0 - textureGrad(u_heightTexture, uv, uvDx, uvDy).r;
}

// March the height field along the tangent-space view direction and
// interpolate the crossing. More layers at grazing angles.
vec2 parallax(vec2 uv, vec3 viewDirTS, vec2 uvDx, vec2 uvDy) {
    const float MIN_LAYERS = 8.0;
    const float MAX_LAYERS = 32.0;
    float numLayers = mix(MAX_LAYERS, MIN_LAYERS, clamp(abs(viewDirTS.z), 0.0, 1.0));

    float layerDepth = 1.0 / numLayers;
    float curLayerDepth = 0.0;

    vec2 maxOffset = (viewDirTS.xy / max(viewDirTS.z, 0.001)) * u_material.heightScale;
    vec2 deltaUV = maxOffset / numLayers;

    vec2  curUV = uv;
    float curH  = heightDepth(curUV, uvDx, uvDy);

    for (int i = 0; i < int(MAX_LAYERS); ++i) {
        if (curLayerDepth >= curH) break;
        curUV -= deltaUV;
        curH = heightDepth(curUV, uvDx, uvDy);
        curLayerDepth += layerDepth;
    }

    vec2  prevUV = curUV + deltaUV;
    float afterD  = curH - curLayerDepth;
    float beforeD = heightDepth(prevUV, uvDx, uvDy) - curLayerDepth + layerDepth;
    float w       = afterD / (afterD - beforeD);
    return mix(curUV, prevUV, clamp(w, 0.0, 1.0));
}

// POM self-shadowing from the displaced uv: march toward the light, keeping the
// worst blocker above the ray. 1 = lit, 0 = fully shadowed.
float parallaxShadow(vec2 uv, vec3 lightDirTS, vec2 uvDx, vec2 uvDy) {
    if (lightDirTS.z <= 0.0) return 1.0;

    const float NUM_LAYERS = 16.0;

    float startDepth = heightDepth(uv, uvDx, uvDy);
    if (startDepth <= 0.0) return 1.0;

    // Each step rises deltaDepth and moves along the light's slope.
    float deltaDepth = startDepth / NUM_LAYERS;
    vec2  deltaUV    = (lightDirTS.xy / max(lightDirTS.z, 0.01)) * u_material.heightScale * deltaDepth;

    vec2  curUV    = uv      + deltaUV;
    float curDepth = startDepth - deltaDepth;

    // Worst, not average: any point above the ray casts a hard shadow.
    float maxBlocker = 0.0;
    for (int i = 0; i < int(NUM_LAYERS); ++i) {
        if (curDepth <= 0.0) break;
        float d = heightDepth(curUV, uvDx, uvDy);
        if (d < curDepth) {
            maxBlocker = max(maxBlocker, curDepth - d);
        }
        curUV    += deltaUV;
        curDepth -= deltaDepth;
    }

    return clamp(1.0 - maxBlocker * 8.0, 0.0, 1.0);
}

struct Surface {
    vec3  albedo;
    float opacity;
    float metallic;
    float roughness;
    float ao;
    vec3  emission;
    // Lobe masks, sampled once per fragment for the per-light lobes.
    float clearcoat;
    float transmission;
    float clearcoatRoughness;  // clamped and specular-antialiased like the base's
};

Surface sampleSurface(vec2 uv) {
    Surface s;

    s.albedo  = u_material.albedo.rgb;
    s.opacity = u_material.albedo.a;
    if (hasTex(MATERIAL_SLOT_ALBEDO)) {
        vec4 tex = texture(u_albedoTexture, uv);
        s.albedo  *= tex.rgb;
        s.opacity *= tex.a;
    }

    s.metallic  = u_material.metallic;
    s.roughness = u_material.roughness;
    s.ao        = u_material.ao;
    s.emission  = u_material.emission.rgb * u_material.emission.a;

    // Combined maps win over separate ones when both are present.
    if (hasTex(MATERIAL_SLOT_AO_METALLIC_ROUGHNESS)) {
        vec3 amr = texture(u_aoMetallicRoughnessTexture, uv).rgb;
        s.ao        *= amr.r;
        s.roughness *= amr.g;
        s.metallic  *= amr.b;
    } else if (hasTex(MATERIAL_SLOT_METALLIC_ROUGHNESS)) {
        vec3 mr = texture(u_metallicRoughnessTexture, uv).rgb;
        s.roughness *= mr.g;  // glTF: green = roughness
        s.metallic  *= mr.b;  // glTF: blue  = metallic
    } else {
        if (hasTex(MATERIAL_SLOT_METALLIC))  s.metallic  *= texture(u_metallicTexture,  uv).r;
        if (hasTex(MATERIAL_SLOT_ROUGHNESS)) s.roughness *= texture(u_roughnessTexture, uv).r;
    }

    if (hasTex(MATERIAL_SLOT_AO) && !hasTex(MATERIAL_SLOT_AO_METALLIC_ROUGHNESS)) {
        s.ao *= texture(u_aoTexture, uv).r;
    }
    if (hasTex(MATERIAL_SLOT_EMISSION)) {
        s.emission *= texture(u_emissionTexture, uv).rgb;
    }

    s.clearcoat = u_material.clearcoat;
    if (hasTex(MATERIAL_SLOT_CLEARCOAT)) s.clearcoat *= texture(u_clearcoatTexture, uv).r;

    s.transmission = u_material.transmission;
    if (hasTex(MATERIAL_SLOT_TRANSMISSION)) s.transmission *= texture(u_transmissionTexture, uv).r;

    s.roughness          = clamp(s.roughness, MIN_ROUGHNESS, 1.0);
    s.clearcoatRoughness = clamp(u_material.clearcoatRoughness, MIN_ROUGHNESS, 1.0);
    return s;
}

// The geometric normal, or the normal map through the TBN. Maps hold x and y
// alone (BC5 or RG8), so z is rebuilt.
vec3 getNormal(vec2 uv, vec3 Ng, mat3 tbn) {
    if (!hasTex(MATERIAL_SLOT_NORMAL)) {
        return Ng;
    }
    vec3 n;
    n.xy = texture(u_normalTexture, uv).rg * 2.0 - 1.0;
    n.z  = sqrt(max(0.0, 1.0 - dot(n.xy, n.xy)));
    n.xy *= u_material.normalScale;
    return normalize(tbn * normalize(n));
}

// Geometric specular antialiasing (Karis / Filament): roughness rises with
// screen-space normal variance so normal detail does not shimmer.
float specularAA(vec3 N, float roughness) {
    const float SAA_VARIANCE  = 0.25;
    const float SAA_THRESHOLD = 0.18;
    vec3  dndu     = dFdx(N);
    vec3  dndv     = dFdy(N);
    float variance = SAA_VARIANCE * (dot(dndu, dndu) + dot(dndv, dndv));
    float alpha2   = (roughness * roughness) * (roughness * roughness);
    float kernel   = min(2.0 * variance, SAA_THRESHOLD);
    float a2       = clamp(alpha2 + kernel, 0.0, 1.0);
    return sqrt(sqrt(a2));   // back to perceptual roughness
}

// Charlie sheen distribution + Ashikhmin visibility (KHR_materials_sheen).
float distributionCharlie(float NdotH, float roughness) {
    float invR = 1.0 / max(roughness, 0.07);
    float cos2 = NdotH * NdotH;
    float sin2 = max(1.0 - cos2, 0.0);
    return (2.0 + invR) * pow(sin2, invR * 0.5) / (2.0 * PI);
}

float visAshikhmin(float NdotV, float NdotL) {
    return clamp(1.0 / (4.0 * (NdotL + NdotV - NdotL * NdotV)), 0.0, 1.0);
}

// Anisotropic GGX (Burley / Filament): at, ab are tangent/bitangent alphas.
float distributionGGXAniso(float NdotH, float ToH, float BoH, float at, float ab) {
    float a2 = at * ab;
    vec3  v  = vec3(ab * ToH, at * BoH, a2 * NdotH);
    float v2 = dot(v, v);
    float w2 = a2 / max(v2, 1e-8);
    return a2 * w2 * w2 * (1.0 / PI);
}

float visSmithAniso(
    float at,
    float ab,
    float ToV,
    float BoV,
    float NdotV,
    float ToL,
    float BoL,
    float NdotL
) {
    float lambdaV = NdotL * length(vec3(at * ToV, ab * BoV, NdotV));
    float lambdaL = NdotV * length(vec3(at * ToL, ab * BoL, NdotL));
    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

// A clear coat is a dielectric of index 1.5.
const float COAT_F0 = 0.04;

vec3 fresnelSchlick(float u, vec3 f0) {
    float x  = clamp(1.0 - u, 0.0, 1.0);
    float x2 = x * x;
    float f  = x2 * x2 * x;
    return f0 + (vec3(1.0) - f0) * f;
}

// Unit normal of an area emitter's plane; the clamp guards a zero-extent axis.
vec3 areaPlaneNormal(vec3 axisU, vec3 axisV) {
    vec3 n = cross(axisU, axisV);
    float nLen2 = max(dot(n, n), 1e-12);
    return n / sqrt(nLen2);
}

// Where a ray meets that plane; parallel or receding, the point on the ray
// nearest the centre instead of a hit behind it. The caller clamps onto the emitter.
vec3 areaPlaneHit(vec3 rayOrigin, vec3 rayDir, vec3 center, vec3 n) {
    float denom = dot(rayDir, n);
    float t = (abs(denom) > 1e-4)
        ? dot(center - rayOrigin, n) / denom
        : -1.0;
    if (t <= 0.0) {
        t = max(0.0, dot(center - rayOrigin, rayDir));
    }
    return rayOrigin + rayDir * t;
}

// Representative point (Karis 2013): the emitter's point closest to the
// mirror ray, shaded as a point source with a lobe broadened by the emitter's apparent size.
vec3 areaRectClosestPoint(vec3 rayOrigin, vec3 rayDir, vec3 center, vec3 axisU, vec3 axisV) {
    // U x V points out of the emitting face.
    vec3 hit = areaPlaneHit(rayOrigin, rayDir, center, areaPlaneNormal(axisU, axisV));

    // axisU / axisV are half-extents, so their lengths are the bounds.
    vec3 d = hit - center;
    float uLen = length(axisU);
    float vLen = length(axisV);
    vec3 uNorm = axisU / max(uLen, 1e-4);
    vec3 vNorm = axisV / max(vLen, 1e-4);
    float uCoord = clamp(dot(d, uNorm), -uLen, uLen);
    float vCoord = clamp(dot(d, vNorm), -vLen, vLen);
    return center + uNorm * uCoord + vNorm * vCoord;
}

vec3 areaDiskClosestPoint(vec3 rayOrigin, vec3 rayDir, vec3 center, vec3 axisU, vec3 axisV) {
    // axisU / axisV both have the disk radius as length.
    vec3 n   = areaPlaneNormal(axisU, axisV);
    vec3 hit = areaPlaneHit(rayOrigin, rayDir, center, n);

    // Project the offset into the disk plane and clamp to the radius.
    vec3 d = hit - center;
    d -= n * dot(d, n);
    float radius = length(axisU);
    float dLen   = length(d);
    if (dLen > radius) d *= (radius / dLen);
    return center + d;
}

// Karis (2013) Eq. 16: alpha' = saturate(alpha + r / (3*dist)), r the
// source's representative radius.
float areaBroadenedAlpha(float alpha, float sourceRadius, float dist) {
    return clamp(alpha + sourceRadius / max(3.0 * dist, 1e-4), 0.0, 1.0);
}

// energyCompensation restores multi-bounce energy (Filament's
// 1 + f0 (1 / Ess - 1)), the direct half of environmentSpecular's correction.
// sourceTan is the tan of the light's angular radius (0 for a point); the lobe
// widens over the disc unrenormalised, spreading its irradiance instead of a
// peak a half float may not hold.
vec3 evaluateLight(
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 T,
    vec3 B,
    Surface s,
    vec3 f0,
    vec3 energyCompensation,
    vec3 radiance,
    float sourceTan
) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float a = areaBroadenedAlpha(s.roughness * s.roughness, sourceTan, 1.0);

    // Base specular: anisotropic when configured, isotropic otherwise.
    float D, Vis;
    if (u_material.anisotropy > 0.001) {
        // The authored direction in the shading plane, else T: a zeroed or
        // N-parallel one would normalize to NaN.
        vec3 authored = u_material.anisotropyDirection.xyz;
        vec3 aT = T * authored.x + B * authored.y + N * authored.z;
        aT -= N * dot(aT, N);
        float aTLen = length(aT);
        aT = (aTLen > 1e-4) ? aT / aTLen : T;
        vec3 aB = cross(N, aT);   // unit: N and aT are unit and perpendicular
        float at = max(a * (1.0 + u_material.anisotropy), 1e-3);
        float ab = max(a * (1.0 - u_material.anisotropy), 1e-3);
        D   = distributionGGXAniso(NdotH, dot(aT, H), dot(aB, H), at, ab);
        Vis = visSmithAniso(at, ab, dot(aT, V), dot(aB, V), NdotV, dot(aT, L), dot(aB, L), NdotL);
    } else {
        D   = distributionGGX(NdotH, a);
        Vis = visSmithCorrelated(NdotV, NdotL, a);
    }
    vec3 F = fresnelSchlick(VdotH, f0);
    vec3 specular = D * Vis * F * energyCompensation;

    // Subsurface wrap bleeds past the terminator, tinted there alone so the lit
    // side keeps the albedo.
    vec3 kd = (vec3(1.0) - F) * (1.0 - s.metallic);
    vec3 diffuse = kd * s.albedo / PI;
    float diffNoL = NdotL;
    vec3  diffTint = vec3(1.0);
    if (u_material.subsurface > 0.001) {
        float w      = u_material.subsurface;
        float rawNoL = dot(N, L);
        diffNoL = clamp((rawNoL + w) / ((1.0 + w) * (1.0 + w)), 0.0, 1.0);
        float blend = 1.0 - smoothstep(-w, w, rawNoL);
        diffTint = mix(vec3(1.0), u_material.subsurfaceColor.rgb, blend);
    }

    // Thin transmission trades front diffuse for a back-lit term, tinted by
    // Beer-Lambert absorption when a volume is declared.
    vec3 transmitted = vec3(0.0);
    if (s.transmission > 0.001) {
        float kt = s.transmission * (1.0 - s.metallic);
        diffuse *= (1.0 - kt);
        transmitted = kt * s.albedo / PI * max(dot(-N, L), 0.0);
        if (u_material.thicknessFactor > 0.0) {
            vec3 transmittance = pow(
                max(u_material.attenuationColor.rgb, vec3(1e-4)),
                vec3(u_material.thicknessFactor / max(u_material.attenuationColor.a, 1e-4))
            );
            transmitted *= transmittance;
        }
    }

    // Subsurface back translucency, tinted.
    vec3 sss = vec3(0.0);
    if (u_material.subsurface > 0.001) {
        sss = u_material.subsurfaceColor.rgb * s.albedo
            * clamp(dot(-N, L), 0.0, 1.0) * u_material.subsurface;
    }

    vec3 baseLit = diffuse * diffTint * diffNoL + specular * NdotL;

    // Clearcoat: a dielectric GGX lobe dimming the base by its Fresnel.
    vec3 ccContrib = vec3(0.0);
    float ccStrength = s.clearcoat;
    if (ccStrength > 0.001) {
        float cca = areaBroadenedAlpha(s.clearcoatRoughness * s.clearcoatRoughness, sourceTan, 1.0);
        float ccD = distributionGGX(NdotH, cca);
        float ccV = visSmithCorrelated(NdotV, NdotL, cca);
        float ccF = fresnelSchlick(VdotH, vec3(COAT_F0)).x * ccStrength;
        baseLit *= (1.0 - ccF);
        ccContrib = vec3(ccD * ccV * ccF) * NdotL;
    }

    // Off when sheenColor is black.
    vec3 sheen = vec3(0.0);
    vec3 sheenColor = u_material.sheenColor.rgb;
    if (max(sheenColor.r, max(sheenColor.g, sheenColor.b)) > 0.0) {
        float sheenAlpha = u_material.sheenColor.a * u_material.sheenColor.a;
        float sheenD     = distributionCharlie(NdotH, sheenAlpha);
        float sheenV     = visAshikhmin(NdotV, NdotL);
        sheen = sheenColor * (sheenD * sheenV * NdotL);
    }

    return (baseLit + ccContrib + transmitted + sss + sheen) * radiance;
}

// Specular occlusion (Lagarde): AO weighted by view angle and roughness, since
// plain AO over-darkens rough specular and under-darkens smooth. Takes GGX alpha.
float specularOcclusion(float NoV, float ao, float alpha) {
    return clamp(pow(NoV + ao, exp2(-16.0 * alpha - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

// Split-sum environment specular plus the multi-bounce energy single scattering
// drops, which darkens rough metal (Fdez-Aguera): the unemitted Ems re-emitted,
// scaled by the average Fresnel. Matters only for rough metal.
//
// @param prefiltered Roughness-prefiltered radiance for the reflection vector.
// @param irradiance  Irradiance for the shading normal; lights the diffuse-like
//                    multi-scatter lobe.
// @param F           Roughness-aware Fresnel at the view angle.
// @param dfg         The split-sum LUT's scale and bias.
// @param f0          Normal-incidence reflectance.
vec3 environmentSpecular(vec3 prefiltered, vec3 irradiance, vec3 F, vec2 dfg, vec3 f0) {
    vec3 FssEss = F * dfg.x + dfg.y;

    // Clamped: a bilinear tap at the LUT's edge can push dfg.x + dfg.y past one.
    float Ems  = clamp(1.0 - (dfg.x + dfg.y), 0.0, 1.0);
    vec3  Favg = f0 + (1.0 - f0) / 21.0;
    vec3  Fms  = FssEss * Favg / max(1.0 - Ems * Favg, vec3(1e-4));

    return prefiltered * FssEss + Fms * Ems * irradiance;
}

void main() {
    ReflectWeight = vec4(0.0);
    ReflectEnv    = vec4(0.0);

    vec3 V = toViewer(vWorldPos);

    vec3 Ng = normalize(vNormal);
    // Interpolation skews the frame; re-orthogonalise, or normal maps and the
    // anisotropic lobe read a tilted basis.
    vec3 tanFlat = vTangent - Ng * dot(Ng, vTangent);
    // A tangent interpolated onto the normal keeps the frame it was given.
    vec3 T = (dot(tanFlat, tanFlat) > 1e-8) ? normalize(tanFlat) : normalize(vTangent);

    // The sign cross cannot recompute: a mirrored UV shell is left-handed.
    vec3 B  = (vHandedness < 0.0) ? -cross(Ng, T) : cross(Ng, T);
    mat3 TBN = mat3(T, B, Ng);

    vec2 uv   = vUV;
    vec2 uvDx = dFdx(vUV);
    vec2 uvDy = dFdy(vUV);
    if (hasTex(MATERIAL_SLOT_HEIGHT) && u_material.heightScale > 0.0) {
        vec3 viewTS = normalize(transpose(TBN) * V);
        uv = parallax(uv, viewTS, uvDx, uvDy);
    }

    // Alpha test before lighting. maskCoverage is the ~1px cutout edge for
    // alpha-to-coverage; without it a pixel is whole, so the cut is at half.
    float maskCoverage = 1.0;
    if (u_material.type == MAT_ALPHA_MASK) {
        float aTex = hasTex(MATERIAL_SLOT_ALBEDO) ? texture(u_albedoTexture, uv).a : 1.0;
        float a    = u_material.albedo.a * aTex;
        maskCoverage = clamp((a - u_material.alphaCutoff) / max(fwidth(a), 1e-5) + 0.5, 0.0, 1.0);
        if (u_alphaToCoverage == 0) maskCoverage = step(0.5, maskCoverage);
        if (maskCoverage <= 0.0) discard;
    }

    // Blended over what the first draw shaded; the reflection inputs write a zero alpha,
    // which leaves them as they were.
    if (u_wirePass != 0) {
        FragColor = vec4(WIRE_COLOR, WIRE_ALPHA);
        return;
    }

    Surface s = sampleSurface(uv);

    // The material views: the surface as sampled, before any light, shown raw.
    if (u_renderMode == MODE_ALBEDO)    { FragColor = vec4(linearToSrgb(s.albedo), 1.0); return; }
    if (u_renderMode == MODE_ROUGHNESS) { FragColor = vec4(vec3(s.roughness), 1.0); return; }
    if (u_renderMode == MODE_METALNESS) { FragColor = vec4(vec3(s.metallic), 1.0); return; }
    if (u_renderMode == MODE_LIGHTING_ONLY) {
        // The light a white, rough-as-authored dielectric would show.
        s.albedo   = vec3(1.0);
        s.metallic = 0.0;
        s.emission = vec3(0.0);
    }

    // Unlit, but fogged.
    if (u_material.type == MAT_UNLIT) {
        vec4 fog = fragmentFog();
        FragColor = vec4((s.albedo + s.emission) * fog.a + fog.rgb, s.opacity);
        return;
    }

    vec3 N = getNormal(uv, Ng, TBN);

    // The coat too: it is smoother and shimmers first.
    s.roughness          = specularAA(N, s.roughness);
    s.clearcoatRoughness = specularAA(N, s.clearcoatRoughness);

    float f0Dielectric = pow((u_material.ior - 1.0) / (u_material.ior + 1.0), 2.0);
    vec3  f0 = mix(vec3(f0Dielectric), s.albedo, s.metallic);

    float NdotV = max(dot(N, V), 1e-4);

    // Read once for the environment, the reflection weight and the direct
    // energy compensation; baked at startup, so valid with no sky.
    vec2 dfg = texture(u_brdfLUT, vec2(NdotV, s.roughness)).rg;
    vec3 energyCompensation = 1.0 + f0 * (1.0 / max(dfg.x + dfg.y, 1e-4) - 1.0);

    // Only these light a fragment from behind; others skip such a light before its shadow.
    bool hasBack = (u_material.subsurface > 0.001) || (s.transmission > 0.001);

    vec3 Lo = vec3(0.0);

    uint count;
    int  ci = 0;
    if (u_useClusters == 1) {
        ci = clusterIndex(
            gl_FragCoord.xy / u_camera.viewport,
            linearizeViewDepth(gl_FragCoord.z, u_camera.invProjection),
            u_camera.zNear,
            u_camera.zFar
        );
        count = u_clusters.clusters[ci].count;
    } else {
        count = uint(min(u_lights.lightCount, MAX_LIGHTS));
    }
    for (uint k = 0u; k < count; ++k) {
        uint  li    = (u_useClusters == 1) ? u_clusters.clusters[ci].indices[k] : k;
        Light light = u_lights.lights[li];

        vec3  lightPos  = light.position.xyz;
        vec3  lightCol  = light.color.xyz;
        float intensity = light.color.w;
        float radius    = light.direction.w;
        int   type      = int(light.position.w);

        // Area lights: LTC diffuse + representative-point specular. intensity is
        // point-equivalent; radiance is intensity / area.
        if (type == LIGHT_RECT || type == LIGHT_DISK) {
            vec3 U  = light.axisU.xyz;
            vec3 Vv = light.axisV.xyz;

            // U x V points out of the emitting face. Behind it a two-sided
            // emitter is wound the other way round.
            float facing   = dot(vWorldPos - lightPos, cross(U, Vv));
            bool  twoSided = (light.axisU.w > 0.5);
            if (facing <= 0.0 && !twoSided) continue;

            vec3  toCenter = lightPos - vWorldPos;
            float dist     = length(toCenter);
            float window   = distanceWindow(dist, radius);
            if (window <= 0.0) continue;

            // Seen from the lit side, the corners (bl -> tl -> tr -> br) wind
            // counter-clockwise about N at local +Z, as the edge sum counts positive.
            mat3 toLocal = ltcTangentFrame(N);
            vec3 F = vec3(0.0);
            float area;
            if (type == LIGHT_RECT) {
                vec3 p0 = normalize(toLocal * ((lightPos - U - Vv) - vWorldPos));
                vec3 p1 = normalize(toLocal * ((lightPos - U + Vv) - vWorldPos));
                vec3 p2 = normalize(toLocal * ((lightPos + U + Vv) - vWorldPos));
                vec3 p3 = normalize(toLocal * ((lightPos + U - Vv) - vWorldPos));
                F = ltcEdgeIntegral(p0, p1) + ltcEdgeIntegral(p1, p2)
                    + ltcEdgeIntegral(p2, p3) + ltcEdgeIntegral(p3, p0);
                area = 4.0 * length(U) * length(Vv);
            } else {
                // 12 vertices read as circular; a 4-vertex diamond loses ~36% of the area.
                const int N_DISK = 12;
                vec3 verts[N_DISK];
                for (int dv = 0; dv < N_DISK; ++dv) {
                    float t = -float(dv) / float(N_DISK) * 2.0 * PI;  // CW order
                    vec3 worldP = lightPos + cos(t) * U + sin(t) * Vv;
                    verts[dv] = normalize(toLocal * (worldP - vWorldPos));
                }
                for (int de = 0; de < N_DISK; ++de) {
                    F += ltcEdgeIntegral(verts[de], verts[(de + 1) % N_DISK]);
                }
                area = PI * dot(U, U);
            }
            if (facing < 0.0) F = -F;
            float formFactor = horizonClippedFormFactor(F);

            // Fresnel at the centre sets the split: the polygon has no single H.
            vec3 Lc = toCenter / max(dist, 1e-4);
            vec3 Fc = fresnelSchlick(max(dot(V, normalize(V + Lc)), 0.0), f0);
            vec3 kd = (vec3(1.0) - Fc) * (1.0 - s.metallic);
            vec3 diffuseArea = kd * s.albedo * formFactor / max(area, 1e-4);

            // Renormalised by (alpha / alpha')^2 so a wide emitter spreads its
            // highlight rather than brightening it.
            vec3 specularArea = vec3(0.0);
            vec3 R = reflect(-V, N);
            vec3 closestPoint = (type == LIGHT_RECT)
                ? areaRectClosestPoint(vWorldPos, R, lightPos, U, Vv)
                : areaDiskClosestPoint(vWorldPos, R, lightPos, U, Vv);

            vec3  toCp    = closestPoint - vWorldPos;
            vec3  Lcp     = toCp / max(length(toCp), 1e-4);
            float NdotLcp = max(dot(N, Lcp), 0.0);
            if (NdotLcp > 0.0) {
                float sourceRadius = (type == LIGHT_RECT)
                    ? max(length(U), length(Vv))
                    : length(U);

                float aGGX    = s.roughness * s.roughness;
                float aBroad  = areaBroadenedAlpha(aGGX, sourceRadius, dist);
                float norm    = (aGGX / aBroad) * (aGGX / aBroad);
                vec3  Hcp     = normalize(V + Lcp);
                vec3  Fcp     = fresnelSchlick(max(dot(V, Hcp), 0.0), f0);
                float D       = distributionGGX(max(dot(N, Hcp), 0.0), aBroad);
                float Vis     = visSmithCorrelated(NdotV, NdotLcp, aBroad);
                specularArea  = D * Vis * Fcp * NdotLcp * norm * energyCompensation
                    / max(dist * dist, 1e-4);
            }

            Lo += lightCol * intensity * window * (diffuseArea + specularArea);
            continue;
        }

        vec3  L;
        float atten = punctualAttenuation(light, type, vWorldPos, L);
        if (atten <= 0.0) continue;
        if (!hasBack && dot(N, L) <= 0.0) continue;

        // The bias follows the geometric surface, not the normal map.
        float visibility = 1.0;

        int sslot = int(light.spot.w);
        if (sslot >= 0) {
            float ndotl = dot(Ng, L);
            if      (type == LIGHT_DIRECTIONAL) visibility *= sampleCSMSoft(vWorldPos, Ng, ndotl);
            else if (type == LIGHT_SPOT)        visibility *= sample2DSlotSoft(sslot, vWorldPos, Ng, ndotl);
            else if (type == LIGHT_POINT)       visibility *= sampleCube(sslot, vWorldPos, ndotl);
        }

        // POM self-shadowing for the sun alone, bounding the trace cost.
        if (type == LIGHT_DIRECTIONAL
            && hasTex(MATERIAL_SLOT_HEIGHT)
            && u_material.heightScale > 0.0
            && visibility > 0.0) {
            vec3 lightDirTS = normalize(transpose(TBN) * L);
            visibility *= parallaxShadow(uv, lightDirTS, uvDx, uvDy);
        }

        // spot.z: tan of a directional's disc, 0 otherwise.
        vec3 radiance = lightCol * intensity * atten * visibility;
        Lo += evaluateLight(N, V, L, T, B, s, f0, energyCompensation, radiance, light.spot.z);
    }

    // Irradiance follows GTAO's bent normal so creases do not over-collect. The
    // bend is from the geometric normal, so it carries over as an offset.
    float gtao  = 1.0;
    vec3  bentN = N;
    if (u_hasAO == 1) {
        vec4 aoSample  = texture(u_ao, gl_FragCoord.xy / u_camera.viewport);
        vec3 bentWorld = normalize(mat3(u_camera.invView) * octDecode(aoSample.gb));
        gtao  = aoSample.r;
        bentN = normalize(N + (bentWorld - Ng));
    }

    // Both measure the same occlusion, so the stronger wins, not the product.
    float ao = min(s.ao, gtao);

    vec3 R = reflect(-V, N);

    // Roughness-aware, so grazing reflections do not blow out on rough surfaces.
    float grazing  = 1.0 - NdotV;
    float grazing2 = grazing * grazing;
    vec3  F  = f0 + (max(vec3(1.0 - s.roughness), f0) - f0) * (grazing2 * grazing2 * grazing);
    vec3  kD = (1.0 - F) * (1.0 - s.metallic);

    // Horizon occlusion drops reflections a normal map turns below the
    // geometric surface.
    float horizon = min(1.0 + dot(R, Ng), 1.0);
    float specOcc = specularOcclusion(NdotV, ao, s.roughness * s.roughness) * horizon * horizon;

    // environmentSpecular's single-scatter half, which a trace can replace (the
    // multi-scatter lobe it cannot), dimmed by a clear coat's Fresnel.
    vec3  reflectWeight = (F * dfg.x + dfg.y) * specOcc;
    bool  hasCoat       = s.clearcoat > 0.001;
    float coatFresnel   = hasCoat ? fresnelSchlick(NdotV, vec3(COAT_F0)).x * s.clearcoat : 0.0;
    reflectWeight *= 1.0 - coatFresnel;
    ReflectWeight = vec4(reflectWeight, s.roughness);

    // Without a baked sky: no reflection and a flat floor, which probes and the
    // irradiance volume still replace where they cover.
    float ccRough          = s.clearcoatRoughness;
    vec3  prefiltered      = vec3(0.0);
    vec3  coat             = vec3(0.0);
    vec3  sourceIrradiance = vec3(FLAT_AMBIENT);
    if (u_hasIBL == 1) {
        prefiltered      = textureLod(u_prefilter, R, s.roughness * MAX_REFLECTION_LOD).rgb;
        coat             = hasCoat ? textureLod(u_prefilter, R, ccRough * MAX_REFLECTION_LOD).rgb : vec3(0.0);
        sourceIrradiance = texture(u_irradiance, bentN).rgb;
    }

    // Probes blended over the sky before shading, so one shading serves all.
    // Coverage is the box's alone; intensity scales the contribution.
    if (u_probeCount > 0) {
        vec3  prefilteredSum = vec3(0.0);
        vec3  coatSum        = vec3(0.0);
        vec3  irradianceSum  = vec3(0.0);
        float wSum           = 0.0;
        for (int p = 0; p < u_probeCount && p < MAX_PROBES; ++p) {
            vec3  center   = u_probes.probes[p].center.xyz;
            vec3  extents  = u_probes.probes[p].extents.xyz;
            float falloff  = u_probes.probes[p].params.x;
            float w        = probeWeight(vWorldPos, center, extents, falloff);
            if (w <= 0.0) continue;
            float scaled  = w * u_probes.probes[p].params.y;
            float layer   = u_probes.probes[p].params.z;
            vec3  Rp      = probeParallax(R, vWorldPos, center, extents);
            vec4  RpLayer = vec4(Rp, layer);
            prefilteredSum += textureLod(u_probePref, RpLayer, s.roughness * MAX_PROBE_LOD).rgb * scaled;
            if (hasCoat) coatSum += textureLod(u_probePref, RpLayer, ccRough * MAX_PROBE_LOD).rgb * scaled;
            irradianceSum  += texture(u_probeIrr, vec4(bentN, layer)).rgb * scaled;
            wSum           += w;
        }
        if (wSum > 0.0) {
            float cover = min(wSum, 1.0);
            prefiltered      = mix(prefiltered, prefilteredSum / wSum, cover);
            coat             = mix(coat, coatSum / wSum, cover);
            sourceIrradiance = mix(sourceIrradiance, irradianceSum / wSum, cover);
        }
    }
    // Scaled where the environment is read, so a traced reflection is not.
    prefiltered      *= u_iblIntensity;
    coat             *= u_iblIntensity;
    sourceIrradiance *= u_iblIntensity;

    // The volume, sampled here rather than at a probe's centre or infinity,
    // where it covers; the reflection's own source elsewhere.
    vec3 irradiance = sourceIrradiance;
    if (u_hasIrradianceVolume == 1) {
        float ivw = irradianceVolumeWeight(vWorldPos);
        if (ivw > 0.0) {
            vec3 volume = sampleIrradianceVolume(vWorldPos, bentN) / PI * u_ivIntensity * u_iblIntensity;
            irradiance = mix(irradiance, volume, ivw);
        }
    }

    // Reflection normalisation (Lazarov): a capture lit more than this point is
    // dimmed by the irradiance ratio, never brightened (the sun is not in it).
    float normalisation = min(luma(irradiance) / max(luma(sourceIrradiance), 1e-4), 1.0);
    prefiltered *= normalisation;
    coat        *= normalisation;

    vec3 diffuseIBL  = irradiance * s.albedo * kD;
    vec3 specularIBL = environmentSpecular(prefiltered, irradiance, F, dfg, f0);

    vec3 ambient = diffuseIBL * multiBounceOcclusion(ao, s.albedo) + specularIBL * specOcc;

    // The coat layer dims everything beneath by its Fresnel (Filament's clear-coat IBL).
    if (hasCoat) ambient = ambient * (1.0 - coatFresnel) + coat * (coatFresnel * specOcc);

    // Zero where nothing was read, so a trace there only adds.
    ReflectEnv = vec4(reflectWeight * prefiltered, 1.0);

    vec3 color = ambient + Lo + s.emission;

    if (u_renderMode == MODE_GI_ONLY)     { FragColor = vec4(ambient, 1.0); return; }
    if (u_renderMode == MODE_DIRECT_ONLY) { FragColor = vec4(Lo, 1.0); return; }
    if (u_renderMode == MODE_CLUSTERS) {
        // Green -> yellow -> red over the cluster's light count.
        float t    = clamp(float(count) / float(MAX_LIGHTS_PER_CLUSTER), 0.0, 1.0) * 3.0;
        vec3  heat = mix(vec3(0.02, 0.10, 0.02), vec3(0.15, 0.85, 0.15), clamp(t, 0.0, 1.0));
        heat       = mix(heat, vec3(0.95, 0.85, 0.10), clamp(t - 1.0, 0.0, 1.0));
        heat       = mix(heat, vec3(0.95, 0.10, 0.10), clamp(t - 2.0, 0.0, 1.0));
        FragColor  = vec4(heat, 1.0);
        return;
    }

    // Before refraction: its scene copy is already fogged. The reflection inputs
    // lose what the colour loses.
    vec4 fog = fragmentFog();
    color = color * fog.a + fog.rgb;
    ReflectWeight.rgb *= fog.a;
    ReflectEnv.rgb    *= fog.a;

    // Screen-space refraction. With no Fresnel split, the specular in `color`
    // is attenuated too.
    if (u_hasSceneColor == 1 && s.transmission > 0.0) {
        vec3 rdir = refract(-V, N, 1.0 / max(u_material.ior, 1.0));
        if (dot(rdir, rdir) > 0.0) {
            float thickness = (u_material.thicknessFactor > 0.0) ? u_material.thicknessFactor : 0.5;
            vec4 cs0 = u_camera.viewProjection * vec4(vWorldPos, 1.0);
            vec4 cs1 = u_camera.viewProjection * vec4(vWorldPos + rdir * thickness, 1.0);
            vec2 uv0 = cs0.xy / max(cs0.w, 1e-4) * 0.5 + 0.5;
            vec2 uv1 = cs1.xy / max(cs1.w, 1e-4) * 0.5 + 0.5;
            vec2 ruv = clamp(gl_FragCoord.xy / u_camera.viewport + (uv1 - uv0), vec2(0.0), vec2(1.0));

            vec3 transmitted = texture(u_sceneColor, ruv).rgb * s.albedo;
            if (u_material.thicknessFactor > 0.0 && u_material.attenuationColor.a > 0.0) {
                float dist = u_material.thicknessFactor / max(abs(dot(N, rdir)), 0.1);
                transmitted *= pow(
                    max(u_material.attenuationColor.rgb, vec3(1e-4)),
                    vec3(dist / u_material.attenuationColor.a)
                );
            }
            color = mix(color, transmitted, s.transmission * (1.0 - s.metallic));
        }
    }

    // Clamped to what RGBA16F holds: past it a channel becomes infinity.
    float outAlpha = (u_material.type == MAT_TRANSPARENT) ? s.opacity : maskCoverage;
    FragColor = vec4(min(color, vec3(HALF_MAX)), outAlpha);

    // Blends zero at its opacity over the inputs behind, roughness masked off
    // (GLForwardPass), dimming them as it dims everything behind.
    if (u_material.type == MAT_TRANSPARENT) {
        ReflectWeight = vec4(0.0, 0.0, 0.0, outAlpha);
        ReflectEnv    = vec4(0.0, 0.0, 0.0, outAlpha);
    }

    // Never traced: the prepass skips cutouts, so the G-buffer under one holds
    // the surface behind.
    if (u_material.type == MAT_ALPHA_MASK) {
        ReflectWeight = vec4(0.0);
        ReflectEnv    = vec4(0.0);
    }
}
