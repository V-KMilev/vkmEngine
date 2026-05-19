/**
 * PBR fragment shader - energy-conserving Cook-Torrance.
 *
 * Outputs LINEAR scene-referred radiance into the HDR target. There is no
 * exposure, tone mapping, or gamma here on purpose - the composite pass owns
 * the entire display transform (exposure -> AgX -> sRGB). Doing it per-object
 * would clamp light and double-correct on blending.
 *
 * Specular: GGX NDF + height-correlated Smith visibility + Schlick Fresnel,
 * isotropic or anisotropic. Diffuse: energy-conserving Lambert coupled via
 * (1 - F). F0 from material IOR, lerped to albedo by metalness. Optional
 * clearcoat (own lobe, analytic + IBL), subsurface wrap + back translucency,
 * thin transmission, and parallax-occlusion mapping. All advanced terms are
 * scalar-driven from the material UBO and are no-ops at their defaults.
 */
#version 420 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vTangent;
in vec3 vBitangent;

out vec4 FragColor;

const float PI = 3.14159265359;

// Texture presence flags - must match MaterialTextureFlags in gl_material.h.
const int TEX_ALBEDO                = 1 << 0;
const int TEX_NORMAL                = 1 << 1;
const int TEX_METALLIC_ROUGHNESS    = 1 << 2;
const int TEX_METALLIC              = 1 << 3;
const int TEX_ROUGHNESS             = 1 << 4;
const int TEX_AO                    = 1 << 5;
const int TEX_AO_METALLIC_ROUGHNESS = 1 << 6;
const int TEX_EMISSION              = 1 << 7;
const int TEX_HEIGHT                = 1 << 8;
const int TEX_CLEARCOAT             = 1 << 9;
const int TEX_TRANSMISSION          = 1 << 10;

const int LIGHT_DIRECTIONAL = 0;
const int LIGHT_POINT       = 1;
const int LIGHT_SPOT        = 2;

const int MAX_LIGHTS = 32;                 // Engine::Config::MaxLights
const int SHADOW_MAX_CASTERS_2D   = 6;     // Engine::Config::MaxShadowCasters2D
const int SHADOW_MAX_CASTERS_CUBE = 2;     // Engine::Config::MaxShadowCastersCube

// Must match kCubeNear in gl_shadow_pass.cpp - used to rebuild the projected
// depth the point-light cube faces wrote.
const float SHADOW_CUBE_NEAR = 0.1;

layout(std140, binding = 0) uniform MaterialBlock {
    vec4  albedo;
    vec3  emission;             float _mp0;
    float metallic;
    float roughness;
    float ior;
    float transmission;
    float alpha;
    float ao;
    float clearcoat;
    float clearcoatRoughness;
    float anisotropy;           float sheenColorR; float sheenColorG; float sheenColorB;
    vec3  anisotropyDirection;  float _mp4;
    float subsurface;           float _mp5; float _mp6; float _mp7;
    vec3  subsurfaceColor;      float _mp8;
    float heightScale;
    float normalScale;
    int   textureFlags;
    float sheenRoughness;
} u_material;

layout(std140, binding = 2) uniform CameraBlock {
    mat4 viewProjection;
    vec4 cameraPosition;  // xyz = position, w = exposure (used by composite)
    vec4 ambient;         // xyz = color,    w = intensity
} u_camera;

struct Light {
    vec4 position;   // xyz = world position, w = type
    vec4 color;      // xyz = rgb,            w = intensity
    vec4 direction;  // xyz = world dir,      w = radius
    vec4 spot;       // x = inner, y = outer, z = unused, w = shadowSlot
};

layout(std140, binding = 1) uniform LightsBlock {
    int   lightCount;
    int   _lp0;
    int   _lp1;
    int   _lp2;
    Light lights[MAX_LIGHTS];
} u_lights;

struct Shadow2DCaster {
    mat4 lightSpace;
    vec4 params;     // x = bias
};
struct ShadowCubeCaster {
    vec4 params;     // x = bias, y = range
};

layout(std140, binding = 3) uniform ShadowBlock {
    int count2D;
    int countCube;
    int csmBaseSlot;   // first 2D layer of cascade 0 (-1 = no CSM)
    int csmCount;      // active sun cascade count
    Shadow2DCaster   casters2D  [SHADOW_MAX_CASTERS_2D];
    ShadowCubeCaster castersCube[SHADOW_MAX_CASTERS_CUBE];
} u_shadow;

uniform sampler2DArrayShadow   u_shadowMap2D;
uniform samplerCubeArrayShadow u_shadowMapCube;

