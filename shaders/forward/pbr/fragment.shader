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
// rgb: split-sum weight x specular occlusion; a: roughness.
layout(location = OUT_REFLECT_WEIGHT) out vec4 ReflectWeight;
layout(location = OUT_REFLECT_ENV) out vec4 ReflectEnv;  // rgb: that weight x the probe / sky radiance

#include "../../material.glsl"

#include "../../camera.glsl"
#include "../../fog.glsl"

#include "../../shadows.glsl"
#include "../../ocean.glsl"

// Whether the optional features - anisotropy, subsurface, transmission, clearcoat, sheen and
// parallax - are compiled in. The plain variant (forward/pbr_plain) leaves them out, for a material
// that uses none (GLMaterial::plain).
#ifdef PLAIN
const bool FEATURES = false;
#else
const bool FEATURES = true;
#endif

// The material's own subsurface lobe: the scene's ocean's water scatters by oceanScatter instead,
// and with no ocean a `waves` material is an ordinary one.
bool hasSubsurface() {
    return FEATURES && u_material.subsurface > 0.001 && (u_material.waves == 0 || !oceanPresent());
}

// The ocean's ripple fields (GLOceanFields), repeating, a layer each, the longest ripples in 0.
layout(binding = OCEAN_SLOT_RIPPLES) uniform sampler2DArray u_ripples;
// Metres from each point of a square of sea to whatever breaks the water (GLOceanFields), and where
// the square lies: xy its corner on the XZ plane (least X, most Z), z its edge; w how far it looks
// round a point, 0 when there is none.
layout(binding = OCEAN_SLOT_SHORE) uniform sampler2D u_oceanShoreMap;
uniform vec4 u_oceanShore;

// Ripple field @p layer at @p uv, cubic B-spline filtered (shaders/bspline.glsl): foam's threshold
// over a field magnified on screen would pick out every texel's square of a bilinear read.
vec4 smoothRipples(vec2 uv, float layer) {
    vec2 at0, at1, g0, g1;
    bsplineReads(uv, vec2(textureSize(u_ripples, 0).xy), at0, at1, g0, g1);
    const vec4 near0 = texture(u_ripples, vec3(at0.x, at0.y, layer));
    const vec4 near1 = texture(u_ripples, vec3(at1.x, at0.y, layer));
    const vec4 far0  = texture(u_ripples, vec3(at0.x, at1.y, layer));
    const vec4 far1  = texture(u_ripples, vec3(at1.x, at1.y, layer));
    return g0.y * (g0.x * near0 + g1.x * near1) + g1.y * (g0.x * far0 + g1.x * far1);
}

// How much of the shore's square a waterline fades out over at its edges.
const float SHORE_FADE = 0.1;

// Metres from @p point on the XZ plane to whatever breaks the water, and how much of a waterline
// the shore can say there: 1 inside its square, fading to 0 at its edges.
vec2 oceanShoreAt(vec2 point) {
    const vec2 uv   = vec2(point.x - u_oceanShore.x, u_oceanShore.y - point.y) / u_oceanShore.z;
    const vec2 edge = min(uv, 1.0 - uv);
    const float say = clamp(min(edge.x, edge.y) / SHORE_FADE, 0.0, 1.0);
    return vec2(texture(u_oceanShoreMap, uv).r, say);
}

#include "../../surface.glsl"

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

// GTAO for the indirect term (rgb), the cascaded sun's screen shadow (a). Each flag is set for the
// opaque bucket alone: both saw only what the prepass drew.
layout(binding = POST_SLOT_AO) uniform sampler2D u_ao;
uniform int u_hasAO;
uniform int u_hasScreenShadow;

#include "../../ambient.glsl"

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

// The sheen lobe's directional albedo at this angle to the normal, from the BRDF LUT's third
// channel.
float sheenAlbedo(float NdotX) {
    return texture(u_brdfLUT, vec2(NdotX, u_material.sheenColor.a)).b;
}

