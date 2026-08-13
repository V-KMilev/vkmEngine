/*
 * Forward PBR ubershader - implements the full MaterialAsset spec.
 *
 * Metal-rough Cook-Torrance base (GGX + height-correlated Smith), isotropic
 * or anisotropic. Clearcoat (second dielectric lobe that attenuates the
 * base), Charlie sheen, subsurface wrap + back translucency, thin
 * transmission with Beer-Lambert volume absorption, parallax-occlusion
 * mapping with self-shadowing, alpha-test (AlphaMask) and Unlit passthrough.
 * Local reflection probes (parallax-corrected) blended over the global IBL.
 *
 * Every optional lobe branches at runtime on the material UBO - one shared
 * program for all materials (there is no compile-time variant cache).
 */

#include "../../_generated/engine_config.glsl"  // MAX_LIGHTS, MAX_SHADOW_CASTERS_2D/_CUBE (generated from engine_config.h)
#include "../../_common/lights.glsl"            // Light + LightsBlock + cluster grid + LIGHT_* + falloff
#include "../../_common/depth.glsl"
#include "../../_common/normal_codec.glsl"           // signNotZero, octDecode (GTAO bent normal)

// Texture-present flags - bit position matches GLBindings::TextureSlots (C++).
#define TEX_ALBEDO                (1 << 0)
#define TEX_NORMAL                (1 << 1)
#define TEX_METALLIC_ROUGHNESS    (1 << 2)
#define TEX_AO                    (1 << 3)
#define TEX_EMISSION              (1 << 4)
#define TEX_HEIGHT                (1 << 5)
#define TEX_CLEARCOAT             (1 << 6)
#define TEX_TRANSMISSION          (1 << 7)
#define TEX_METALLIC              (1 << 8)
#define TEX_ROUGHNESS             (1 << 9)
#define TEX_AO_METALLIC_ROUGHNESS (1 << 10)

// MaterialType (material_asset.h).
#define MAT_OPAQUE      0
#define MAT_TRANSPARENT 1
#define MAT_UNLIT       2
#define MAT_ALPHA_MASK  3

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vTangent;
in vec3 vBitangent;

out vec4 FragColor;

#include "../../_common/material.glsl"

#include "../../_common/camera.glsl"

// Shadows: the ShadowBlock UBO, atlas/cube samplers, and the per-light-type
// sampling functions live in a shared include - the volumetric fog scatters the
// sun through the very same cascades.
#include "../../_common/shadows.glsl"

layout(binding = 0)  uniform sampler2D u_albedoTexture;
layout(binding = 1)  uniform sampler2D u_normalTexture;
layout(binding = 2)  uniform sampler2D u_metallicRoughnessTexture;
layout(binding = 3)  uniform sampler2D u_aoTexture;
layout(binding = 4)  uniform sampler2D u_emissionTexture;
layout(binding = 5)  uniform sampler2D u_heightTexture;
layout(binding = 6)  uniform sampler2D u_clearcoatTexture;
layout(binding = 7)  uniform sampler2D u_transmissionTexture;
layout(binding = 8)  uniform sampler2D u_metallicTexture;
layout(binding = 9)  uniform sampler2D u_roughnessTexture;
layout(binding = 10) uniform sampler2D u_aoMetallicRoughnessTexture;

// Image-based lighting (split-sum). Bound by the forward pass when the backend
// has a baked environment; u_hasIBL gates the sampling so an unbaked frame
// falls back to flat ambient. Slots match GLBindings::IBLTextureSlots (C++).
layout(binding = 14) uniform samplerCube u_irradiance;  // diffuse irradiance
layout(binding = 15) uniform samplerCube u_prefilter;   // roughness-prefiltered specular
layout(binding = 16) uniform sampler2D   u_brdfLUT;      // split-sum BRDF/DFG LUT
uniform int u_hasIBL;
uniform float u_iblIntensity;  // environment intensity: scales the indirect (IBL / flat ambient) term
uniform int   u_renderMode;    // MODE_* debug view; 0 (default) everywhere but the main view
// Highest prefilter mip index; matches GLIBL::PREFILTER_MIPS - 1 (C++).

// Scene-color copy (opaque + sky) for screen-space transmission refraction.
// The forward pass binds it for the transparent bucket; u_hasSceneColor gates it.
layout(binding = 18) uniform sampler2D u_sceneColor;
uniform int  u_hasSceneColor;
uniform vec2  u_screenSize;
uniform float u_zNear;        // camera near plane (cluster depth-slice mapping)
uniform float u_zFar;         // camera far plane
uniform int   u_useClusters;  // 1 = per-cluster light list, 0 = full list (preview / probe bake)
uniform mat4  u_invView;      // view -> world, to bring the GTAO bent normal into world space

// Screen-space ambient occlusion (GTAO). Bound by the forward pass when the
// GTAO pass ran this frame; u_hasSSAO gates it. Modulates the indirect term.
layout(binding = 21) uniform sampler2D u_ao;
uniform int u_hasSSAO;

// Baked irradiance volume (SH-L1 on a probe grid): indirect diffuse for anything
// inside its box. u_hasIrradianceVolume gates it (0 in the bakes, which must not
// sample the volume they are producing).
layout(binding = 26) uniform sampler3D u_shVolume0;
layout(binding = 27) uniform sampler3D u_shVolume1;
layout(binding = 28) uniform sampler3D u_shVolume2;
layout(binding = 29) uniform sampler3D u_shVolume3;
uniform int   u_hasIrradianceVolume;
uniform vec3  u_ivMin;        // volume box min corner (world)
uniform vec3  u_ivSize;       // volume box size (world)
uniform float u_ivIntensity;

// Screen-space sun contact-shadow mask; u_hasContactShadow gates it (0 in the
// preview / probe bakes, which run no contact-shadow pass).
layout(binding = 25) uniform sampler2D u_contactShadow;
uniform int u_hasContactShadow;

