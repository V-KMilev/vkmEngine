#version 420 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec3 Tangent;
in vec3 Bitangent;

out vec4 FragColor;

uniform vec3 u_cameraPosition;

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

const int MAX_LIGHTS = 16;

// Light data (binding = 1, must match C++ LightGPUData exactly)
struct Light {
    vec3  position;         // offset 0
    int   type;             // offset 12
    vec3  color;            // offset 16
    float intensity;        // offset 28
    vec3  direction;        // offset 32
    float radius;           // offset 44
    float innerConeAngle;   // offset 48
    float outerConeAngle;   // offset 52
    float castShadows;      // offset 56
    float _padding;         // offset 60 - EXPLICIT padding to 64 bytes
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

vec3 getNormalFromMap() {
    if (!hasTexture(u_material.textureFlags, TEXTURE_FLAG_NORMAL)) {
        return normalize(Normal);
    }

    vec3 tangentNormal = texture(u_normalTexture, TexCoords).rgb * 2.0 - 1.0;
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

// Calculate distance attenuation for point and spot lights
float calculateAttenuation(vec3 lightPos, vec3 fragPos, float radius) {
    float distance = length(lightPos - fragPos);
    float attenuation = clamp(1.0 - (distance / radius), 0.0, 1.0);
    return attenuation * attenuation; // Quadratic falloff
}

// Calculate cone attenuation for spot lights
float calculateSpotAttenuation(vec3 lightDir, vec3 lightToFrag, float innerAngle, float outerAngle) {
    float theta   = dot(lightDir, normalize(-lightToFrag));
    float epsilon = cos(innerAngle) - cos(outerAngle);
    float intensity = clamp((theta - cos(outerAngle)) / epsilon, 0.0, 1.0);
    return intensity;
}

// Calculate BRDF (Bidirectional Reflectance Distribution Function)
vec3 calculateBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 H = normalize(V + L);
    // Cook-Torrance BRDF components
    float NDF = distributionGGX(N, H, roughness);
    float G   = geometrySmith(N, V, L, roughness);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);
    // Calculate specular
    vec3  numerator   = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3  specular    = numerator / denominator;
    // Energy conservation
    vec3 kS = F;                    // Specular reflection
    vec3 kD = vec3(1.0) - kS;       // Remaining energy for diffuse
    kD *= 1.0 - metallic;           // Metals have no diffuse
    // Lambertian diffuse
    vec3 diffuse = kD * albedo / PI;

    return diffuse + specular;
}

struct MaterialProperties {
    vec4  albedo;
    float metallic;
    float roughness;
    float ao;
    vec3  emission;
};

MaterialProperties sampleMaterial() {
    MaterialProperties props;
    // Sample albedo
    props.albedo = u_material.albedo;
    if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_ALBEDO)) {
        props.albedo *= texture(u_albedoTexture, TexCoords);
    }
    // Initialize from material uniforms
    props.metallic   = u_material.metallic;
    props.emission   = u_material.emission;
    props.roughness  = u_material.roughness;
    float ior        = u_material.ior;
    float transmission = u_material.transmission;
    float alpha      = u_material.alpha;
    props.ao         = u_material.ao;
    float clearcoat  = u_material.clearcoat;
    float clearcoatRoughness = u_material.clearcoatRoughness;
    float anisotropy = u_material.anisotropy;

    // Sample packed textures (priority order)
    if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_AO_METALLIC_ROUGHNESS)) {
        // Combined AO + Metallic + Roughness texture
        vec3 aoMR = texture(u_aoMetallicRoughnessTexture, TexCoords).rgb;
        props.ao        *= aoMR.r;
        props.metallic   = aoMR.g;
        props.roughness  = aoMR.b;
    }
    else if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_METALLIC_ROUGHNESS)) {
        // Combined Metallic + Roughness texture
        vec3 mr = texture(u_metallicRoughnessTexture, TexCoords).rgb;
        props.metallic  = mr.b;
        props.roughness = mr.g;
    } 
    else {
        // Separate textures
        if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_METALLIC)) {
            props.metallic *= texture(u_metallicTexture, TexCoords).r;
        }
        if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_ROUGHNESS)) {
            props.roughness *= texture(u_roughnessTexture, TexCoords).r;
        }
    }
    // Sample AO separately if needed
    if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_AO) && 
        !hasTexture(u_material.textureFlags, TEXTURE_FLAG_AO_METALLIC_ROUGHNESS)) {
        props.ao *= texture(u_aoTexture, TexCoords).r;
    }
    // Sample emission
    props.emission = u_material.emission;
    if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_EMISSION)) {
        props.emission *= texture(u_emissionTexture, TexCoords).rgb;
    }

    return props;
}

void main() {
    // Sample material properties
    MaterialProperties material = sampleMaterial();
    // Get surface normal (with normal mapping)
    vec3 N = getNormalFromMap();
    vec3 V = normalize(u_cameraPosition - FragPos);
    // Calculate base reflectance (F0)
    vec3 F0 = vec3(0.04);  // Default for dielectrics
    F0 = mix(F0, material.albedo.rgb, material.metallic);

    vec3 Lo = vec3(0.0);

    if (u_lights.lightCount == 0) {
        // Fallback: Default directional light
        vec3 L = normalize(vec3(-1.0, -1.0, 1.0));
        vec3 radiance = vec3(1.0) * 3.0;
        float NdotL = max(dot(N, L), 0.0);

        Lo = calculateBRDF(N, V, L, material.albedo.rgb, material.metallic, material.roughness, F0) 
             * radiance * NdotL;
    }
    else {
        // Loop through all scene lights
        for (int i = 0; i < u_lights.lightCount; i++) {
            Light light = u_lights.lights[i];

            vec3  L = vec3(0.0);
            float attenuation = 1.0;

            // Calculate light direction and attenuation based on type
            if (light.type == LIGHT_TYPE_DIRECTIONAL) {
                // direction stored toward scene; from surface to light is -dir
                L = normalize(-light.direction);
            }
            else if (light.type == LIGHT_TYPE_POINT) {
                L = normalize(light.position - FragPos);
                attenuation = calculateAttenuation(light.position, FragPos, light.radius);
            }
            else if (light.type == LIGHT_TYPE_SPOT) {
                L = normalize(light.position - FragPos);
                attenuation = calculateAttenuation(light.position, FragPos, light.radius);
                attenuation *= calculateSpotAttenuation(light.direction, light.position - FragPos, 
                                                       light.innerConeAngle, light.outerConeAngle);
            }
            // Skip lights that don't contribute
            if (attenuation < 0.001) continue;
            // Calculate radiance
            float NdotL = max(dot(N, L), 0.0);
            vec3 radiance = light.color * light.intensity * attenuation;
            
            // Accumulate light contribution
            Lo += calculateBRDF(N, V, L, material.albedo.rgb, material.metallic, material.roughness, F0) 
                  * radiance * NdotL;
        }
    }

    // Ambient lighting with ambient occlusion
    vec3 ambient = vec3(0.03) * material.albedo.rgb * material.ao;

    // Combine all lighting
    vec3 color = ambient + Lo + material.emission;

    // Output with alpha
    float finalAlpha = material.albedo.a * u_material.alpha;
    FragColor = vec4(color, finalAlpha);
}