// The sheen colour's strongest channel, 0 for no sheen.
float sheenStrength() {
    vec3 sheenColor = u_material.sheenColor.rgb;
    return max(sheenColor.r, max(sheenColor.g, sheenColor.b));
}

// How much of the light a sheen takes at this angle, its albedo times its strength: the layer
// under it keeps the rest (KHR_materials_sheen's albedo scaling).
float sheenTaken(float NdotX) {
    return sheenStrength() * sheenAlbedo(NdotX);
}

bool hasSheen() {
    return FEATURES && sheenStrength() > 0.0;
}

// The GGX alpha along an anisotropic material's direction, from @p a across it
// (KHR_materials_anisotropy): rougher along it, as rough as authored across.
float anisotropicAlpha(float a) {
    return mix(a, 1.0, u_material.anisotropy * u_material.anisotropy);
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

// The direct lights' specular, summed apart as evaluateLight and the area lights shade it, so a
// transmissive surface keeps its reflections when its colour blends toward what it refracts.
vec3 g_directSpecular = vec3(0.0);

// energyCompensation restores multi-bounce energy (Filament's
// 1 + f0 (1 / Ess - 1)), the direct half of environmentEnergy's correction.
// sourceTan is the tan of the light's angular radius (0 for a point); the lobe
// widens over the disc unrenormalised, spreading its irradiance instead of a
// peak a half float may not hold.
vec3 evaluateLight(
    vec3 N,
    vec3 Ng,
    vec3 V,
    vec3 L,
    vec3 T,
    vec3 B,
    Surface s,
    vec3 f0,
    vec3 energyCompensation,
    vec3 radiance,
    float sourceTan,
    vec3 Ls,
    float sphereNorm
) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float a = areaBroadenedAlpha(s.roughness * s.roughness, sourceTan, 1.0);

    // Base specular: anisotropic when configured, isotropic otherwise. A sphere light's
    // isotropic lobe is toward its representative point Ls (Karis), renormalised by
    // sphereNorm; Ls is L for a point source. specNoL is the cosine the lobe takes.
    float D, Vis;
    float specNoL = NdotL;
    if (FEATURES && u_material.anisotropy > 0.001) {
        vec3 aT = anisotropyTangent(T, B, N);
        vec3 aB = cross(N, aT);   // unit: N and aT are unit and perpendicular
        float at = max(anisotropicAlpha(a), 1e-3);
        float ab = max(a, 1e-3);
        D   = distributionGGXAniso(NdotH, dot(aT, H), dot(aB, H), at, ab);
        Vis = visSmithAniso(at, ab, dot(aT, V), dot(aB, V), NdotV, dot(aT, L), dot(aB, L), NdotL);
    } else {
        vec3 Hs = normalize(V + Ls);
        specNoL = max(dot(N, Ls), 0.0);
        D       = distributionGGX(max(dot(N, Hs), 0.0), a) * sphereNorm;
        Vis     = visSmithCorrelated(NdotV, specNoL, a);
    }
    vec3 F = fresnelSchlick(VdotH, f0);
    vec3 specular = D * Vis * F * energyCompensation;

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

    // Clearcoat: a dielectric GGX lobe dimming the base by its Fresnel.
    vec3 ccContrib = vec3(0.0);
    float underCoat = 1.0;
    float ccStrength = s.clearcoat;
    if (FEATURES && ccStrength > 0.001) {
        // On the geometric normal: the coat is smooth over the base's bumps (KHR_materials_clearcoat
        // without a coat normal map), so lacquer over bumpy wood keeps a mirror top.
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
        float sheenV     = visCharlie(NdotV, NdotL, sheenAlpha);
        sheen      = u_material.sheenColor.rgb * (sheenD * sheenV * NdotL) * underCoat;
        underSheen = 1.0 - max(sheenTaken(NdotV), sheenTaken(NdotL));
    }

    g_directSpecular += (specular * specNoL * underCoat * underSheen + ccContrib) * radiance;
    return ((baseLit + transmitted + sss) * underSheen + ccContrib + sheen) * radiance;
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
// The LUT integrates Schlick's Fresnel over the lobe, A = int (1 - Fc) and B = int Fc, so the
// single-scatter energy is f0 * A + B: a view-angle Fresnel in place of f0 counts it twice,
// up to twice too bright toward grazing (Fdez-Aguera's equation 16, not his listing).
//
// @param dfg    The split-sum LUT's scale and bias.
// @param f0     Normal-incidence reflectance.
// @param FssEss Out: the single-scatter energy, what a trace can replace.
// @param FmsEms Out: the multi-scatter energy, lit by the irradiance.
void environmentEnergy(vec2 dfg, vec3 f0, out vec3 FssEss, out vec3 FmsEms) {
    FssEss = f0 * dfg.x + dfg.y;
    // Clamped: a bilinear tap at the LUT's edge can push dfg.x + dfg.y past one.
    float Ems  = clamp(1.0 - (dfg.x + dfg.y), 0.0, 1.0);
    vec3  Favg = f0 + (1.0 - f0) / 21.0;
    FmsEms = FssEss * Favg / max(1.0 - Ems * Favg, vec3(1e-4)) * Ems;
}

void main() {
    ReflectWeight = vec4(0.0);
    ReflectEnv    = vec4(0.0);

    vec3 V = toViewer(vWorldPos);

    vec3 Ng = normalize(vNormal);
    vec3 tangent = vTangent;
    // The ocean's water: the waves' frame as the vertices left it, with the ripples this pixel can show.
    const bool water = u_material.waves != 0 && oceanPresent();
    OceanShade sea;
    float      seaFoam = 0.0;  // whitecaps and waterlines
    if (water) {
        // With no ripples the fields are not summed, and their unit holds another pass's texture.
        vec4 fields[OCEAN_RIPPLE_FIELDS];
        for (int f = 0; f < OCEAN_RIPPLE_FIELDS; ++f) fields[f] = vec4(0.0);
        if (u_ocean.squeeze.y > 0.0) {
            // The longest ripples' texels are the widest on screen: those are B-spline filtered.
            fields[0] = smoothRipples(vRest * u_ocean.tiles.x, 0.0);
            fields[1] = texture(u_ripples, vec3(vRest * u_ocean.tiles.y, 1.0));
            fields[2] = texture(u_ripples, vec3(vRest * u_ocean.tiles.z, 2.0));
        }
        sea = oceanShadeAt(vRest, vAlongX, vAlongZ, fields);
        // Foam's share of the ripples dies away with the waves, as their light does.
        for (int f = 0; f < OCEAN_RIPPLE_FIELDS; ++f) fields[f] *= oceanReach(vRest);
        // Foam where a crest squeezes the water now or did a moment ago, its edge torn by the ripples.
        const float whitecap = max(sea.pressed, vLingering) + oceanTear(fields);
        seaFoam = oceanFoam(whitecap, fwidth(whitecap));
        // A waterline: water near whatever breaks it (the shore), torn by the ripples and
        // washed out and back by the waves.
        if (u_oceanShore.w > 0.0) {
            const vec2  shore = oceanShoreAt(vWorldPos.xz);
            const float past  = oceanWaterline(shore.x, u_oceanShore.w, fields, oceanCrest(vHeight));
            seaFoam = max(seaFoam, shore.y * oceanFroth(past, fwidth(past)));
        }
        Ng      = sea.normal;
        tangent = sea.tangent;
    }
    // Interpolation skews the frame; re-orthogonalise, or normal maps and the
    // anisotropic lobe read a tilted basis.
    vec3 tanFlat = tangent - Ng * dot(Ng, tangent);
    // A tangent interpolated onto the normal keeps the frame it was given.
    vec3 T = (dot(tanFlat, tanFlat) > 1e-8) ? normalize(tanFlat) : normalize(tangent);

    // The sign cross cannot recompute: a mirrored UV shell is left-handed.
    vec3 B  = (vHandedness < 0.0) ? -cross(Ng, T) : cross(Ng, T);
    // A double-sided material's back face is lit as its own: the whole frame turns, so the
    // normal map mirrors with it (the glTF sample viewer's). Single-sided ones never show one.
    if (!gl_FrontFacing) {
        T  = -T;
        B  = -B;
        Ng = -Ng;
    }
    mat3 TBN = mat3(T, B, Ng);

    vec2 uv   = vUV;
    vec2 uvDx = dFdx(vUV);
    vec2 uvDy = dFdy(vUV);
    if (FEATURES && hasTex(MATERIAL_SLOT_HEIGHT) && u_material.heightScale > 0.0) {
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

    // The water's scattering colour, its strength in it.
    const vec3 scatter = water ? u_material.subsurfaceColor.rgb * u_material.subsurface : vec3(0.0);
    if (water) {
        // Ripples too fine for the pixel roughen it: GGX's alpha squared takes twice their slope
        // variance, as Beckmann's would.
        float alpha = s.roughness * s.roughness;
        s.roughness = sqrt(sqrt(alpha * alpha + 2.0 * sea.variance));
        // Whitecaps: where the crests squeeze the surface, foam over the water.
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

    vec3 N = getNormal(uv, Ng, TBN);

    // The coat too, on its own smooth normal: it is smoother and shimmers first.
    s.roughness          = specularAA(N, s.roughness);
    s.clearcoatRoughness = specularAA(Ng, s.clearcoatRoughness);

    float f0Dielectric = pow((u_material.ior - 1.0) / (u_material.ior + 1.0), 2.0);
    vec3  f0 = mix(vec3(f0Dielectric), s.albedo, s.metallic);

    float NdotV = max(dot(N, V), 1e-4);

    // Read once for the environment, the reflection weight and the direct
    // energy compensation; baked at startup, so valid with no sky.
    vec2 dfg = texture(u_brdfLUT, vec2(NdotV, s.roughness)).rg;
    vec3 energyCompensation = 1.0 + f0 * (1.0 / max(dfg.x + dfg.y, 1e-4) - 1.0);

    // Only these light a fragment from behind; others skip such a light before its shadow.
    bool hasBack = hasSubsurface() || (FEATURES && s.transmission > 0.001) || water;

    vec3 Lo = vec3(0.0);

    // The contacts the cascades are too coarse for, the cascaded sun's alone.
    float screenShadow = 1.0;
    if (u_hasScreenShadow == 1) screenShadow = texture(u_ao, gl_FragCoord.xy / u_camera.viewport).a;

    uint count;
    int  ci = 0;
    if (u_useClusters == 1) {
        vec2  clusterUV    = gl_FragCoord.xy / u_camera.viewport;
        float clusterDepth = linearizeViewDepth(gl_FragCoord.z, u_camera.invProjection);
        // Its lights are where it is, not where its own projection drew it.
        if (vViewmodel != 0u) {
            vec4 seen    = u_camera.viewProjection * vec4(vWorldPos, 1.0);
            clusterUV    = seen.xy / seen.w * 0.5 + 0.5;
            clusterDepth = -(u_camera.view * vec4(vWorldPos, 1.0)).z;
        }
        ci = clusterIndex(clusterUV, clusterDepth, u_camera.zNear, u_camera.zFar);
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

            float area;
            float formFactor = areaFormFactor(type, lightPos, U, Vv, vWorldPos, N, facing, area);

            // Fresnel at the centre sets the split: the polygon has no single H.
            vec3 Lc = toCenter / max(dist, 1e-4);
            vec3 Fc = fresnelSchlick(max(dot(V, normalize(V + Lc)), 0.0), f0);
            vec3 kd = (vec3(1.0) - Fc) * (1.0 - s.metallic);
            vec3 diffuseArea = kd * s.albedo * formFactor / max(area, 1e-4);

            // Karis's representative point, renormalised by (alpha / alpha')^2 (lighting.md).
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
                float D       = distributionGGX(max(dot(N, Hcp), 0.0), aGGX);
                float Vis     = visSmithCorrelated(NdotV, NdotLcp, aGGX);
                specularArea  = D * Vis * Fcp * NdotLcp * norm * energyCompensation
                    / max(dist * dist, 1e-4);
            }

            Lo += lightCol * intensity * window * (diffuseArea + specularArea);
            g_directSpecular += lightCol * intensity * window * specularArea;
            continue;
        }

        vec3  L;
        float atten = punctualAttenuation(light, type, vWorldPos, L);
        if (atten <= 0.0) continue;
        if (!hasBack && dot(N, L) <= 0.0) continue;

        // The bias follows the geometric surface, not the normal map.
        float visibility = 1.0;

        visibility *= surfaceShadow(type, int(light.spot.w), vWorldPos, Ng, dot(Ng, L), screenShadow);

        // POM self-shadowing for the sun alone, bounding the trace cost.
        if (FEATURES
            && type == LIGHT_DIRECTIONAL
            && hasTex(MATERIAL_SLOT_HEIGHT)
            && u_material.heightScale > 0.0
            && visibility > 0.0) {
            vec3 lightDirTS = normalize(transpose(TBN) * L);
            visibility *= parallaxShadow(uv, lightDirTS, uvDx, uvDy);
        }

        // Micro-shadowing (Chan, CoD WWII; Filament): the material's own AO is the cone a cavity
        // stays open over, so a grazing light leaves mortar and creases dark rather than flat.
        // Front-lit only; the back-lights through it are the subsurface's and transmission's.
        float NoLs = dot(N, L);
        if (s.ao < 1.0 && NoLs > 0.0) {
            float micro = clamp(NoLs * inversesqrt(1.0 - min(s.ao, 0.9999)), 0.0, 1.0);
            visibility *= micro * micro;
        }

        // A point or spot of radius r is a sphere (Karis 2013), its lobe renormalised by
        // (a / a')^2 with a' = a + r / 2d (lighting.md).
        vec3  Ls         = L;
        float sphereNorm = 1.0;
        float sphereR    = light.axisV.w;
        if (sphereR > 0.0 && (type == LIGHT_POINT || type == LIGHT_SPOT)) {
            vec3  toLight = light.position.xyz - vWorldPos;
            vec3  R       = reflect(-V, N);
            vec3  toRay   = dot(toLight, R) * R - toLight;
            vec3  closest = toLight + toRay * clamp(sphereR / max(length(toRay), 1e-4), 0.0, 1.0);
            Ls = normalize(closest);
            float a      = max(s.roughness * s.roughness, 1e-3);
            float aWide  = clamp(a + 0.5 * sphereR / max(length(toLight), 1e-4), 0.0, 1.0);
            sphereNorm   = (a / aWide) * (a / aWide);
        }

        // spot.z: tan of a directional's disc, 0 otherwise.
        vec3 radiance = lightCol * intensity * atten * visibility;
        Lo += evaluateLight(
            N,
            Ng,
            V,
            L,
            T,
            B,
            s,
            f0,
            energyCompensation,
            radiance,
            light.spot.z,
            Ls,
            sphereNorm
        );
        if (water) {
            const vec3 into = 1.0 - fresnelSchlick(NdotV, f0);
            Lo += into * oceanScatter(scatter, N, V, L, radiance, oceanCrest(vHeight));
        }
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

    // An anisotropic lobe reflects about a normal bent toward its stretch, the more the rougher
    // across it (the glTF sample viewer's): the bend across the surface draws the stretch. The
    // trace, which reflects about the G-buffer's normal, is handed it as wide as its widest axis,
    // so a strong one keeps this reflection rather than one about the wrong normal.
    vec3  R             = reflect(-V, N);
    float lobeRoughness = sqrt(anisotropicAlpha(s.roughness * s.roughness));  // its widest axis
    if (FEATURES && u_material.anisotropy > 0.001) {
        vec3  aB     = cross(N, anisotropyTangent(T, B, N));
        vec3  across = cross(cross(aB, V), aB);
        float bend   = 1.0 - u_material.anisotropy * (1.0 - s.roughness);
        R = reflect(-V, normalize(mix(across, N, bend * bend * bend * bend)));
    }

    // What the environment's specular takes, and the diffuse what is left (Fdez-Aguera, section 4).
    vec3 FssEss;
    vec3 FmsEms;
    environmentEnergy(dfg, f0, FssEss, FmsEms);
    vec3 kD = (1.0 - (FssEss + FmsEms)) * (1.0 - s.metallic);

    // Horizon occlusion drops reflections a normal map turns below the
    // geometric surface.
    float horizon = min(1.0 + dot(R, Ng), 1.0);

    // Where a rough lobe peaks: GGX's dominant direction bends from R toward N as roughness
    // grows (Lagarde, Moving Frostbite to PBR 4.9.3), so a rough floor at grazing does not
    // reflect the bright horizon R points at. The coat reflects off its smooth top (Rc).
    float rdA = s.roughness * s.roughness;
    vec3  Rd  = normalize(mix(N, R, (1.0 - rdA) * (sqrt(1.0 - rdA) + rdA)));
    float specOcc = specularOcclusion(NdotV, ao, s.roughness * s.roughness) * horizon * horizon;

    // environmentEnergy's single-scatter half, which a trace can replace (the
    // multi-scatter lobe it cannot), dimmed by a clear coat's Fresnel.
    // Not occluded: a trace that hits has found the occluder itself, so only the environment it
    // replaces (ReflectEnv) carries specOcc.
    vec3  reflectWeight = FssEss;
    float underSheen    = hasSheen() ? 1.0 - sheenTaken(NdotV) : 1.0;
    bool  hasCoat       = FEATURES && s.clearcoat > 0.001;
    float coatNoV       = max(dot(Ng, V), 1e-4);
    float coatFresnel   = hasCoat ? fresnelSchlick(coatNoV, vec3(COAT_F0)).x * s.clearcoat : 0.0;
    vec3  Rc            = reflect(-V, Ng);  // the coat's, on its smooth top
    // The coat's top is never below its own horizon, so it takes no horizon term.
    float coatOcc       = specularOcclusion(coatNoV, ao, s.clearcoatRoughness * s.clearcoatRoughness);
    reflectWeight *= (1.0 - coatFresnel) * underSheen;
    ReflectWeight = vec4(reflectWeight, lobeRoughness);

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

    // Probes blended over the sky before shading, so one shading serves all (shaders/probes.glsl):
    // the reflection as well as the irradiance, so the loop is this pass's own.
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
            vec3  Rp      = probeParallax(Rd, vWorldPos, center, extents);
            vec4  RpLayer = vec4(Rp, layer);
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

    // The volume, sampled here rather than at a probe's centre or infinity,
    // where it covers; the reflection's own source elsewhere.
    vec3 irradiance = sourceIrradiance;
    if (u_hasIrradianceVolume == 1) {
        // Weighted where it is read, so a box fitted to a room's inside covers its walls.
        vec3  lookup = irradianceVolumeLookup(vWorldPos, Ng);
        float ivw    = irradianceVolumeWeight(lookup);
        if (ivw > 0.0) {
            vec3 volume = sampleIrradianceVolume(lookup, bentN) / PI * u_ivIntensity * u_iblIntensity;
            irradiance = mix(irradiance, volume, ivw);
        }
    }

    // Reflection normalisation (Lazarov): a capture lit more than this point is
    // dimmed by the irradiance ratio, never brightened (the sun is not in it).
    float normalisation = min(luma(irradiance) / max(luma(sourceIrradiance), 1e-4), 1.0);
    prefiltered *= normalisation;
    coat        *= normalisation;

    vec3 diffuseIBL  = irradiance * s.albedo * kD;
    // The water's own colour, lit by the sky it takes in.
    if (water) diffuseIBL += irradiance * scatter * (SCATTER_AMBIENT * kD);
    vec3 specularIBL = prefiltered * FssEss + FmsEms * irradiance;

    vec3 ambient = diffuseIBL * multiBounceOcclusion(ao, s.albedo) + specularIBL * specOcc;
    vec3 ambientSpecular = specularIBL * specOcc;

    // A sheen takes its share of the environment before the base does: its directional albedo of
    // the irradiance, since a lobe as broad as cloth's gathers nearly what a diffuse one does
    // (Filament reads the prefiltered sky at its roughness instead).
    if (hasSheen()) {
        vec3 sheenIBL = u_material.sheenColor.rgb * sheenAlbedo(NdotV) * irradiance * ao;
        ambient         = ambient * underSheen + sheenIBL;
        ambientSpecular = ambientSpecular * underSheen;
    }

    // The coat layer dims everything beneath by its Fresnel (Filament's clear-coat IBL).
    if (hasCoat) {
        ambient         = ambient * (1.0 - coatFresnel) + coat * (coatFresnel * coatOcc);
        ambientSpecular = ambientSpecular * (1.0 - coatFresnel) + coat * (coatFresnel * coatOcc);
    }

    // Zero where nothing was read, so a trace there only adds.
    ReflectEnv = vec4(reflectWeight * specOcc * prefiltered, 1.0);

    vec3 color = ambient + Lo + s.emission;

    if (u_renderMode == MODE_GI_ONLY)     { FragColor = vec4(ambient, 1.0); return; }
    if (u_renderMode == MODE_DIRECT_ONLY) { FragColor = vec4(Lo, 1.0); return; }
    if (u_renderMode == MODE_CLUSTERS) {
        // Green -> yellow -> red over the cluster's light count.
        float t    = clamp(float(count) / float(MAX_LIGHTS_PER_CLUSTER), 0.0, 1.0) * 3.0;
        vec3  heat = mix(vec3(0.02, 0.10, 0.02), vec3(0.15, 0.85, 0.15), clamp(t, 0.0, 1.0));
        heat       = mix(heat, vec3(0.95, 0.85, 0.10), clamp(t - 1.0, 0.0, 1.0));
        heat       = mix(heat, vec3(0.95, 0.10, 0.10), clamp(t - 2.0, 0.0, 1.0));
        // Magenta where the list is full: lights past it are dropped, and show as a cut.
        if (count >= uint(MAX_LIGHTS_PER_CLUSTER)) heat = vec3(0.95, 0.10, 0.95);
        FragColor  = vec4(heat, 1.0);
        return;
    }

    // Before refraction: its scene copy is already fogged. The reflection inputs
    // lose what the colour loses.
    vec4 fog = fragmentFog();
    color = color * fog.a + fog.rgb;
    ReflectWeight.rgb *= fog.a;
    ReflectEnv.rgb    *= fog.a;

    // Screen-space refraction. What the surface reflects stays whole and only the rest blends
    // toward the scene behind, itself dimmed by the reflected share (Filament), so glass keeps
    // its reflections and highlights.
    if (FEATURES && u_hasSceneColor == 1 && s.transmission > 0.0) {
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
            vec3 reflected = (ambientSpecular + g_directSpecular) * fog.a;
            transmitted *= 1.0 - FssEss;
            color = mix(color - reflected, transmitted, s.transmission * (1.0 - s.metallic)) + reflected;
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