uniform sampler2D u_albedoTexture;
uniform sampler2D u_normalTexture;
uniform sampler2D u_metallicRoughnessTexture;
uniform sampler2D u_aoMetallicRoughnessTexture;
uniform sampler2D u_aoTexture;
uniform sampler2D u_emissionTexture;
uniform sampler2D u_heightTexture;
uniform sampler2D u_metallicTexture;
uniform sampler2D u_roughnessTexture;

// Image-based lighting (split-sum). u_hasIBL gates between IBL and the flat
// ambient fallback; the maps are bound by the forward pass when a bake exists.
uniform samplerCube u_irradianceMap;
uniform samplerCube u_prefilterMap;
uniform samplerCube u_envCube;       // raw env radiance - sharp mirror at low roughness
uniform sampler2D   u_brdfLUT;
uniform int   u_hasIBL;
uniform float u_iblIntensity;

// Screen-space AO (GTAO). u_ssao is HALF resolution, so it is sampled by
// normalized screen UV (fragcoord / full viewport) - independent of the AO
// texture's own size - and linear-filtered up by the hardware.
uniform sampler2D u_ssao;
uniform int       u_ssaoEnabled;
uniform vec2      u_screenSize;   // full viewport pixels

// Resolved opaque-only scene color, bound by the forward pass at the
// opaque->transparent boundary. Lets transmissive materials refract what is
// actually behind them; u_hasSceneColor gates it (0 = fall back to IBL).
uniform sampler2D u_sceneColor;
uniform int       u_hasSceneColor;

const float MAX_PREFILTER_LOD = 6.0;  // GLIBL::PREFILTER_MIPS - 1

bool hasTex(int flag) {
    return (u_material.textureFlags & flag) != 0;
}

// Parallax-occlusion mapping: ray-march the height field along the
// tangent-space view direction, then interpolate the crossing for a smooth
// silhouette. More layers at grazing angles.
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

vec3 getNormal(vec2 uv, mat3 tbn) {
    if (!hasTex(TEX_NORMAL)) {
        return normalize(vNormal);
    }
    vec3 n = texture(u_normalTexture, uv).rgb * 2.0 - 1.0;
    n.xy *= u_material.normalScale;
    return normalize(tbn * normalize(n));
}

// Geometric specular antialiasing - Karis (UE4) / Filament. Raises roughness
// proportionally to screen-space shading-normal variance, so high-frequency
// normal-map detail and sharp curvature do not alias into shimmering specular
// highlights. That micro-sparkle is what makes an otherwise-correct PBR
// surface read as "raw/CG" instead of the reference's "finished" look. No
// ghosting and ~free (two derivatives), unlike a temporal solution.
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

// GGX / Trowbridge-Reitz normal distribution (Karis stable form).
float distributionGGX(float NdotH, float a) {
    float a2 = a * a;
    float d  = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

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

float sample2DShadow(int slot, vec3 worldPos, float NdotL) {
    Shadow2DCaster c = u_shadow.casters2D[slot];

    vec4 lp = c.lightSpace * vec4(worldPos, 1.0);
    vec3 proj = lp.xyz / lp.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 1.0;

    float biasMax = c.params.x;
    float bias = max(biasMax * (1.0 - NdotL), biasMax * 0.2);
    float ref = proj.z - bias;

    vec2 texel = 1.0 / vec2(textureSize(u_shadowMap2D, 0).xy);
    float sum = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 off = vec2(float(x), float(y)) * texel;
            sum += texture(u_shadowMap2D, vec4(proj.xy + off, float(slot), ref));
        }
    }
    return sum / 9.0;
}

float samplePointShadow(int slot, vec3 worldPos, vec3 lightPos) {
    ShadowCubeCaster c = u_shadow.castersCube[slot];
    float bias  = c.params.x;
    float range = max(c.params.y, 0.001);

    vec3 toFrag = worldPos - lightPos;
    if (length(toFrag) > range) return 1.0;

    // Rebuild the projected depth the cube face wrote with a 90 deg
    // perspective(near = SHADOW_CUBE_NEAR, far = range).
    vec3 a = abs(toFrag);
    float zc = max(a.x, max(a.y, a.z));
    float n = SHADOW_CUBE_NEAR;
    float f = range;
    float ndc = (f + n) / (f - n) - (2.0 * f * n) / (zc * (f - n));
    float ref = (ndc * 0.5 + 0.5) - bias;

    return texture(u_shadowMapCube, vec4(toFrag, float(slot)), ref);
}