// Local reflection probes (parallax-corrected boxes), stored as cube-map arrays
// (layer = probe index). The backend binds two array samplers + the ProbeBlock
// UBO; the shader weight-blends the covering probes over the global IBL.
layout(binding = 22) uniform samplerCubeArray u_probeIrr;
layout(binding = 23) uniform samplerCubeArray u_probePref;
uniform int u_probeCount;

struct ProbeEntry {
    vec4 center;    // xyz world centre, w pad
    vec4 extents;   // xyz half-extents, w pad
    vec4 params;    // x falloff, y intensity, z layer, w pad
};
layout(std140, binding = 4) uniform ProbeBlock {
    ProbeEntry probes[MAX_PROBES];
} u_probes;


// Parallax box correction: intersect the reflection ray from worldPos along R
// with the probe box, then return the direction from the box centre to the hit.
// Without it, a cube captured from one point reflects as if infinitely far.
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

// 1 deep inside the box, fading to 0 at the box face over falloff.
float probeWeight(vec3 worldPos, vec3 center, vec3 extents, float falloff) {
    vec3  d = abs(worldPos - center) / max(extents, vec3(1e-3));
    float m = max(max(d.x, d.y), d.z);
    return 1.0 - smoothstep(1.0 - falloff, 1.0, m);
}

#include "../../_common/constants.glsl"

bool hasTex(int flag) {
    return (u_material.textureFlags & flag) != 0;
}

// ---------------------------------------------------------------------------
// Parallax-occlusion mapping
// ---------------------------------------------------------------------------

// Ray-march the height field along the tangent-space view direction, then
// interpolate the crossing for a smooth silhouette. More layers at grazing
// angles.
vec2 parallax(vec2 uv, vec3 viewDirTS) {
    const float MIN_LAYERS = 8.0;
    const float MAX_LAYERS = 32.0;
    float numLayers = mix(MAX_LAYERS, MIN_LAYERS,
        clamp(abs(viewDirTS.z), 0.0, 1.0));

    float layerDepth = 1.0 / numLayers;
    float curLayerDepth = 0.0;

    vec2 maxOffset = (viewDirTS.xy / max(viewDirTS.z, 0.001)) * u_material.heightScale;
    vec2 deltaUV = maxOffset / numLayers;

    vec2  curUV = uv;
    float curH  = texture(u_heightTexture, curUV).r;

    for (int i = 0; i < int(MAX_LAYERS); ++i) {
        if (curLayerDepth >= curH) break;
        curUV -= deltaUV;
        curH = texture(u_heightTexture, curUV).r;
        curLayerDepth += layerDepth;
    }

    vec2  prevUV = curUV + deltaUV;
    float afterD  = curH - curLayerDepth;
    float beforeD = texture(u_heightTexture, prevUV).r - curLayerDepth + layerDepth;
    float w = afterD / (afterD - beforeD);
    return mix(curUV, prevUV, clamp(w, 0.0, 1.0));
}

