#version 420 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec3 Tangent;
in vec3 Bitangent;

out vec4 FragColor;

layout(std140, binding = 2) uniform CameraBlock {
    mat4 viewProjection;
    vec4 cameraPosition;  // xyz = position, w = exposure
    vec4 ambient;         // xyz = color, w = intensity
} u_camera;

const float PI = 3.14159265359;

// Texture flags (must match MaterialTextureFlags in C++)
const int TEXTURE_FLAG_ALBEDO                = 1 << 0;
const int TEXTURE_FLAG_NORMAL                = 1 << 1;
const int TEXTURE_FLAG_METALLIC_ROUGHNESS    = 1 << 2;
const int TEXTURE_FLAG_METALLIC              = 1 << 3;
const int TEXTURE_FLAG_ROUGHNESS             = 1 << 4;
const int TEXTURE_FLAG_AO                    = 1 << 5;
const int TEXTURE_FLAG_AO_METALLIC_ROUGHNESS = 1 << 6;
const int TEXTURE_FLAG_EMISSION              = 1 << 7;
const int TEXTURE_FLAG_HEIGHT                = 1 << 8;
const int TEXTURE_FLAG_CLEARCOAT             = 1 << 9;
const int TEXTURE_FLAG_TRANSMISSION          = 1 << 10;

// Light types
const int LIGHT_TYPE_DIRECTIONAL = 0;
const int LIGHT_TYPE_POINT       = 1;
const int LIGHT_TYPE_SPOT        = 2;

const int MAX_LIGHTS = 32;

// Light data (binding = 1, must match C++ LightGPUData exactly)
// Light slots are vec4 to sidestep drivers that don't pack vec3+scalar in std140.
//   position.xyz = world position,  position.w = type (cast to int)
//   color.xyz    = RGB,             color.w    = intensity
//   direction.xyz= world direction, direction.w= radius
//   spot.x       = inner cone,      spot.y     = outer cone, spot.z = castShadows
struct Light {
    vec4 position;
    vec4 color;
    vec4 direction;
    vec4 spot;
};

// Material properties (binding = 0, must match C++ MaterialUBOData exactly with std140 padding)
layout(std140, binding = 0) uniform MaterialBlock {
    vec4 albedo;                          // offset 0
    vec3 emission;                        // offset 16
    float pad0;                           // offset 28

    float metallic;                       // offset 32
    float roughness;                      // offset 36
    float ior;                            // offset 40
    float transmission;                   // offset 44

    float alpha;                          // offset 48
    float ao;                             // offset 52
    float clearcoat;                      // offset 56
    float clearcoatRoughness;             // offset 60

    float anisotropy;                     // offset 64
    float pad1_0;                         // offset 68
    float pad1_1;                         // offset 72
    float pad1_2;                         // offset 76

    vec3 anisotropyDirection;             // offset 80
    float pad2;                           // offset 92

    float subsurface;                     // offset 96
    float pad3_0;                         // offset 100
    float pad3_1;                         // offset 104
    float pad3_2;                         // offset 108

    vec3 subsurfaceColor;                 // offset 112
    float pad4;                           // offset 124

    float heightScale;                    // offset 128
    float normalScale;                    // offset 132
    int   textureFlags;                   // offset 136
    float pad5;                           // offset 140
} u_material;

layout(std140, binding = 1) uniform LightsBlock {
    int   lightCount;           // offset 0, 4 bytes
    float lightspad1;
    float lightspad2;
    float lightspad3;
    Light lights[MAX_LIGHTS];   // offset 16, 64 bytes per light
} u_lights;

// Shadow caster array (binding = 3, must match C++ ShadowUBOData).
//
// Each caster has:
//   lightSpace : world -> light clip space  (2D-array casters only)
//   params.x   = lightIndex into LightsBlock
//   params.y   = mapLayer:
//                  >= 0 -> sampler2DArrayShadow layer (directional / spot)
//                  <  0 -> samplerCubeArrayShadow cube index, encoded as -(idx + 1)
//   params.z   = bias
//   params.w   = light range (point lights, used to normalise sample depth)
const int SHADOW_MAX_CASTERS = 8;
struct ShadowCaster {
    mat4 lightSpace;
    vec4 params;
};
layout(std140, binding = 3) uniform ShadowBlock {
    int          casterCount;
    int          _pad0;
    int          _pad1;
    int          _pad2;
    ShadowCaster casters[SHADOW_MAX_CASTERS];
} u_shadow;

