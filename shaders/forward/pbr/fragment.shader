/*
 * Forward PBR ubershader for the full MaterialAsset spec: metal-rough GGX base
 * (optionally anisotropic), clearcoat, sheen, subsurface, thin transmission,
 * POM, alpha mask and unlit; probes over the IBL. The reflection's weight and
 * radiance go beside the colour for the screen-space resolve
 * (reflection/resolve/fragment.shader). Each fragment, transparent ones too, is
 * fogged here at its own depth and never again; shading-split debug views are not.
 */

#include "../../constants.glsl"
#include "../../brdf.glsl"
#include "../../area_light.glsl"
#include "../../color.glsl"

in vec3 vWorldPos;
flat in uint vViewmodel;
in vec3 vNormal;
in vec2 vUV;
in vec3 vTangent;
in float vHandedness;
in vec2  vRest;
in float vHeight;
in vec3  vAlongX;
in vec3  vAlongZ;
in float vLingering;

layout(location = OUT_COLOR) out vec4 FragColor;

// The reflection pass's inputs (GLTarget, Layout::Scene). The product is stored,
// not the radiance, so the pass subtracts exactly what was added whatever the
// weight's quantising. A pixel reflecting nothing writes zero; alpha is what a
// transparent surface blends by.
// rgb: the split-sum weight a trace can replace; a: the lobe's roughness, its widest axis.
layout(location = OUT_REFLECT_WEIGHT) out vec4 ReflectWeight;
layout(location = OUT_REFLECT_ENV) out vec4 ReflectEnv;  // rgb: that weight x the probe / sky radiance

#include "../../material.glsl"
#include "../../camera.glsl"
#include "../../fog.glsl"
#include "../../shadows.glsl"
#include "../../ocean_water.glsl"
#include "../../surface.glsl"
#include "../../parallax.glsl"

// Whether the optional features - anisotropy, subsurface, transmission, clearcoat, sheen and
// parallax - are compiled in. The plain variant (forward/pbr_plain) leaves them out, for a material
// that uses none (GLMaterial::plain): a feature gated here is one GLMaterial::update tests.
#ifdef PLAIN
const bool FEATURES = false;
#else
const bool FEATURES = true;
#endif

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
uniform int u_useClusters;      // 1 = per-cluster light list; 0 = every light, for an eye without clusters
uniform int u_hardShadows;      // 1 = the 3x3 compare for the sun and spots, no blocker search
uniform int u_alphaToCoverage;  // 1 while alpha-to-coverage is on for a cutout

// GTAO for the indirect term, the cascaded sun's screen shadow. Each flag is set for the opaque bucket
// alone: both saw only what the prepass drew.
#include "../../scene_ao.glsl"

#include "../../ambient.glsl"

// A clear coat is a dielectric of index 1.5.
const float COAT_F0 = 0.04;

// The share of a lobe of @p roughness the one reflection trace, at @p traceLobe, stands for: all of
// the sharper lobe, and none of one TRACE_LOBE_GAP rougher, which keeps its environment's blur.
const float TRACE_LOBE_GAP = 0.25;
float traceShare(float roughness, float traceLobe) {
    return clamp(1.0 - (roughness - traceLobe) / TRACE_LOBE_GAP, 0.0, 1.0);
}

// The fragment's frame and what every light it takes reads.
struct Shading {
    vec3  V;                   // toward the viewer
    vec3  N;                   // the shading normal, the normal map's
    vec3  Ng;                  // the geometric normal; a back face's turned toward it
    vec3  T;                   // the tangent frame about Ng, re-orthogonalised
    vec3  B;
    vec2  uv;                  // displaced by parallax
    vec2  uvDx;                // the undisplaced uv's derivatives
    vec2  uvDy;
    vec3  f0;
    float NdotV;
    vec3  energyCompensation;  // multi-bounce energy, Filament's 1 + f0 (1 / Ess - 1)
};

// The direct lights' specular, summed apart as evaluateLight and the area lights shade it, so a
// transmissive surface keeps its reflections when its colour blends toward what it refracts.
vec3 g_directSpecular = vec3(0.0);