// POM self-shadowing. After parallax() has displaced the UV onto the virtual
// surface, march a second ray toward the light in tangent space and
// accumulate the worst blocker over the trace. Returns 1.0 = no occlusion,
// 0.0 = fully shadowed.
float parallaxShadow(vec2 uv, vec3 lightDirTS) {
    if (lightDirTS.z <= 0.0) return 1.0;

    const float NUM_LAYERS = 16.0;

    float startDepth = texture(u_heightTexture, uv).r;
    if (startDepth <= 0.0) return 1.0;

    vec2  deltaUV    = (lightDirTS.xy / max(lightDirTS.z, 0.01))
                       * u_material.heightScale / NUM_LAYERS;
    float deltaDepth = startDepth / NUM_LAYERS;

    vec2  curUV    = uv      + deltaUV;
    float curDepth = startDepth - deltaDepth;

    // Track the worst blocker rather than averaging - any taller heightmap
    // value along the path is enough to cast a hard shadow there.
    float maxBlocker = 0.0;
    for (int i = 0; i < int(NUM_LAYERS); ++i) {
        if (curDepth <= 0.0) break;
        float h = texture(u_heightTexture, curUV).r;
        if (h > curDepth) {
            maxBlocker = max(maxBlocker, h - curDepth);
        }
        curUV   += deltaUV;
        curDepth -= deltaDepth;
    }

    return clamp(1.0 - maxBlocker * 8.0, 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Surface
// ---------------------------------------------------------------------------

struct Surface {
    vec3  albedo;
    float opacity;
    float metallic;
    float roughness;
    float ao;
    vec3  emission;
};

Surface sampleSurface(vec2 uv) {
    Surface s;

    s.albedo  = u_material.albedo.rgb;
    s.opacity = u_material.albedo.a;
    if (hasTex(TEX_ALBEDO)) {
        vec4 tex = texture(u_albedoTexture, uv);
        s.albedo  *= tex.rgb;
        s.opacity *= tex.a;
    }

    s.metallic  = u_material.metallic;
    s.roughness = u_material.roughness;
    s.ao        = u_material.ao;
    s.emission  = u_material.emission.rgb * u_material.emission.a;

    // Combined maps win over separate ones when both are present.
    if (hasTex(TEX_AO_METALLIC_ROUGHNESS)) {
        vec3 amr = texture(u_aoMetallicRoughnessTexture, uv).rgb;
        s.ao        *= amr.r;
        s.roughness *= amr.g;
        s.metallic  *= amr.b;
    } else if (hasTex(TEX_METALLIC_ROUGHNESS)) {
        vec3 mr = texture(u_metallicRoughnessTexture, uv).rgb;
        s.roughness *= mr.g;  // glTF: green = roughness
        s.metallic  *= mr.b;  // glTF: blue  = metallic
    } else {
        if (hasTex(TEX_METALLIC))  s.metallic  *= texture(u_metallicTexture,  uv).r;
        if (hasTex(TEX_ROUGHNESS)) s.roughness *= texture(u_roughnessTexture, uv).r;
    }

    if (hasTex(TEX_AO) && !hasTex(TEX_AO_METALLIC_ROUGHNESS)) {
        s.ao *= texture(u_aoTexture, uv).r;
    }
    if (hasTex(TEX_EMISSION)) {
        s.emission *= texture(u_emissionTexture, uv).rgb;
    }

    s.roughness = clamp(s.roughness, 0.045, 1.0);
    return s;
}

// Geometric normal, or the tangent-space normal map mapped through the TBN
// basis when one is bound. normalScale flattens / exaggerates the map.
vec3 getNormal(vec2 uv, vec3 Ng, mat3 tbn) {
    if (!hasTex(TEX_NORMAL)) {
        return Ng;
    }
    vec3 n = texture(u_normalTexture, uv).rgb * 2.0 - 1.0;
    n.xy *= u_material.normalScale;
    return normalize(tbn * normalize(n));
}

// Geometric specular antialiasing - Karis (UE4) / Filament. Raises roughness
// proportionally to screen-space shading-normal variance so high-frequency
// normal detail does not alias into shimmering highlights.
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

// ---------------------------------------------------------------------------
// BRDF lobes
// ---------------------------------------------------------------------------

#include "../../_common/brdf.glsl"  // distributionGGX (takes the GGX alpha)
#include "../../_common/sh_l1.glsl"  // SH_Y*/SH_A*: the irradiance-volume projection <-> evaluation contract
#include "../../_generated/render_modes.glsl"  // SH_Y*/SH_A*: the irradiance-volume projection <-> evaluation contract

// Height-correlated Smith visibility (already folds in the 1/(4 NoL NoV)).
float visSmithCorrelated(float NdotV, float NdotL, float a) {
    float a2 = a * a;
    float gv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float gl = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

// Charlie sheen distribution + Ashikhmin visibility (glTF KHR_materials_sheen
// / Filament cloth). Used for fabric/dust grazing retroreflection.
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

float visSmithAniso(float at, float ab,
                    float ToV, float BoV, float NdotV,
                    float ToL, float BoL, float NdotL) {
    float lambdaV = NdotL * length(vec3(at * ToV, ab * BoV, NdotV));
    float lambdaL = NdotV * length(vec3(at * ToL, ab * BoL, NdotL));
    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

vec3 fresnelSchlick(float u, vec3 f0) {
    float f = pow(clamp(1.0 - u, 0.0, 1.0), 5.0);
    return f0 + (vec3(1.0) - f0) * f;
}

// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------

// LTC area-light integration - diffuse / Lambertian only.
//
// Evaluates the clamped-cosine integral over a planar polygon emitter.
// Inputs are in a tangent frame where the shading normal is (0,0,1); the
// caller transforms vertices into that frame.
//
// ltcEdgeIntegral is Hill's stable approximation of theta/sin(theta),
// avoiding the trig discontinuity near v1.v2 == -1. The published constants
// (Heitz 2016 supplement) match the reference to within FP precision.
//
// The full LTC GGX specular path (multiplying vertices by M^-1(NdotV,
// roughness) before the integral) is not implemented here; specular is
// handled separately via the representative-point method below.
float ltcEdgeIntegral(vec3 v1, vec3 v2) {
    float x = dot(v1, v2);
    float y = abs(x);
    float a = 0.8543985 + (0.4965155 + 0.0145206 * y) * y;
    float b = 3.4175940 + (4.1616724 + y) * y;
    float v = a / b;
    float thetaSinTheta = (x > 0.0)
        ? v
        : 0.5 * inversesqrt(max(1.0 - x * x, 1e-7)) - v;
    return (v1.x * v2.y - v1.y * v2.x) * thetaSinTheta;
}

// Lambertian irradiance from a 4-vertex polygon at the origin with normal
// (0,0,1). Returns a value in [0, pi]; divide by pi for unit-Lambertian
// reflectance. Negative result means the polygon is back-facing (caller
// folds in two-sided handling).
float ltcQuadIrradiance(vec3 p0, vec3 p1, vec3 p2, vec3 p3) {
    vec3 v0 = normalize(p0);
    vec3 v1 = normalize(p1);
    vec3 v2 = normalize(p2);
    vec3 v3 = normalize(p3);
    float sum = 0.0;
    sum += ltcEdgeIntegral(v0, v1);
    sum += ltcEdgeIntegral(v1, v2);
    sum += ltcEdgeIntegral(v2, v3);
    sum += ltcEdgeIntegral(v3, v0);
    return sum;
}

// World-to-tangent rotation for LTC: rows are (T1, T2, N) where T1 is the
// view-tangent component in the N plane and T2 = cross(N, T1).
mat3 ltcTangentFrame(vec3 N, vec3 V) {
    vec3 T1 = normalize(V - N * dot(V, N));
    vec3 T2 = cross(N, T1);
    return transpose(mat3(T1, T2, N));
}

// Representative-point specular for area lights (Karis 2013).
//
// Find the point on the area emitter closest to the mirror reflection ray;
// use it as a point-source for the standard GGX evaluation. The lobe is
// broadened proportional to the emitter's projected solid angle so a wide
// rect produces a wide highlight even when the closest point is at the same
// world position as a point light would be. Cheap, self-contained, and
// gives the perceptually correct "highlight stretches across the rect"
// without LUT data.
vec3 areaRectClosestPoint(vec3 rayOrigin, vec3 rayDir,
                          vec3 center, vec3 axisU, vec3 axisV)
{
    // Plane of the rect: normal is U x V (sign matches the light's forward,
    // since axisU/axisV are derived from the same rotation).
    vec3 n = cross(axisU, axisV);
    float nLen2 = max(dot(n, n), 1e-12);
    n /= sqrt(nLen2);

    // Forward ray-plane intersection; fall back to the closest-point-on-ray
    // when the ray runs parallel or away from the plane.
    float denom = dot(rayDir, n);
    float t = (abs(denom) > 1e-4)
        ? dot(center - rayOrigin, n) / denom
        : -1.0;
    if (t <= 0.0) {
        t = max(0.0, dot(center - rayOrigin, rayDir));
    }
    vec3 hit = rayOrigin + rayDir * t;

    // Clamp the hit into the rect's (U, V) extents. axisU / axisV are
    // half-extents already, so their magnitudes are the rect's bounds in
    // their normalised directions.
    vec3 d = hit - center;
    float uLen = length(axisU);
    float vLen = length(axisV);
    vec3 uNorm = axisU / max(uLen, 1e-4);
    vec3 vNorm = axisV / max(vLen, 1e-4);
    float uCoord = clamp(dot(d, uNorm), -uLen, uLen);
    float vCoord = clamp(dot(d, vNorm), -vLen, vLen);
    return center + uNorm * uCoord + vNorm * vCoord;
}

vec3 areaDiskClosestPoint(vec3 rayOrigin, vec3 rayDir,
                          vec3 center, vec3 axisU, vec3 axisV)
{
    // Disk plane (axisU / axisV have equal magnitude = disk radius, so the
    // cross product is a clean normal regardless of which one happens to
    // align with the user-authored rotation).
    vec3 n = cross(axisU, axisV);
    float nLen2 = max(dot(n, n), 1e-12);
    n /= sqrt(nLen2);

    float denom = dot(rayDir, n);
    float t = (abs(denom) > 1e-4)
        ? dot(center - rayOrigin, n) / denom
        : -1.0;
    if (t <= 0.0) {
        t = max(0.0, dot(center - rayOrigin, rayDir));
    }
    vec3 hit = rayOrigin + rayDir * t;

    // Project the offset into the disk plane and clamp to the radius.
    vec3 d = hit - center;
    d -= n * dot(d, n);
    float radius = length(axisU);
    float dLen   = length(d);
    if (dLen > radius) d *= (radius / dLen);
    return center + d;
}

// Broaden the GGX lobe to cover the light source's projected solid angle.
// Karis (2013) Eq. 16: alpha' = saturate(alpha + r / (3*dist)). r is the
// source's representative radius (half the longest extent for a rect, the
// disk radius for a disk).
float areaBroadenedAlpha(float alpha, float sourceRadius, float dist) {
    return clamp(alpha + sourceRadius / max(3.0 * dist, 1e-4), 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Per-light shading
// ---------------------------------------------------------------------------

vec3 evaluateLight(vec3 N, vec3 V, vec3 L, vec3 T, vec3 B, Surface s, vec3 f0, vec3 radiance) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    // Materials with subsurface or transmission can light back-facing
    // fragments; otherwise we early out on NdotL == 0.
    bool hasBack = (u_material.subsurface > 0.001) || (u_material.transmission > 0.001);
    if (NdotL <= 0.0 && !hasBack) return vec3(0.0);

    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float a = s.roughness * s.roughness;

    // Base specular: anisotropic when configured, isotropic otherwise.
    float D, Vis;
    if (u_material.anisotropy > 0.001) {
        // Project the authored direction into the shading plane and fall back
        // to the geometric tangent when it is degenerate. The direction is a
        // free vector in the material, so it can arrive zeroed or parallel to
        // N - normalize() of either is NaN, and a NaN here poisons D, Vis and
        // the whole fragment.
        vec3 aT = T * u_material.anisotropyDirection.x +
                  B * u_material.anisotropyDirection.y +
                  N * u_material.anisotropyDirection.z;
        aT -= N * dot(aT, N);
        float aTLen = length(aT);
        aT = (aTLen > 1e-4) ? aT / aTLen : T;
        vec3 aB = cross(N, aT);   // unit: N and aT are unit and perpendicular
        float at = max(a * (1.0 + u_material.anisotropy), 1e-3);
        float ab = max(a * (1.0 - u_material.anisotropy), 1e-3);
        D   = distributionGGXAniso(NdotH, dot(aT, H), dot(aB, H), at, ab);
        Vis = visSmithAniso(at, ab, dot(aT, V), dot(aB, V), NdotV,
                                    dot(aT, L), dot(aB, L), NdotL);
    } else {
        D   = distributionGGX(NdotH, a);
        Vis = visSmithCorrelated(NdotV, NdotL, a);
    }
    vec3 F = fresnelSchlick(VdotH, f0);
    vec3 specular = D * Vis * F;

    // Diffuse with optional subsurface wrap: light bleeds past the
    // terminator, tinted toward subsurfaceColor in the terminator zone so
    // the lit hemisphere stays faithful to the albedo.
    vec3 kd = (vec3(1.0) - F) * (1.0 - s.metallic);
    vec3 diffuse = kd * s.albedo / PI;
    float diffNoL = NdotL;
    vec3  diffTint = vec3(1.0);
    if (u_material.subsurface > 0.001) {
        float w      = u_material.subsurface;
        float rawNoL = dot(N, L);
        diffNoL  = clamp((rawNoL + w) / ((1.0 + w) * (1.0 + w)), 0.0, 1.0);
        float blend = smoothstep(w, -w, rawNoL);
        diffTint = mix(vec3(1.0), u_material.subsurfaceColor.rgb, blend);
    }

    // Thin transmission: reduce front diffuse, add a back-lit term. A
    // transmission mask narrows it; declared volume tints the transmitted
    // light by Beer-Lambert absorption through the thickness.
    vec3 transmitted = vec3(0.0);
    if (u_material.transmission > 0.001) {
        float kt = u_material.transmission;
        if (hasTex(TEX_TRANSMISSION)) kt *= texture(u_transmissionTexture, vUV).r;
        kt *= (1.0 - s.metallic);
        diffuse *= (1.0 - kt);
        transmitted = kt * s.albedo / PI * max(dot(-N, L), 0.0);
        if (u_material.thicknessFactor > 0.0) {
            vec3 transmittance = pow(
                max(u_material.attenuationColor.rgb, vec3(1e-4)),
                vec3(u_material.thicknessFactor / max(u_material.attenuationColor.a, 1e-4)));
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

    // Clearcoat: a second, always-dielectric GGX lobe that also attenuates
    // the base layer by its Fresnel. A mask map scales the strength.
    vec3 ccContrib = vec3(0.0);
    float ccStrength = u_material.clearcoat;
    if (ccStrength > 0.001) {
        if (hasTex(TEX_CLEARCOAT)) ccStrength *= texture(u_clearcoatTexture, vUV).r;
        float ccRough = clamp(u_material.clearcoatRoughness, 0.045, 1.0);
        float cca = ccRough * ccRough;
        float ccD = distributionGGX(NdotH, cca);
        float ccV = visSmithCorrelated(NdotV, NdotL, cca);
        float ccF = fresnelSchlick(VdotH, vec3(0.04)).x * ccStrength;
        baseLit *= (1.0 - ccF);
        ccContrib = vec3(ccD * ccV * ccF) * NdotL;
    }

    // Sheen / cloth lobe (Charlie). Disabled when sheenColor is black.
    vec3 sheen = vec3(0.0);
    vec3 sheenColor = u_material.sheenColor.rgb;
    if (max(sheenColor.r, max(sheenColor.g, sheenColor.b)) > 0.0) {
        float sheenD = distributionCharlie(NdotH, u_material.sheenColor.a);
        float sheenV = visAshikhmin(NdotV, NdotL);
        sheen = sheenColor * (sheenD * sheenV * NdotL);
    }

    return (baseLit + ccContrib + transmitted + sss + sheen) * radiance;
}

// Fade the volume out near its faces so its edge is not a hard seam against the
// global IBL. 0 outside the box, 1 well inside.
float irradianceVolumeWeight(vec3 worldPos) {
    vec3 uvw = (worldPos - u_ivMin) / max(u_ivSize, vec3(1e-4));
    if (any(lessThan(uvw, vec3(0.0))) || any(greaterThan(uvw, vec3(1.0)))) return 0.0;
    vec3  toFace = min(uvw, 1.0 - uvw);                 // distance to the nearest face, in [0, 0.5]
    float d      = min(min(toFace.x, toFace.y), toFace.z);
    return clamp(d / 0.05, 0.0, 1.0);                   // fade across the outer 5%
}

// Irradiance from the volume's SH-L1 for normal @p n. The stored coefficients are
// radiance-projected; this applies the cosine-lobe convolution (Ramamoorthi).
vec3 sampleIrradianceVolume(vec3 worldPos, vec3 n) {
    vec3 uvw = clamp((worldPos - u_ivMin) / max(u_ivSize, vec3(1e-4)), 0.0, 1.0);
    vec3 sh0 = texture(u_shVolume0, uvw).rgb;
    vec3 sh1 = texture(u_shVolume1, uvw).rgb;
    vec3 sh2 = texture(u_shVolume2, uvw).rgb;
    vec3 sh3 = texture(u_shVolume3, uvw).rgb;

    vec3 E = SH_A0 * SH_Y0 * sh0 + SH_A1 * SH_Y1 * (n.y * sh1 + n.z * sh2 + n.x * sh3);
    return max(E, vec3(0.0));
}

// Specular occlusion (Filament): AO alone over-darkens rough specular and
// under-darkens smooth, so weight it by view angle and roughness.
float specularOcclusion(float NoV, float ao, float roughness) {
    return clamp(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

// ---------------------------------------------------------------------------

// Which cluster this fragment falls in: screen tile from gl_FragCoord, depth
// slice from the linear view depth (matches the cull compute's exponential
// slicing). Must agree with the grid the compute pass wrote.
int clusterIndex(vec3 worldPos) {
    vec2 tileSize = u_screenSize / vec2(float(CLUSTER_X), float(CLUSTER_Y));
    uint tx = uint(clamp(gl_FragCoord.x / tileSize.x, 0.0, float(CLUSTER_X - 1)));
    uint ty = uint(clamp(gl_FragCoord.y / tileSize.y, 0.0, float(CLUSTER_Y - 1)));

    float vd = dot(worldPos - u_camera.cameraPosition.xyz, u_shadow.camForward.xyz);
    float slice = viewDepthToSlice(vd, u_zNear, u_zFar, float(CLUSTER_Z));
    uint tz = uint(clamp(floor(slice), 0.0, float(CLUSTER_Z - 1)));

    return int(tx + ty * uint(CLUSTER_X) + tz * uint(CLUSTER_X * CLUSTER_Y));
}

void main() {
    vec3 V = normalize(u_camera.cameraPosition.xyz - vWorldPos);

    vec3 T  = normalize(vTangent);
    vec3 B  = normalize(vBitangent);
    vec3 Ng = normalize(vNormal);
    mat3 TBN = mat3(T, B, Ng);

    vec2 uv = vUV;
    if (hasTex(TEX_HEIGHT) && u_material.heightScale > 0.0) {
        vec3 viewTS = normalize(transpose(TBN) * V);
        uv = parallax(uv, viewTS);
    }

    // Alpha test for foliage / leaves (glTF alphaMode = MASK). Done before any
    // lighting work so masked-out pixels skip the whole PBR cost. The cutout is
    // sharpened to a ~1px edge and written as coverage (outAlpha below): under
    // MSAA the forward pass enables alpha-to-coverage so this anti-aliases; with
    // A2C off, blending is off too, so any coverage > 0 renders solid - a hard
    // cutout, matching the old behaviour.
    float maskCoverage = 1.0;
    if (u_material.type == MAT_ALPHA_MASK) {
        float aTex = hasTex(TEX_ALBEDO) ? texture(u_albedoTexture, uv).a : 1.0;
        float a    = u_material.albedo.a * aTex;
        maskCoverage = clamp((a - u_material.alphaCutoff) / max(fwidth(a), 1e-5) + 0.5, 0.0, 1.0);
        if (maskCoverage <= 0.0) discard;
    }

    Surface s = sampleSurface(uv);

    // Unlit: albedo + emission straight out, no BRDF, no lights.
    if (u_material.type == MAT_UNLIT) {
        FragColor = vec4(s.albedo + s.emission, s.opacity);
        return;
    }

    vec3 N = getNormal(uv, Ng, TBN);

    // Tame specular shimmer from normal-map detail / curvature.
    s.roughness = specularAA(N, s.roughness);

    // Dielectric F0 from the index of refraction.
    float f0Dielectric = pow((u_material.ior - 1.0) / (u_material.ior + 1.0), 2.0);
    vec3  f0 = mix(vec3(f0Dielectric), s.albedo, s.metallic);

    vec3 Lo = vec3(0.0);

    // Forward+: shade only the lights the cull compute placed in this fragment's
    // cluster. The preview / probe-bake paths run no cull pass, so they set
    // u_useClusters = 0 and fall back to the full light list.
    uint count;
    int  ci = 0;
    if (u_useClusters == 1) {
        ci    = clusterIndex(vWorldPos);
        count = u_clusters.clusters[ci].count;
    } else {
        count = uint(min(u_lights.lightCount, MAX_LIGHTS));
    }
    for (uint k = 0u; k < count; ++k) {
        uint  li    = (u_useClusters == 1) ? u_clusters.clusters[ci].indices[k] : k;
        Light light = u_lights.lights[li];

        vec3  lightPos  = light.position.xyz;
        vec3  lightDir  = normalize(light.direction.xyz);
        vec3  lightCol  = light.color.xyz;
        float intensity = light.color.w;
        float radius    = light.direction.w;
        int   type      = int(light.position.w);

        // Area lights (Rect/Disk): LTC Lambertian diffuse + Karis
        // representative-point GGX specular (broadened alpha). Rect uses
        // its 4 corners; Disk uses a 12-vertex polygon approximation.
        if (type == LIGHT_RECT || type == LIGHT_DISK) {
            vec3  toCenter = lightPos - vWorldPos;
            float dist     = length(toCenter);
            float atten    = distanceAttenuation(dist, radius);
            if (atten <= 0.0) continue;

            vec3 Lc = toCenter / max(dist, 1e-4);

            // Build the tangent frame (N = +Z in local space) and transform
            // the polygon's vertices.
            //
            // Vertex order matters: the Lambert edge formula gives positive
            // irradiance when the polygon is CCW-wound viewed from local +Z
            // (the shading normal). With axisU / axisV oriented so
            // cross(axisU, axisV) points along the EMITTER face, surfaces
            // lit by the emitter front have their normal pointing back at
            // the polygon - which means the polygon appears CW in local
            // space. We pre-reverse the world-space winding (emit corners
            // as bl -> tl -> tr -> br instead of bl -> br -> tr -> tl) so
            // the local frame sees them as CCW and the integral is
            // positive on the lit side.
            mat3 toLocal = ltcTangentFrame(N, V);
            vec3 U  = light.axisU.xyz;
            vec3 Vv = light.axisV.xyz;
            bool twoSided = (light.axisU.w > 0.5);
            float irradiance = 0.0;

            if (type == LIGHT_RECT) {
                vec3 p0 = toLocal * ((lightPos - U - Vv) - vWorldPos);
                vec3 p1 = toLocal * ((lightPos - U + Vv) - vWorldPos);
                vec3 p2 = toLocal * ((lightPos + U + Vv) - vWorldPos);
                vec3 p3 = toLocal * ((lightPos + U - Vv) - vWorldPos);
                irradiance = ltcQuadIrradiance(p0, p1, p2, p3);
            } else {
                // Disk: 12-vertex polygon approximation. A 4-vertex diamond
                // loses ~36% of the disk's area; 12 vertices is dense enough
                // that the silhouette reads as circular.
                const int N_DISK = 12;
                vec3 verts[12];
                // Named apart from the enclosing light loop's k: shadowing a
                // counter inside maths this dense is a trap for whoever edits
                // it next, even though the scopes are distinct.
                for (int dv = 0; dv < N_DISK; ++dv) {
                    float t = -float(dv) / float(N_DISK) * 6.2831853;  // CW order
                    vec3 worldP = lightPos + cos(t) * U + sin(t) * Vv;
                    verts[dv] = normalize(toLocal * (worldP - vWorldPos));
                }
                float sum = 0.0;
                for (int de = 0; de < N_DISK; ++de) {
                    int next = (de + 1) % N_DISK;
                    sum += ltcEdgeIntegral(verts[de], verts[next]);
                }
                irradiance = sum;
            }

            // Negative result means the back of the polygon is facing the
            // surface; twoSided emitters illuminate from either side,
            // otherwise skip.
            if (irradiance < 0.0) {
                if (twoSided) irradiance = -irradiance;
                else continue;
            }

            // Diffuse: LTC Lambertian integral. Fresnel at the area centre
            // is used for the energy split - the standard approximation for
            // area diffuse since the polygon doesn't have a single H.
            vec3  Hc     = normalize(V + Lc);
            float NdotV2 = max(dot(N, V), 1e-4);
            float VdotH2 = max(dot(V, Hc), 0.0);
            vec3  Fc     = fresnelSchlick(VdotH2, f0);
            vec3  kd     = (vec3(1.0) - Fc) * (1.0 - s.metallic);
            vec3  diffuseArea = kd * s.albedo * (irradiance / PI);

            // Specular: Karis representative-point with broadened lobe.
            vec3 specularArea = vec3(0.0);
            vec3 R = reflect(-V, N);
            vec3 closestPoint = (type == LIGHT_RECT)
                ? areaRectClosestPoint(vWorldPos, R, lightPos, U, Vv)
                : areaDiskClosestPoint(vWorldPos, R, lightPos, U, Vv);

            vec3  toCp    = closestPoint - vWorldPos;
            float distCp  = length(toCp);
            vec3  Lcp     = toCp / max(distCp, 1e-4);
            float NdotLcp = max(dot(N, Lcp), 0.0);
            if (NdotLcp > 0.0) {
                float sourceRadius = (type == LIGHT_RECT)
                    ? max(length(U), length(Vv))
                    : length(U);

                float aGGX    = s.roughness * s.roughness;
                float aBroad  = areaBroadenedAlpha(aGGX, sourceRadius, distCp);
                vec3  Hcp     = normalize(V + Lcp);
                float NdotHcp = max(dot(N, Hcp), 0.0);
                float VdotHcp = max(dot(V, Hcp), 0.0);
                vec3  Fcp     = fresnelSchlick(VdotHcp, f0);
                float D       = distributionGGX(NdotHcp, aBroad);
                float Vis     = visSmithCorrelated(NdotV2, NdotLcp, aBroad);
                specularArea  = D * Vis * Fcp * NdotLcp;
            }

            Lo += lightCol * intensity * atten * (diffuseArea + specularArea);
            continue;
        }

        // Punctual lights: directional / point / spot.
        vec3  L;
        float atten = 1.0;

        if (type == LIGHT_DIRECTIONAL) {
            L = normalize(-lightDir);
        } else {
            vec3 toLight = lightPos - vWorldPos;
            float dist = length(toLight);
            L = toLight / max(dist, 1e-4);
            atten = distanceAttenuation(dist, radius);

            if (type == LIGHT_SPOT) {
                float cosInner = cos(light.spot.x);
                float cosOuter = cos(light.spot.y);
                float cd = dot(lightDir, -L);
                float cone = clamp((cd - cosOuter) / max(cosInner - cosOuter, 1e-4), 0.0, 1.0);
                atten *= cone * cone;
            }
        }

        if (atten <= 0.0) continue;

        // Shadow sampling lands here with the shadow pass. Each shadow-casting
        // light carries its atlas slot in spot.w (-1 = none); the type picks the
        // sampling path (directional cascades / spot map / point cube).
        float visibility = 1.0;

        int sslot = int(light.spot.w);
        if (sslot >= 0) {
            float ndotl = dot(N, L);
            if (type == LIGHT_DIRECTIONAL) visibility *= sampleCSM(vWorldPos, N, ndotl, u_camera.cameraPosition.xyz);
            else if (type == LIGHT_SPOT)   visibility *= sample2DSlot(sslot, vWorldPos, N, ndotl);
            else if (type == LIGHT_POINT)  visibility *= sampleCube(sslot, vWorldPos, ndotl);
        }

        // Screen-space contact shadow: catches small-scale sun occlusion the
        // cascades miss. Sun only, and only where still lit.
        if (type == LIGHT_DIRECTIONAL && u_hasContactShadow == 1 && visibility > 0.0) {
            visibility *= texture(u_contactShadow, gl_FragCoord.xy / u_screenSize).r;
        }

        // POM self-shadowing: only for the directional sun so the
        // per-fragment trace cost stays bounded.
        if (type == LIGHT_DIRECTIONAL
            && hasTex(TEX_HEIGHT)
            && u_material.heightScale > 0.0
            && visibility > 0.0) {
            vec3 lightDirTS = normalize(transpose(TBN) * L);
            visibility *= parallaxShadow(uv, lightDirTS);
        }

        vec3 radiance = lightCol * intensity * atten * visibility;
        Lo += evaluateLight(N, V, L, T, B, s, f0, radiance);
    }

    // Indirect light. Split-sum IBL when a baked environment is present:
    // diffuse from the irradiance cube, specular from the roughness-prefiltered
    // cube weighted by the BRDF/DFG LUT. Falls back to flat ambient otherwise.
    // The AO map modulates the indirect term either way.
    // Screen-space AO (GTAO) carries the occlusion factor plus a bent normal -
    // the average unoccluded direction. Sampling irradiance along the bent normal
    // (instead of the geometric normal) keeps creases from over-collecting light.
    float ssao  = 1.0;
    vec3  bentN = N;
    if (u_hasSSAO == 1) {
        vec4 aoSample = texture(u_ao, gl_FragCoord.xy / u_screenSize);
        ssao  = aoSample.r;
        bentN = normalize(mat3(u_invView) * octDecode(aoSample.gb));
    }

    vec3 ambient;
    if (u_hasIBL == 1) {
        float NdotV = max(dot(N, V), 1e-4);
        vec3  R     = reflect(-V, N);

        // Roughness-aware Fresnel: keeps grazing reflections from blowing out
        // on rough surfaces.
        vec3 F  = f0 + (max(vec3(1.0 - s.roughness), f0) - f0) * pow(1.0 - NdotV, 5.0);
        vec3 kD = (1.0 - F) * (1.0 - s.metallic);

        vec3 diffuseIBL  = texture(u_irradiance, bentN).rgb * s.albedo * kD;

        // Baked GI: inside a volume, its SH irradiance replaces the global
        // ambient for the diffuse half (reflections still come from the probes).
        // kD already carries (1 - metallic), so metals are untouched.
        if (u_hasIrradianceVolume == 1) {
            float ivw = irradianceVolumeWeight(vWorldPos);
            if (ivw > 0.0) {
                vec3 volumeDiffuse = (sampleIrradianceVolume(vWorldPos, bentN) / PI)
                                   * u_ivIntensity * s.albedo * kD;
                diffuseIBL = mix(diffuseIBL, volumeDiffuse, ivw);
            }
        }

        vec3 prefiltered = textureLod(u_prefilter, R, s.roughness * MAX_REFLECTION_LOD).rgb;
        vec2 dfg         = texture(u_brdfLUT, vec2(NdotV, s.roughness)).rg;
        vec3 specularIBL = prefiltered * (F * dfg.x + dfg.y);

        // Diffuse takes the raw occlusion; specular takes the angle/roughness-
        // aware form, so a rough surface's reflection is not flatly darkened.
        float specOcc = specularOcclusion(NdotV, ssao, s.roughness);
        ambient = (diffuseIBL * ssao + specularIBL * specOcc) * s.ao;

        // Local probes: weight-blend the covering probes (parallax-corrected
        // reflection + local irradiance) over the global IBL. Reuses F/kD/dfg.
        if (u_probeCount > 0) {
            vec3  probeSum = vec3(0.0);
            float wSum     = 0.0;
            for (int p = 0; p < u_probeCount && p < MAX_PROBES; ++p) {
                vec3  center  = u_probes.probes[p].center.xyz;
                vec3  extents = u_probes.probes[p].extents.xyz;
                float falloff = u_probes.probes[p].params.x;
                float w       = probeWeight(vWorldPos, center, extents, falloff) * u_probes.probes[p].params.y;
                if (w <= 0.0) continue;
                float layer = u_probes.probes[p].params.z;
                vec3  Rp = probeParallax(R, vWorldPos, center, extents);
                vec3  pd = texture(u_probeIrr, vec4(bentN, layer)).rgb * s.albedo * kD;
                vec3  ps = textureLod(u_probePref, vec4(Rp, layer), s.roughness * MAX_PROBE_LOD).rgb * (F * dfg.x + dfg.y);
                probeSum += (pd * ssao + ps * specOcc) * s.ao * w;
                wSum     += w;
            }
            if (wSum > 0.0) ambient = mix(ambient, probeSum / wSum, min(wSum, 1.0));
        }
    } else {
        // Flat ambient fallback (no baked environment). Diffuse-only.
        ambient = vec3(0.03) * s.albedo * s.ao * ssao;
    }

    // Environment intensity scales the indirect term, so the Environment panel's
    // brightness control dims/brightens the scene's ambient, not just the sky.
    ambient *= u_iblIntensity;

    vec3 color = ambient + Lo + s.emission;

    // Shading-split debug views (composite tonemaps these like the normal
    // path). Unset in the preview/baker paths, so they always shade normally.
    if (u_renderMode == MODE_GI_ONLY)     { FragColor = vec4(ambient, 1.0); return; }
    if (u_renderMode == MODE_DIRECT_ONLY) { FragColor = vec4(Lo, 1.0); return; }
    if (u_renderMode == MODE_CLUSTERS) {
        // Green -> yellow -> red heatmap over the cluster's light count.
        float t    = clamp(float(count) / float(MAX_LIGHTS_PER_CLUSTER), 0.0, 1.0) * 3.0;
        vec3  heat = mix(vec3(0.02, 0.10, 0.02), vec3(0.15, 0.85, 0.15), clamp(t, 0.0, 1.0));
        heat       = mix(heat, vec3(0.95, 0.85, 0.10), clamp(t - 1.0, 0.0, 1.0));
        heat       = mix(heat, vec3(0.95, 0.10, 0.10), clamp(t - 2.0, 0.0, 1.0));
        FragColor = vec4(heat, 1.0);
        return;
    }

    // Screen-space transmission refraction: sample the copied scene behind the
    // surface, offset along the refracted view ray (IOR bend), tinted by the
    // glass colour + Beer-Lambert volume absorption. Makes transmissive glass
    // show and bend the background instead of rendering opaque. (Specular in
    // `color` is attenuated by the blend - a simplification until a Fresnel split.)
    if (u_hasSceneColor == 1 && u_material.transmission > 0.0) {
        vec3 rdir = refract(-V, N, 1.0 / max(u_material.ior, 1.0));
        if (dot(rdir, rdir) > 0.0) {
            float thickness = (u_material.thicknessFactor > 0.0) ? u_material.thicknessFactor : 0.5;
            vec4 cs0 = u_camera.viewProjection * vec4(vWorldPos, 1.0);
            vec4 cs1 = u_camera.viewProjection * vec4(vWorldPos + rdir * thickness, 1.0);
            vec2 uv0 = cs0.xy / max(cs0.w, 1e-4) * 0.5 + 0.5;
            vec2 uv1 = cs1.xy / max(cs1.w, 1e-4) * 0.5 + 0.5;
            vec2 ruv = clamp(gl_FragCoord.xy / u_screenSize + (uv1 - uv0), vec2(0.0), vec2(1.0));

            vec3 transmitted = texture(u_sceneColor, ruv).rgb * s.albedo;
            // Beer-Lambert absorption through a declared volume (tinted glass).
            if (u_material.thicknessFactor > 0.0 && u_material.attenuationColor.a > 0.0) {
                float dist = u_material.thicknessFactor / max(abs(dot(N, rdir)), 0.1);
                transmitted *= pow(max(u_material.attenuationColor.rgb, vec3(1e-4)),
                                   vec3(dist / u_material.attenuationColor.a));
            }
            color = mix(color, transmitted, u_material.transmission * (1.0 - s.metallic));
        }
    }

    // Write linear HDR - the composite pass tonemaps + gamma-corrects.
    // Transparent materials carry their opacity; alpha-masked materials write
    // their sharpened cutout coverage (for alpha-to-coverage); opaque writes 1.
    float outAlpha = (u_material.type == MAT_TRANSPARENT) ? s.opacity : maskCoverage;
    FragColor = vec4(color, outAlpha);
}