uniform sampler2DArrayShadow   u_shadowMap2D;
uniform samplerCubeArrayShadow u_shadowMapCube;

// Texture samplers
uniform sampler2D u_albedoTexture;
uniform sampler2D u_normalTexture;
uniform sampler2D u_metallicRoughnessTexture;
uniform sampler2D u_metallicTexture;
uniform sampler2D u_roughnessTexture;
uniform sampler2D u_aoTexture;
uniform sampler2D u_aoMetallicRoughnessTexture;
uniform sampler2D u_emissionTexture;
uniform sampler2D u_heightTexture;
uniform sampler2D u_clearcoatTexture;
uniform sampler2D u_transmissionTexture;

bool hasTexture(int flags, int flag) {
    return (flags & flag) != 0;
}

// Parallax offset mapping
vec2 parallaxMapping(vec2 uv, vec3 viewDirTS) {
    float height = texture(u_heightTexture, uv).r;
    vec2 offset = viewDirTS.xy / max(viewDirTS.z, 0.001) * (height * u_material.heightScale);
    return uv - offset;
}

// Normal mapping with adjustable intensity
vec3 getNormalFromMap(vec2 uv) {
    if (!hasTexture(u_material.textureFlags, TEXTURE_FLAG_NORMAL)) {
        return normalize(Normal);
    }

    vec3 tangentNormal = texture(u_normalTexture, uv).rgb * 2.0 - 1.0;
    tangentNormal.xy *= u_material.normalScale;
    tangentNormal = normalize(tangentNormal);
    mat3 TBN = mat3(normalize(Tangent), normalize(Bitangent), normalize(Normal));
    return normalize(TBN * tangentNormal);
}

// Normal Distribution Function (GGX/Trowbridge-Reitz)
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float numerator   = a2;
    float denominator = (NdotH2 * (a2 - 1.0) + 1.0);
    denominator = PI * denominator * denominator;

    return numerator / denominator;
}

// Anisotropic NDF (GGX with directional roughness)
float distributionGGXAnisotropic(vec3 H, vec3 T, vec3 B, vec3 N, float at, float ab) {
    float TdotH = dot(T, H);
    float BdotH = dot(B, H);
    float NdotH = max(dot(N, H), 0.0);

    float a2 = at * ab;
    float d = (TdotH * TdotH) / (at * at)
            + (BdotH * BdotH) / (ab * ab)
            + NdotH * NdotH;

    return 1.0 / (PI * a2 * d * d + 0.0001);
}

// Geometry Function (Schlick-GGX)
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float numerator   = NdotV;
    float denominator = NdotV * (1.0 - k) + k;

    return numerator / denominator;
}

// Geometry Function (Smith's method)
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1  = geometrySchlickGGX(NdotV, roughness);
    float ggx2  = geometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ACES filmic tone mapping (Narkowicz fit)
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Linear to sRGB gamma correction
vec3 linearToSRGB(vec3 color) {
    return pow(color, vec3(1.0 / 2.2));
}

// Linear scan: find the caster entry for light index `lightIdx`, or -1 if none.
int findShadowCaster(int lightIdx) {
    for (int j = 0; j < u_shadow.casterCount; ++j) {
        if (int(u_shadow.casters[j].params.x + 0.5) == lightIdx) return j;
    }
    return -1;
}