// The material's own subsurface lobe: the scene's ocean's water scatters by oceanScatter instead,
// and with no ocean a `waves` material is an ordinary one.
bool hasSubsurface() {
    return FEATURES && u_material.subsurface > 0.001 && (u_material.waves == 0 || !oceanPresent());
}

bool hasAnisotropy() {
    return FEATURES && u_material.anisotropy > 0.001;
}

// The sheen colour's strongest channel, 0 for no sheen.
float sheenStrength() {
    vec3 sheenColor = u_material.sheenColor.rgb;
    return max(sheenColor.r, max(sheenColor.g, sheenColor.b));
}

bool hasSheen() {
    return FEATURES && sheenStrength() > 0.0;
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

// The anisotropy's direction in the shading plane: the authored one in tangent space, else T, as
// a zeroed or N-parallel one would normalize to NaN.
vec3 anisotropyTangent(vec3 T, vec3 B, vec3 N) {
    vec3 authored = u_material.anisotropyDirection.xyz;
    vec3 aT = T * authored.x + B * authored.y + N * authored.z;
    aT -= N * dot(aT, N);
    float aTLen = length(aT);
    return (aTLen > 1e-4) ? aT / aTLen : T;
}

// The GGX alpha along an anisotropic material's direction, from @p a across it
// (KHR_materials_anisotropy): rougher along it, as rough as authored across.
float anisotropicAlpha(float a) {
    return mix(a, 1.0, u_material.anisotropy * u_material.anisotropy);
}

// The sheen lobe's directional albedo at this angle to the normal, from the BRDF LUT's third
// channel.
float sheenAlbedo(float NdotX) {
    return texture(u_brdfLUT, vec2(NdotX, u_material.sheenColor.a)).b;
}

// How much of the light a sheen takes at this angle, its albedo times its strength: the layer
// under it keeps the rest (KHR_materials_sheen's albedo scaling).
float sheenTaken(float NdotX) {
    return sheenStrength() * sheenAlbedo(NdotX);
}

// One light's reflected radiance toward the viewer, arriving along @p L with @p radiance.
// sourceTan is the tan of the light's angular radius (0 for a point); the lobe widens over the
// disc unrenormalised, spreading its irradiance instead of a peak a half float may not hold. A
// sphere light's isotropic lobe is toward its representative point @p Ls, renormalised by
// @p sphereNorm (sphereLobe); Ls is L for a point source.
vec3 evaluateLight(
    Shading sh,
    Surface s,
    vec3 L,
    vec3 radiance,
    float sourceTan,
    vec3 Ls,
    float sphereNorm
) {
    vec3  N     = sh.N;
    vec3  V     = sh.V;
    vec3  H     = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float a = areaBroadenedAlpha(s.roughness * s.roughness, sourceTan, 1.0);

    // Base specular; specNoL is the cosine the lobe takes.
    float D, Vis;
    float specNoL = NdotL;
    if (hasAnisotropy()) {
        vec3 aT = anisotropyTangent(sh.T, sh.B, N);
        vec3 aB = cross(N, aT);   // unit: N and aT are unit and perpendicular
        float at = max(anisotropicAlpha(a), 1e-3);
        float ab = max(a, 1e-3);
        D   = distributionGGXAniso(NdotH, dot(aT, H), dot(aB, H), at, ab);
        Vis = visSmithAniso(at, ab, dot(aT, V), dot(aB, V), sh.NdotV, dot(aT, L), dot(aB, L), NdotL);
    } else {
        vec3 Hs = normalize(V + Ls);
        specNoL = max(dot(N, Ls), 0.0);
        D       = distributionGGX(max(dot(N, Hs), 0.0), a) * sphereNorm;
        Vis     = visSmithCorrelated(sh.NdotV, specNoL, a);
    }
    vec3 F = fresnelSchlick(VdotH, sh.f0);
    vec3 specular = D * Vis * F * sh.energyCompensation;

    // Subsurface wrap bleeds past the terminator, tinted there alone so the lit
    // side keeps the albedo.
    vec3 kd = (vec3(1.0) - F) * (1.0 - s.metallic);
    vec3 diffuse = kd * s.albedo / PI;
    float diffNoL = NdotL;
    vec3  diffTint = vec3(1.0);
    if (hasSubsurface()) {
        float w      = u_material.subsurface;
        float rawNoL = dot(N, L);
        diffNoL = clamp((rawNoL + w) / ((1.0 + w) * (1.0 + w)), 0.0, 1.0);
        float blend = 1.0 - smoothstep(-w, w, rawNoL);
        diffTint = mix(vec3(1.0), u_material.subsurfaceColor.rgb, blend);
    }

    // Thin transmission trades front diffuse for a back-lit term, tinted by
    // Beer-Lambert absorption when a volume is declared.
    vec3 transmitted = vec3(0.0);
    if (FEATURES && s.transmission > 0.001) {
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

    // Subsurface back translucency, tinted: Unreal's two-sided foliage, a wrapped back-light
    // through a broad lobe about the light's own direction, so a leaf glows looking toward
    // the sun and not with it behind. Normalised as the diffuse beside it is.
    vec3 sss = vec3(0.0);
    if (hasSubsurface()) {
        float wrap    = clamp((-dot(N, L) + 0.5) / 2.25, 0.0, 1.0);
        float scatter = distributionGGX(clamp(-dot(V, L), 0.0, 1.0), 0.6);
        sss = u_material.subsurfaceColor.rgb * s.albedo * (wrap * scatter * u_material.subsurface);
    }

    vec3 baseLit = diffuse * diffTint * diffNoL + specular * specNoL;

    // Clearcoat: a dielectric GGX lobe dimming the base by its Fresnel, on the geometric normal -
    // smooth over the base's bumps (KHR_materials_clearcoat without a coat normal map).
    vec3 ccContrib = vec3(0.0);
    float underCoat = 1.0;
    float ccStrength = s.clearcoat;
    if (FEATURES && ccStrength > 0.001) {
        vec3  Ng   = sh.Ng;
        float cNoL = max(dot(Ng, L), 0.0);
        float cNoV = max(dot(Ng, V), 1e-4);
        float cca = areaBroadenedAlpha(s.clearcoatRoughness * s.clearcoatRoughness, sourceTan, 1.0);
        float ccD = distributionGGX(max(dot(Ng, H), 0.0), cca);
        float ccV = visSmithCorrelated(cNoV, cNoL, cca);
        float ccF = fresnelSchlick(VdotH, vec3(COAT_F0)).x * ccStrength;
        underCoat = 1.0 - ccF;
        baseLit *= underCoat;
        ccContrib = vec3(ccD * ccV * ccF) * cNoL;
    }

    // A sheen over the base and under the coat, off when its colour is black. What it takes of the
    // light going in or coming out the base below does not get (KHR_materials_sheen).
    vec3  sheen      = vec3(0.0);
    float underSheen = 1.0;
    if (hasSheen()) {
        float sheenAlpha = u_material.sheenColor.a * u_material.sheenColor.a;
        float sheenD     = distributionCharlie(NdotH, sheenAlpha);
        float sheenV     = visCharlie(sh.NdotV, NdotL, sheenAlpha);
        sheen      = u_material.sheenColor.rgb * (sheenD * sheenV * NdotL) * underCoat;
        underSheen = 1.0 - max(sheenTaken(sh.NdotV), sheenTaken(NdotL));
    }

    g_directSpecular += (specular * specNoL * underCoat * underSheen + ccContrib) * radiance;
    return ((baseLit + transmitted + sss) * underSheen + ccContrib + sheen) * radiance;
}

// A rect or disk light's reflected radiance: its diffuse by the exact form factor, its specular from
// Karis's representative point, renormalised by (alpha / alpha')^2 (lighting.md).
vec3 areaLight(Shading sh, Surface s, Light light, int type) {
    float facing, window;
    if (!areaReaches(light, vWorldPos, facing, window)) return vec3(0.0);

    vec3  lightPos = light.position.xyz;
    vec3  U        = light.axisU.xyz;
    vec3  Vv       = light.axisV.xyz;
    vec3  toCenter = lightPos - vWorldPos;
    float dist     = length(toCenter);

    // Fresnel at the centre sets the split: the polygon has no single H.
    vec3 Lc = toCenter / max(dist, 1e-4);
    vec3 Fc = fresnelSchlick(max(dot(sh.V, normalize(sh.V + Lc)), 0.0), sh.f0);
    vec3 kd = (vec3(1.0) - Fc) * (1.0 - s.metallic);
    vec3 diffuse = kd * s.albedo * areaIrradiance(light, type, vWorldPos, sh.N, facing, window);

    vec3 specular = vec3(0.0);
    vec3 R = reflect(-sh.V, sh.N);
    vec3 closestPoint = (type == LIGHT_RECT)
        ? areaRectClosestPoint(vWorldPos, R, lightPos, U, Vv)
        : areaDiskClosestPoint(vWorldPos, R, lightPos, U, Vv);

    vec3  toCp    = closestPoint - vWorldPos;
    vec3  Lcp     = toCp / max(length(toCp), 1e-4);
    float NdotLcp = max(dot(sh.N, Lcp), 0.0);
    if (NdotLcp > 0.0) {
        float sourceRadius = (type == LIGHT_RECT) ? max(length(U), length(Vv)) : length(U);
        float aGGX   = s.roughness * s.roughness;
        float aBroad = areaBroadenedAlpha(aGGX, sourceRadius, dist);
        float norm   = (aGGX / aBroad) * (aGGX / aBroad);
        vec3  Hcp    = normalize(sh.V + Lcp);
        vec3  Fcp    = fresnelSchlick(max(dot(sh.V, Hcp), 0.0), sh.f0);
        float D      = distributionGGX(max(dot(sh.N, Hcp), 0.0), aGGX);
        float Vis    = visSmithCorrelated(sh.NdotV, NdotLcp, aGGX);
        specular     = D * Vis * Fcp * NdotLcp * norm * sh.energyCompensation / max(dist * dist, 1e-4);
    }

    vec3 radiance = light.color.xyz * light.color.w;
    g_directSpecular += radiance * window * specular;
    return radiance * (diffuse + window * specular);
}

// A point or spot of radius r is a sphere (Karis 2013): its lobe points at the sphere's point
// nearest the mirror ray (@p Ls, else L), renormalised by the (a / a')^2 returned, a' = a + r / 2d
// (lighting.md).
float sphereLobe(Shading sh, Surface s, Light light, int type, inout vec3 Ls) {
    float sphereR = light.axisV.w;
    if (sphereR <= 0.0 || (type != LIGHT_POINT && type != LIGHT_SPOT)) return 1.0;
    vec3  toLight = light.position.xyz - vWorldPos;
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
// the others skip one before its shadow. @p water adds the ocean's scattering, of colour @p scatter.
vec3 directLight(
    Shading sh,
    Surface s,
    bool litFromBehind,
    bool water,
    vec3 scatter,
    out uint count
) {
    // The contacts the cascades are too coarse for, the cascaded sun's alone.
    float screenShadow = screenShadowAt(gl_FragCoord.xy / u_camera.viewport);

    int ci = 0;
    if (u_useClusters == 1) {
        vec2  clusterUV    = gl_FragCoord.xy / u_camera.viewport;
        float clusterDepth = linearizeViewDepth(gl_FragCoord.z, u_camera.invProjection);
        // Its lights are where it is, not where its own projection drew it.
        if (vViewmodel != 0u) {
            clusterUV    = screenUvOf(vWorldPos);
            clusterDepth = viewDepthOf(vWorldPos);
        }
        ci = clusterIndex(clusterUV, clusterDepth, u_camera.zNear, u_camera.zFar);
        count = u_clusters.clusters[ci].count;
    } else {
        count = uint(min(u_lights.lightCount, MAX_LIGHTS));
    }

    const mat3 TBN = mat3(sh.T, sh.B, sh.Ng);
    vec3 Lo = vec3(0.0);
    for (uint k = 0u; k < count; ++k) {
        uint  li    = (u_useClusters == 1) ? u_clusters.clusters[ci].indices[k] : k;
        Light light = u_lights.lights[li];
        int   type  = int(light.position.w);

        // Without clusters, a light out of its radius is passed over here, as a cluster leaves it out.
        if (u_useClusters == 0 && type != LIGHT_DIRECTIONAL) {
            vec3 away = vWorldPos - light.position.xyz;
            if (dot(away, away) > light.direction.w * light.direction.w) continue;
        }

        if (type == LIGHT_RECT || type == LIGHT_DISK) {
            Lo += areaLight(sh, s, light, type);
            continue;
        }

        vec3  L;
        float atten = punctualAttenuation(light, type, vWorldPos, L);
        if (atten <= 0.0) continue;
        if (!litFromBehind && dot(sh.N, L) <= 0.0) continue;

        // The bias follows the geometric surface, not the normal map.
        float ndotlGeo   = dot(sh.Ng, L);
        float visibility = surfaceShadow(
            type,
            int(light.spot.w),
            vWorldPos,
            sh.Ng,
            ndotlGeo,
            screenShadow,
            u_hardShadows == 0
        );

        // POM self-shadowing for the sun alone, bounding the trace cost.
        if (FEATURES
            && type == LIGHT_DIRECTIONAL
            && hasTex(MATERIAL_SLOT_HEIGHT)
            && u_material.heightScale > 0.0
            && visibility > 0.0) {
            vec3 lightDirTS = normalize(transpose(TBN) * L);
            visibility *= parallaxShadow(sh.uv, lightDirTS, sh.uvDx, sh.uvDy);
        }

        // Micro-shadowing (Chan, CoD WWII; Filament): the material's own AO is the cone a cavity
        // stays open over, so a grazing light leaves mortar and creases dark rather than flat.
        // Front-lit only; the back-lights through it are the subsurface's and transmission's.
        float NoLs = dot(sh.N, L);
        if (s.ao < 1.0 && NoLs > 0.0) {
            float micro = clamp(NoLs * inversesqrt(1.0 - min(s.ao, 0.9999)), 0.0, 1.0);
            visibility *= micro * micro;
        }

        vec3  Ls         = L;
        float sphereNorm = sphereLobe(sh, s, light, type, Ls);

        // spot.z: tan of a directional's disc, 0 otherwise.
        vec3 radiance = light.color.xyz * light.color.w * atten * visibility;
        Lo += evaluateLight(sh, s, L, radiance, light.spot.z, Ls, sphereNorm);
        if (water) {
            const vec3 into = 1.0 - fresnelSchlick(sh.NdotV, sh.f0);
            Lo += into * oceanScatter(scatter, sh.N, sh.V, L, radiance, oceanCrest(vHeight));
        }
    }
    return Lo;
}

// The environment's light on @p s - the sky, probes and irradiance volume - through GTAO and the
// material's occlusion. Writes the reflection inputs (ReflectWeight, ReflectEnv); @p specularPart
// is what of the result is reflected, @p FssEss the single-scatter energy a trace can replace.
// @p water adds the ocean's own colour, @p scatter, lit by the sky it takes in.
vec3 environmentLight(
    Shading sh,
    Surface s,
    vec2 dfg,
    bool water,
    vec3 scatter,
    out vec3 specularPart,
    out vec3 FssEss
) {
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

    // An anisotropic lobe reflects about a normal bent toward its stretch, the more the stronger and
    // smoother it is (the glTF sample viewer's). The trace, which reflects about the G-buffer's
    // normal, is handed it as wide as its widest axis.
    vec3  R             = reflect(-V, N);
    float lobeRoughness = sqrt(anisotropicAlpha(s.roughness * s.roughness));
    if (hasAnisotropy()) {
        vec3  aB     = cross(N, anisotropyTangent(sh.T, sh.B, N));
        vec3  across = cross(cross(aB, V), aB);
        float bend   = 1.0 - u_material.anisotropy * (1.0 - s.roughness);
        R = reflect(-V, normalize(mix(across, N, bend * bend * bend * bend)));
    }

    // What the environment's specular takes, and the diffuse what is left (Fdez-Aguera, section 4).
    vec3 FmsEms;
    environmentEnergy(dfg, sh.f0, FssEss, FmsEms);
    vec3 kD = (1.0 - (FssEss + FmsEms)) * (1.0 - s.metallic);

    // Horizon occlusion drops reflections a normal map turns below the geometric surface.
    float horizon = min(1.0 + dot(R, Ng), 1.0);

    // Where a rough lobe peaks: GGX's dominant direction bends from R toward N as roughness
    // grows (Lagarde, Moving Frostbite to PBR 4.9.3), so a rough floor at grazing does not
    // reflect the bright horizon R points at. The coat reflects off its smooth top (Rc).
    float rdA = s.roughness * s.roughness;
    vec3  Rd  = normalize(mix(N, R, (1.0 - rdA) * (sqrt(1.0 - rdA) + rdA)));
    float specOcc = specularOcclusion(sh.NdotV, ao, s.roughness * s.roughness) * horizon * horizon;

    // The single-scatter half, dimmed by a clear coat's Fresnel and a sheen's share, plus the coat's
    // own mirror, each by its share of the one trace (traceShare): traced at the sharper lobe, a
    // coated surface shows no probe reflection over the traced one, and a rough base under a
    // sharp coat keeps its own blur. Not occluded: a trace that hits has found the occluder
    // itself, so only the environment it replaces (ReflectEnv) carries the occlusion.
    vec3  reflectWeight = FssEss;
    float underSheen    = hasSheen() ? 1.0 - sheenTaken(sh.NdotV) : 1.0;
    bool  hasCoat       = FEATURES && s.clearcoat > 0.001;
    float coatNoV       = max(dot(Ng, V), 1e-4);
    float coatFresnel   = hasCoat ? fresnelSchlick(coatNoV, vec3(COAT_F0)).x * s.clearcoat : 0.0;
    vec3  Rc            = reflect(-V, Ng);
    // The coat's top is never below its own horizon, so it takes no horizon term.
    float coatOcc       = specularOcclusion(coatNoV, ao, s.clearcoatRoughness * s.clearcoatRoughness);
    float traceLobe  = hasCoat ? min(lobeRoughness, s.clearcoatRoughness) : lobeRoughness;
    reflectWeight *= (1.0 - coatFresnel) * underSheen * traceShare(lobeRoughness, traceLobe);
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
    if (u_probeCount > 0) {
        vec3  prefilteredSum = vec3(0.0);
        vec3  coatSum        = vec3(0.0);
        vec3  irradianceSum  = vec3(0.0);
        float covered        = 0.0;
        for (int p = 0; p < u_probeCount && p < MAX_PROBES && covered < 0.999; ++p) {
            float w = probeShare(p, vWorldPos, covered);
            if (w <= 0.0) continue;
            vec3  center  = u_probes.probes[p].center.xyz;
            vec3  extents = u_probes.probes[p].extents.xyz;
            float scaled  = w * u_probes.probes[p].params.y;
            float layer   = u_probes.probes[p].params.z;
            vec4  RpLayer = vec4(probeParallax(Rd, vWorldPos, center, extents), layer);
            prefilteredSum += textureLod(u_probePref, RpLayer, s.roughness * MAX_PROBE_LOD).rgb * scaled;
            if (hasCoat) {
                vec4 coatLayer = vec4(probeParallax(Rc, vWorldPos, center, extents), layer);
                coatSum += textureLod(u_probePref, coatLayer, ccRough * MAX_PROBE_LOD).rgb * scaled;
            }
            irradianceSum += texture(u_probeIrr, vec4(bentN, layer)).rgb * scaled;
        }
        prefiltered      = prefilteredSum + prefiltered * (1.0 - covered);
        coat             = coatSum + coat * (1.0 - covered);
        sourceIrradiance = irradianceSum + sourceIrradiance * (1.0 - covered);
    }
    // Scaled where the environment is read, so a traced reflection is not.
    prefiltered      *= u_iblIntensity;
    coat             *= u_iblIntensity;
    sourceIrradiance *= u_iblIntensity;

    // The volume, sampled here rather than at a probe's centre or infinity, where it covers.
    vec3 irradiance = sourceIrradiance;
    if (u_hasIrradianceVolume == 1) {
        // Weighted where it is read, so a box fitted to a room's inside covers its walls.
        const vec3 lookup = irradianceVolumeLookup(vWorldPos, Ng);
        irradiance = blendIrradianceVolume(irradiance, lookup, bentN, u_iblIntensity);
    }

    // Reflection normalisation (Lazarov): a capture lit more than this point is
    // dimmed by the irradiance ratio, never brightened (the sun is not in it).
    float normalisation = min(luma(irradiance) / max(luma(sourceIrradiance), 1e-4), 1.0);
    prefiltered *= normalisation;
    coat        *= normalisation;

    vec3 diffuseIBL = irradiance * s.albedo * kD;
    if (water) diffuseIBL += irradiance * scatter * (SCATTER_AMBIENT * kD);
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

    // The coat layer dims everything beneath by its Fresnel (Filament's clear-coat IBL).
    if (hasCoat) {
        ambient      = ambient * (1.0 - coatFresnel) + coat * (coatFresnel * coatOcc);
        specularPart = specularPart * (1.0 - coatFresnel) + coat * (coatFresnel * coatOcc);
    }

    // Zero where nothing was read, so a trace there only adds.
    ReflectEnv = vec4(reflectWeight * specOcc * prefiltered + coat * (coatWeight * coatOcc), 1.0);
    return ambient;
}

// The Light Clusters view: green -> yellow -> red over the cluster's light count, magenta where
// the list is full, as lights past it are dropped and show as a cut.
vec3 clusterHeat(uint count) {
    float t    = clamp(float(count) / float(MAX_LIGHTS_PER_CLUSTER), 0.0, 1.0) * 3.0;
    vec3  heat = mix(vec3(0.02, 0.10, 0.02), vec3(0.15, 0.85, 0.15), clamp(t, 0.0, 1.0));
    heat       = mix(heat, vec3(0.95, 0.85, 0.10), clamp(t - 1.0, 0.0, 1.0));
    heat       = mix(heat, vec3(0.95, 0.10, 0.10), clamp(t - 2.0, 0.0, 1.0));
    if (count >= uint(MAX_LIGHTS_PER_CLUSTER)) heat = vec3(0.95, 0.10, 0.95);
    return heat;
}

// Screen-space refraction of @p color. What the surface reflects, @p reflected, stays whole and
// only the rest blends toward the scene behind, itself dimmed by the reflected share @p FssEss
// (Filament), so glass keeps its reflections and highlights.
vec3 refraction(Shading sh, Surface s, vec3 color, vec3 reflected, vec3 FssEss) {
    vec3 rdir = refract(-sh.V, sh.N, 1.0 / max(u_material.ior, 1.0));
    if (dot(rdir, rdir) <= 0.0) return color;

    float thickness = (u_material.thicknessFactor > 0.0) ? u_material.thicknessFactor : 0.5;
    vec2 uv0 = screenUvOf(vWorldPos);
    vec2 uv1 = screenUvOf(vWorldPos + rdir * thickness);
    vec2 ruv = clamp(gl_FragCoord.xy / u_camera.viewport + (uv1 - uv0), vec2(0.0), vec2(1.0));

    vec3 transmitted = texture(u_sceneColor, ruv).rgb * s.albedo;
    if (u_material.thicknessFactor > 0.0 && u_material.attenuationColor.a > 0.0) {
        float dist = u_material.thicknessFactor / max(abs(dot(sh.N, rdir)), 0.1);
        transmitted *= pow(
            max(u_material.attenuationColor.rgb, vec3(1e-4)),
            vec3(dist / u_material.attenuationColor.a)
        );
    }
    transmitted *= 1.0 - FssEss;
    return mix(color - reflected, transmitted, s.transmission * (1.0 - s.metallic)) + reflected;
}

void main() {
    ReflectWeight = vec4(0.0);
    ReflectEnv    = vec4(0.0);

    Shading sh;
    sh.V = toViewer(vWorldPos);

    vec3 Ng      = normalize(vNormal);
    vec3 tangent = vTangent;
    // The ocean's water: the waves' frame as the vertices left it, with the ripples this pixel can show.
    const bool water   = u_material.waves != 0 && oceanPresent();
    float      seaFoam = 0.0;
    OceanShade sea;
    if (water) {
        sea     = waterSurface(vRest, vAlongX, vAlongZ, vLingering, vHeight, vWorldPos, seaFoam);
        Ng      = sea.normal;
        tangent = sea.tangent;
    }
    // Interpolation skews the frame; re-orthogonalise, or normal maps and the
    // anisotropic lobe read a tilted basis.
    vec3 tanFlat = tangent - Ng * dot(Ng, tangent);
    // A tangent interpolated onto the normal keeps the frame it was given.
    vec3 T = (dot(tanFlat, tanFlat) > 1e-8) ? normalize(tanFlat) : normalize(tangent);
    // The sign cross cannot recompute: a mirrored UV shell is left-handed.
    vec3 B = (vHandedness < 0.0) ? -cross(Ng, T) : cross(Ng, T);
    // A double-sided material's back face is lit as its own: the whole frame turns, so the
    // normal map mirrors with it (the glTF sample viewer's). Single-sided ones never show one.
    if (!gl_FrontFacing) {
        T  = -T;
        B  = -B;
        Ng = -Ng;
    }
    sh.T  = T;
    sh.B  = B;
    sh.Ng = Ng;
    const mat3 TBN = mat3(T, B, Ng);

    sh.uv   = vUV;
    sh.uvDx = dFdx(vUV);
    sh.uvDy = dFdy(vUV);
    if (FEATURES && hasTex(MATERIAL_SLOT_HEIGHT) && u_material.heightScale > 0.0) {
        vec3 viewTS = normalize(transpose(TBN) * sh.V);
        sh.uv = parallax(sh.uv, viewTS, sh.uvDx, sh.uvDy);
    }

    // Alpha test before lighting. maskCoverage is the ~1px cutout edge for
    // alpha-to-coverage; without it a pixel is whole, so the cut is at half.
    float maskCoverage = 1.0;
    if (u_material.type == MAT_ALPHA_MASK) {
        float a = sampledOpacity(sh.uv);
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

    Surface s = sampleSurface(sh.uv);

    // The water's scattering colour, its strength in it.
    const vec3 scatter = water ? u_material.subsurfaceColor.rgb * u_material.subsurface : vec3(0.0);
    if (water) {
        // Ripples too fine for the pixel roughen it: GGX's alpha squared takes twice their slope
        // variance, as Beckmann's would.
        float alpha = s.roughness * s.roughness;
        s.roughness = sqrt(sqrt(alpha * alpha + 2.0 * sea.variance));
        // Whitecaps and waterlines: foam over the water.
        s.albedo       = mix(s.albedo, u_ocean.foam.rgb, seaFoam);
        s.roughness    = mix(s.roughness, FOAM_ROUGHNESS, seaFoam);
        s.metallic     = mix(s.metallic, 0.0, seaFoam);
        s.transmission = mix(s.transmission, 0.0, seaFoam);
    }

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

    sh.N = getNormal(sh.uv, Ng, TBN);

    // The coat too, on its own smooth normal: it is smoother and shimmers first.
    s.roughness          = specularAA(sh.N, s.roughness);
    s.clearcoatRoughness = specularAA(Ng, s.clearcoatRoughness);

    sh.f0    = surfaceF0(u_material.ior, s.albedo, s.metallic);
    sh.NdotV = max(dot(sh.N, sh.V), 1e-4);

    // Read once for the environment, the reflection weight and the direct
    // energy compensation; baked at startup, so valid with no sky.
    vec2 dfg = texture(u_brdfLUT, vec2(sh.NdotV, s.roughness)).rg;
    sh.energyCompensation = 1.0 + sh.f0 * (1.0 / max(dfg.x + dfg.y, 1e-4) - 1.0);

    const bool litFromBehind = hasSubsurface() || (FEATURES && s.transmission > 0.001) || water;
    uint count;
    vec3 Lo = directLight(sh, s, litFromBehind, water, scatter, count);

    vec3 ambientSpecular;
    vec3 FssEss;
    vec3 ambient = environmentLight(sh, s, dfg, water, scatter, ambientSpecular, FssEss);

    if (u_renderMode == MODE_GI_ONLY)     { FragColor = vec4(ambient, 1.0); return; }
    if (u_renderMode == MODE_DIRECT_ONLY) { FragColor = vec4(Lo, 1.0); return; }
    if (u_renderMode == MODE_CLUSTERS)    { FragColor = vec4(clusterHeat(count), 1.0); return; }

    // Before refraction: its scene copy is already fogged. The reflection inputs
    // lose what the colour loses.
    vec4 fog   = fragmentFog();
    vec3 color = (ambient + Lo + s.emission) * fog.a + fog.rgb;
    ReflectWeight.rgb *= fog.a;
    ReflectEnv.rgb    *= fog.a;

    if (FEATURES && u_hasSceneColor == 1 && s.transmission > 0.0) {
        color = refraction(sh, s, color, (ambientSpecular + g_directSpecular) * fog.a, FssEss);
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