// Pick the tightest sun cascade that contains the fragment (cascades are
// ordered near -> far). Falls back to the largest cascade at the edges.
int selectCascade(vec3 worldPos) {
    for (int c = 0; c < u_shadow.csmCount; ++c) {
        int layer = u_shadow.csmBaseSlot + c;
        vec4 lp = u_shadow.casters2D[layer].lightSpace * vec4(worldPos, 1.0);
        vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
        if (p.x > 0.02 && p.x < 0.98 &&
            p.y > 0.02 && p.y < 0.98 &&
            p.z > 0.0  && p.z < 1.0)
            return layer;
    }
    return u_shadow.csmBaseSlot + u_shadow.csmCount - 1;
}

struct Surface {
    vec3  albedo;
    float metallic;
    float roughness;
    float ao;
    vec3  emission;
};

Surface sampleSurface(vec2 uv) {
    Surface s;

    s.albedo = u_material.albedo.rgb;
    if (hasTex(TEX_ALBEDO)) {
        s.albedo *= texture(u_albedoTexture, uv).rgb;
    }

    s.metallic  = u_material.metallic;
    s.roughness = u_material.roughness;
    s.ao        = u_material.ao;
    s.emission  = u_material.emission;

    if (hasTex(TEX_AO_METALLIC_ROUGHNESS)) {
        vec3 amr = texture(u_aoMetallicRoughnessTexture, uv).rgb;
        s.ao        *= amr.r;
        s.metallic   = amr.g;
        s.roughness  = amr.b;
    } else if (hasTex(TEX_METALLIC_ROUGHNESS)) {
        vec3 mr = texture(u_metallicRoughnessTexture, uv).rgb;
        s.metallic  = mr.b;
        s.roughness = mr.g;
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

// Smooth windowed inverse-square falloff (physically based, finite range).
float distanceAttenuation(float dist, float radius) {
    float invSqr = 1.0 / max(dist * dist, 1e-4);
    float window = clamp(1.0 - pow(dist / max(radius, 1e-3), 4.0), 0.0, 1.0);
    return invSqr * window * window;
}

vec3 evaluateLight(vec3 N, vec3 V, vec3 L, vec3 T, vec3 B, Surface s, vec3 f0, vec3 radiance) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    bool hasBack = u_material.subsurface > 0.001 || u_material.transmission > 0.001;
    if (NdotL <= 0.0 && !hasBack) return vec3(0.0);

    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float a = s.roughness * s.roughness;

    // Base specular: anisotropic when configured, isotropic otherwise.
    float D, Vis;
    if (u_material.anisotropy > 0.001) {
        vec3 aT = normalize(T * u_material.anisotropyDirection.x +
                            B * u_material.anisotropyDirection.y +
                            N * u_material.anisotropyDirection.z);
        vec3 aB = normalize(cross(N, aT));
        aT = normalize(cross(aB, N));
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

    // Diffuse with optional subsurface wrap (light bleeds past the terminator).
    vec3 kd = (vec3(1.0) - F) * (1.0 - s.metallic);
    vec3 diffuse = kd * s.albedo / PI;
    float diffNoL = NdotL;
    if (u_material.subsurface > 0.001) {
        float w = u_material.subsurface;
        diffNoL = clamp((dot(N, L) + w) / ((1.0 + w) * (1.0 + w)), 0.0, 1.0);
    }

    // Thin transmission: reduce front diffuse, add a back-lit term.
    vec3 transmitted = vec3(0.0);
    if (u_material.transmission > 0.001) {
        float kt = u_material.transmission * (1.0 - s.metallic);
        diffuse *= (1.0 - kt);
        transmitted = kt * s.albedo / PI * max(dot(-N, L), 0.0);
    }

    // Subsurface back translucency, tinted.
    vec3 sss = vec3(0.0);
    if (u_material.subsurface > 0.001) {
        sss = u_material.subsurfaceColor * s.albedo
            * clamp(dot(-N, L), 0.0, 1.0) * u_material.subsurface;
    }

    vec3 baseLit = diffuse * diffNoL + specular * NdotL;

    // Clearcoat: a second, always-dielectric GGX lobe that also attenuates
    // the base layer by its Fresnel.
    vec3 ccContrib = vec3(0.0);
    if (u_material.clearcoat > 0.001) {
        float ccRough = clamp(u_material.clearcoatRoughness, 0.045, 1.0);
        float cca = ccRough * ccRough;
        float ccD = distributionGGX(NdotH, cca);
        float ccV = visSmithCorrelated(NdotV, NdotL, cca);
        float ccF = fresnelSchlick(VdotH, vec3(0.04)).x * u_material.clearcoat;
        baseLit *= (1.0 - ccF);
        ccContrib = vec3(ccD * ccV * ccF) * NdotL;
    }

    // Sheen / cloth lobe (Charlie). Disabled when sheenColor is black.
    vec3 sheenColor = vec3(u_material.sheenColorR, u_material.sheenColorG, u_material.sheenColorB);
    vec3 sheen = vec3(0.0);
    if (max(sheenColor.r, max(sheenColor.g, sheenColor.b)) > 0.0) {
        float sheenD = distributionCharlie(NdotH, u_material.sheenRoughness);
        float sheenV = visAshikhmin(NdotV, NdotL);
        sheen = sheenColor * (sheenD * sheenV * NdotL);
    }

    return (baseLit + ccContrib + transmitted + sss + sheen) * radiance;
}

void main() {
    vec3 V = normalize(u_camera.cameraPosition.xyz - vWorldPos);

    vec3 T = normalize(vTangent);
    vec3 B = normalize(vBitangent);
    vec3 Ng = normalize(vNormal);
    mat3 TBN = mat3(T, B, Ng);

    vec2 uv = vUV;
    if (hasTex(TEX_HEIGHT) && u_material.heightScale > 0.0) {
        vec3 viewTS = normalize(transpose(TBN) * V);
        uv = parallax(uv, viewTS);
    }

    Surface s = sampleSurface(uv);
    vec3 N = getNormal(uv, TBN);

    // Tame specular shimmer from normal-map detail / curvature before the
    // roughness drives both the analytic lobes and the IBL LOD.
    s.roughness = specularAA(N, s.roughness);

    float f0Dielectric = pow((u_material.ior - 1.0) / (u_material.ior + 1.0), 2.0);
    vec3 f0 = mix(vec3(f0Dielectric), s.albedo, s.metallic);

    vec3 Lo = vec3(0.0);

    for (int i = 0; i < u_lights.lightCount && i < MAX_LIGHTS; ++i) {
        Light light = u_lights.lights[i];

        vec3  lightPos = light.position.xyz;
        vec3  lightDir = normalize(light.direction.xyz);
        vec3  lightCol = light.color.xyz;
        float intensity = light.color.w;
        float radius    = light.direction.w;
        int   type      = int(light.position.w);

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

        float NdotL = max(dot(N, L), 0.0);
        float visibility = 1.0;
        int shadowSlot = int(light.spot.w);
        if (shadowSlot >= 0 && NdotL > 0.0) {
            if (type == LIGHT_POINT) {
                visibility = samplePointShadow(shadowSlot, vWorldPos, lightPos);
            } else if (type == LIGHT_DIRECTIONAL && u_shadow.csmCount > 0) {
                visibility = sample2DShadow(selectCascade(vWorldPos), vWorldPos, NdotL);
            } else {
                visibility = sample2DShadow(shadowSlot, vWorldPos, NdotL);
            }
        }

        vec3 radiance = lightCol * intensity * atten * visibility;
        Lo += evaluateLight(N, V, L, T, B, s, f0, radiance);
    }

    // Screen-space AO factor, sampled once. It and the material AO map
    // attenuate only the indirect DIFFUSE term; specular reflections get a
    // separate occlusion (below) so metals do not read as dull plastic.
    float ssaoFactor = 1.0;
    if (u_ssaoEnabled == 1) {
        ssaoFactor = texture(u_ssao, gl_FragCoord.xy / u_screenSize).r;
    }

    vec3 ambient;
    if (u_hasIBL == 1) {
        // Split-sum IBL with Fdez-Aguera multiscatter energy conservation
        // (Khronos glTF sample-viewer formulation).
        float NdotV = max(dot(N, V), 1e-4);
        vec3  R = reflect(-V, N);

        // Roughness-aware Fresnel (Sebastien Lagarde).
        vec3 F = f0 + (max(vec3(1.0 - s.roughness), f0) - f0) * pow(1.0 - NdotV, 5.0);

        vec3 irradiance  = texture(u_irradianceMap, N).rgb;
        vec3 prefiltered = textureLod(u_prefilterMap, R, s.roughness * MAX_PREFILTER_LOD).rgb;

        // Polished metal: the prefilter (even at 512) is GGX-convolved at
        // mip 0, so a perfect mirror still reads slightly soft. Blend in the
        // raw environment cube at very low roughness for a true reflection.
        // smoothstep keeps the transition seamless into the prefiltered set.
        if (s.roughness < 0.2) {
            vec3 sharp = textureLod(u_envCube, R, 0.0).rgb;
            prefiltered = mix(sharp, prefiltered, smoothstep(0.0, 0.2, s.roughness));
        }

        vec2 dfg         = texture(u_brdfLUT, vec2(NdotV, s.roughness)).rg;

        vec3  FssEss = F * dfg.x + dfg.y;
        float Ess    = dfg.x + dfg.y;
        float Ems    = 1.0 - Ess;
        vec3  Favg   = f0 + (1.0 - f0) / 21.0;
        vec3  Fms    = FssEss * Favg / (1.0 - Ems * Favg);
        vec3  kD     = (1.0 - FssEss - Fms * Ems) * (1.0 - s.metallic);

        // Diffuse indirect takes the full AO (map * SSAO). Specular indirect
        // uses Lagarde/Frostbite specular occlusion: it preserves reflections
        // on smooth surfaces and only occludes rough cavities, instead of the
        // old `* s.ao` that flatly dimmed every metal.
        float diffuseAO = s.ao * ssaoFactor;
        float specOcc   = clamp(pow(NdotV + diffuseAO,
                                    exp2(-16.0 * s.roughness - 1.0))
                                - 1.0 + diffuseAO, 0.0, 1.0);

        vec3 specularIBL = FssEss * prefiltered * specOcc;
        vec3 diffuseIBL  = (Fms * Ems + kD) * irradiance * s.albedo * diffuseAO;

        ambient = (diffuseIBL + specularIBL) * u_iblIntensity;

        // Clearcoat IBL: a dielectric specular lobe over the base ambient.
        if (u_material.clearcoat > 0.001) {
            float ccRough = clamp(u_material.clearcoatRoughness, 0.045, 1.0);
            float ccFr = (0.04 + 0.96 * pow(1.0 - NdotV, 5.0)) * u_material.clearcoat;
            vec3  ccPref = textureLod(u_prefilterMap, R, ccRough * MAX_PREFILTER_LOD).rgb;
            vec2  ccDfg  = texture(u_brdfLUT, vec2(NdotV, ccRough)).rg;
            vec3  ccSpecIBL = ccPref * (0.04 * ccDfg.x + ccDfg.y);
            ambient = ambient * (1.0 - ccFr) + ccSpecIBL * ccFr * s.ao * u_iblIntensity;
        }
    } else {
        // Flat ambient fallback when no environment map is set. This term is
        // purely diffuse, so the full AO (map * SSAO) applies directly.
        ambient = u_camera.ambient.xyz * u_camera.ambient.w * s.albedo
                * s.ao * ssaoFactor;
    }

    vec3 color = ambient + Lo + s.emission;

    // Refraction for transmissive materials. When the forward pass has
    // snapshotted the opaque scene (u_hasSceneColor), glass shows the actual
    // geometry behind it, screen-space-offset along the bent ray; otherwise
    // it falls back to refracted IBL (first frame / no opaque behind).
    if (u_material.transmission > 0.001) {
        vec3 rdir = refract(-V, N, 1.0 / max(u_material.ior, 1.0));
        vec3 refr;
        if (u_hasSceneColor == 1 && dot(rdir, rdir) > 0.0) {
            // Project a short refracted ray into screen space and offset by
            // that delta. Using world-space xy directly (the old code)
            // smeared with the view angle - this is camera-correct.
            float thick = 0.20 + 0.40 * clamp(u_material.transmission, 0.0, 1.0);
            vec4 cs0 = u_camera.viewProjection * vec4(vWorldPos, 1.0);
            vec4 cs1 = u_camera.viewProjection * vec4(vWorldPos + rdir * thick, 1.0);
            vec2 p0  = cs0.xy / max(cs0.w, 1e-4) * 0.5 + 0.5;
            vec2 p1  = cs1.xy / max(cs1.w, 1e-4) * 0.5 + 0.5;
            vec2 uv  = gl_FragCoord.xy / u_screenSize
                     + (p1 - p0) * (1.0 + 2.0 * s.roughness);
            uv = clamp(uv, vec2(0.0), vec2(1.0));
            refr = texture(u_sceneColor, uv).rgb * s.albedo;
        } else if (u_hasIBL == 1 && dot(rdir, rdir) > 0.0) {
            refr = textureLod(u_prefilterMap, rdir,
                        s.roughness * MAX_PREFILTER_LOD).rgb * s.albedo;
        } else {
            refr = color;
        }
        color = mix(color, refr, clamp(u_material.transmission, 0.0, 1.0));
    }

    FragColor = vec4(color, u_material.albedo.a * u_material.alpha);
}