// Hardware PCF (2x2) + 3x3 kernel on a directional/spot map. Returns 1.0
// when the fragment lies beyond the shadow camera's far plane.
float sample2DShadow(int casterIdx, vec3 worldPos, vec3 N, vec3 L) {
    ShadowCaster caster = u_shadow.casters[casterIdx];
    float layer = caster.params.y;
    float biasMax = caster.params.z;

    vec4 lp = caster.lightSpace * vec4(worldPos, 1.0);
    vec3 proj = lp.xyz / lp.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 1.0;

    float bias = max(biasMax * (1.0 - dot(N, L)), biasMax * 0.1);
    proj.z -= bias;

    vec2 texel = 1.0 / vec2(textureSize(u_shadowMap2D, 0).xy);
    float sum = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(float(x), float(y)) * texel;
            sum += texture(u_shadowMap2D, vec4(proj.xy + offset, layer, proj.z));
        }
    }
    return sum / 9.0;
}

// Cube-map shadow sample for point lights. Depth was written as a normalised
// linear distance (|frag - light| / range), so the reference is the same.
float samplePointShadow(int casterIdx, vec3 worldPos, vec3 lightPos) {
    ShadowCaster caster = u_shadow.casters[casterIdx];
    // params.y encodes cube index as -(idx + 1) to distinguish from 2D layers.
    int cubeIndex = int(-caster.params.y - 0.5);
    float bias    = caster.params.z;
    float range   = max(caster.params.w, 0.001);

    vec3 toFrag = worldPos - lightPos;
    float currentDepth = length(toFrag) / range;
    if (currentDepth > 1.0) return 1.0;

    vec3 dir = normalize(toFrag);
    return texture(u_shadowMapCube, vec4(dir, float(cubeIndex)), currentDepth - bias);
}

// Pick the correct sampler based on the caster's map type encoding.
float sampleShadowFor(int casterIdx, vec3 worldPos, vec3 lightPos, vec3 N, vec3 L) {
    ShadowCaster caster = u_shadow.casters[casterIdx];
    if (caster.params.y < 0.0) {
        return samplePointShadow(casterIdx, worldPos, lightPos);
    }
    return sample2DShadow(casterIdx, worldPos, N, L);
}

// Calculate distance attenuation for point and spot lights
float calculateAttenuation(vec3 lightPos, vec3 fragPos, float radius) {
    float distance = length(lightPos - fragPos);
    float attenuation = clamp(1.0 - (distance / radius), 0.0, 1.0);
    return attenuation * attenuation; // Quadratic falloff
}

// Calculate cone attenuation for spot lights
float calculateSpotAttenuation(vec3 lightDir, vec3 lightToFrag, float innerAngle, float outerAngle) {
    float theta   = dot(lightDir, normalize(-lightToFrag));
    float epsilon = max(cos(innerAngle) - cos(outerAngle), 0.0001);
    float intensity = clamp((theta - cos(outerAngle)) / epsilon, 0.0, 1.0);
    return intensity;
}

struct MaterialProperties {
    vec4  albedo;
    float metallic;
    float roughness;
    float ao;
    vec3  emission;
    float ior;
    float transmission;
    float clearcoat;
    float clearcoatRoughness;
    float anisotropy;
    float subsurface;
    vec3  subsurfaceColor;
};

MaterialProperties sampleMaterial(vec2 uv) {
    MaterialProperties props;
    // Sample albedo
    props.albedo = u_material.albedo;
    if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_ALBEDO)) {
        props.albedo *= texture(u_albedoTexture, uv);
    }
    // Initialize from material uniforms
    props.metallic           = u_material.metallic;
    props.emission           = u_material.emission;
    props.roughness          = u_material.roughness;
    props.ao                 = u_material.ao;
    props.ior                = u_material.ior;
    props.transmission       = u_material.transmission;
    props.clearcoat          = u_material.clearcoat;
    props.clearcoatRoughness = u_material.clearcoatRoughness;
    props.anisotropy         = u_material.anisotropy;
    props.subsurface         = u_material.subsurface;
    props.subsurfaceColor    = u_material.subsurfaceColor;

    // Sample packed textures (priority order)
    if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_AO_METALLIC_ROUGHNESS)) {
        vec3 aoMR = texture(u_aoMetallicRoughnessTexture, uv).rgb;
        props.ao        *= aoMR.r;
        props.metallic   = aoMR.g;
        props.roughness  = aoMR.b;
    }
    else if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_METALLIC_ROUGHNESS)) {
        vec3 mr = texture(u_metallicRoughnessTexture, uv).rgb;
        props.metallic  = mr.b;
        props.roughness = mr.g;
    }
    else {
        if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_METALLIC)) {
            props.metallic *= texture(u_metallicTexture, uv).r;
        }
        if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_ROUGHNESS)) {
            props.roughness *= texture(u_roughnessTexture, uv).r;
        }
    }
    // Clamp roughness to prevent NDF singularity
    props.roughness = clamp(props.roughness, 0.04, 1.0);

    // Sample AO separately if needed
    if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_AO) &&
        !hasTexture(u_material.textureFlags, TEXTURE_FLAG_AO_METALLIC_ROUGHNESS)) {
        props.ao *= texture(u_aoTexture, uv).r;
    }
    // Sample emission
    if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_EMISSION)) {
        props.emission *= texture(u_emissionTexture, uv).rgb;
    }
    // Sample clearcoat
    if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_CLEARCOAT)) {
        props.clearcoat *= texture(u_clearcoatTexture, uv).r;
    }
    // Sample transmission
    if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_TRANSMISSION)) {
        props.transmission *= texture(u_transmissionTexture, uv).r;
    }

    return props;
}

// Full per-light contribution including all material features
vec3 evaluateLight(
    vec3 N, vec3 V, vec3 L, vec3 T, vec3 B,
    MaterialProperties mat, vec3 F0, vec3 radiance
) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float HdotV = max(dot(H, V), 0.0);

    // Normal Distribution Function
    float NDF;
    if (mat.anisotropy > 0.001) {
        // Anisotropic GGX: stretch roughness along tangent/bitangent
        float at = max(mat.roughness * (1.0 + mat.anisotropy), 0.04);
        float ab = max(mat.roughness * (1.0 - mat.anisotropy), 0.04);
        NDF = distributionGGXAnisotropic(H, T, B, N, at, ab);
    } else {
        NDF = distributionGGX(N, H, mat.roughness);
    }

    // Geometry & Fresnel
    float G  = geometrySmith(N, V, L, mat.roughness);
    vec3  F  = fresnelSchlick(HdotV, F0);

    // Specular (Cook-Torrance)
    vec3  specular = (NDF * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    // Diffuse with energy conservation
    vec3 kD = (vec3(1.0) - F) * (1.0 - mat.metallic);

    // Subsurface scattering: wrap lighting approximation
    float diffuseNdotL = NdotL;
    vec3  diffuseAlbedo = mat.albedo.rgb;
    if (mat.subsurface > 0.001) {
        // Wrap the N.L term so light bleeds to the back side
        diffuseNdotL = max((dot(N, L) + mat.subsurface) / (1.0 + mat.subsurface), 0.0);
        // Tint with subsurface color
        diffuseAlbedo = mix(diffuseAlbedo, mat.subsurfaceColor * diffuseAlbedo, mat.subsurface);
    }

    vec3 diffuse = kD * diffuseAlbedo / PI;

    // ---- Transmission (back-face illumination) ----
    vec3 transmitted = vec3(0.0);
    if (mat.transmission > 0.001) {
        float backNdotL = max(dot(-N, L), 0.0);
        transmitted = kD * mat.albedo.rgb * mat.transmission * backNdotL / PI;
        // Reduce front diffuse proportionally
        diffuse *= (1.0 - mat.transmission);
    }

    // ---- Clearcoat (second specular lobe) ----
    vec3 ccContrib = vec3(0.0);
    if (mat.clearcoat > 0.001) {
        float ccRoughness = clamp(mat.clearcoatRoughness, 0.04, 1.0);
        float ccNDF = distributionGGX(N, H, ccRoughness);
        float ccG   = geometrySmith(N, V, L, ccRoughness);
        // Clearcoat is always dielectric (F0 = 0.04)
        vec3  ccF   = fresnelSchlick(HdotV, vec3(0.04));
        vec3  ccSpec = (ccNDF * ccG * ccF) / (4.0 * NdotV * NdotL + 0.0001);
        ccContrib = ccSpec * mat.clearcoat * NdotL * radiance;

        // Clearcoat absorbs some energy from the base layer
        float ccFresnel = ccF.r;
        diffuse  *= (1.0 - mat.clearcoat * ccFresnel);
        specular *= (1.0 - mat.clearcoat * ccFresnel);
    }

    // Combine
    vec3 result = (diffuse * diffuseNdotL + specular * NdotL + transmitted) * radiance;
    result += ccContrib;

    return result;
}

void main() {
    vec3 V = normalize(u_camera.cameraPosition.xyz - FragPos);
    vec3 T = normalize(Tangent);
    vec3 B = normalize(Bitangent);
    vec3 Ngeom = normalize(Normal);

    // Parallax mapping (shifts UVs based on height map)
    vec2 uv = TexCoords;
    if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_HEIGHT) && u_material.heightScale > 0.0) {
        mat3 TBN = mat3(T, B, Ngeom);
        vec3 viewDirTS = normalize(transpose(TBN) * V);
        uv = parallaxMapping(uv, viewDirTS);
    }

    // Sample material with (potentially displaced) UVs
    MaterialProperties material = sampleMaterial(uv);

    // Normal from map (with normalScale intensity)
    vec3 N = getNormalFromMap(uv);

    // F0 from IOR (physically correct instead of hardcoded 0.04)
    float f0Dielectric = pow((material.ior - 1.0) / (material.ior + 1.0), 2.0);
    vec3 F0 = vec3(f0Dielectric);
    F0 = mix(F0, material.albedo.rgb, material.metallic);

    // Lighting
    vec3 Lo = vec3(0.0);

    if (u_lights.lightCount == 0) {
        // Fallback: default directional light
        vec3 L = normalize(vec3(-1.0, -1.0, 1.0));
        vec3 radiance = vec3(3.0);
        Lo = evaluateLight(N, V, L, T, B, material, F0, radiance);
    }
    else {
        for (int i = 0; i < u_lights.lightCount; i++) {
            Light light = u_lights.lights[i];

            vec3  lightPos = light.position.xyz;
            vec3  lightDir = light.direction.xyz;
            vec3  lightCol = light.color.xyz;
            float intensity = light.color.w;
            float radius    = light.direction.w;
            int   type      = int(light.position.w);

            vec3  L = vec3(0.0);
            float attenuation = 1.0;

            if (type == LIGHT_TYPE_DIRECTIONAL) {
                L = normalize(-lightDir);
            }
            else if (type == LIGHT_TYPE_POINT) {
                L = normalize(lightPos - FragPos);
                attenuation = calculateAttenuation(lightPos, FragPos, radius);
            }
            else if (type == LIGHT_TYPE_SPOT) {
                L = normalize(lightPos - FragPos);
                attenuation = calculateAttenuation(lightPos, FragPos, radius);
                attenuation *= calculateSpotAttenuation(lightDir, lightPos - FragPos,
                                                        light.spot.x, light.spot.y);
            }
            if (attenuation < 0.001) continue;

            // Any light that has a caster entry (regardless of type) gets shadowed.
            float visibility = 1.0;
            int casterIdx = findShadowCaster(i);
            if (casterIdx >= 0) {
                visibility = sampleShadowFor(casterIdx, FragPos, lightPos, N, L);
            }

            vec3 radiance = lightCol * intensity * attenuation * visibility;
            Lo += evaluateLight(N, V, L, T, B, material, F0, radiance);
        }
    }

    // Ambient with AO
    vec3 ambient = u_camera.ambient.xyz * u_camera.ambient.w * material.albedo.rgb * material.ao;

    // Combine
    vec3 color = ambient + Lo + material.emission;

    // Exposure
    color *= u_camera.cameraPosition.w;

    // Tone mapping & gamma
    color = ACESFilm(color);
    color = linearToSRGB(color);

    // Output
    float finalAlpha = material.albedo.a * u_material.alpha;
    FragColor = vec4(color, finalAlpha);
}
