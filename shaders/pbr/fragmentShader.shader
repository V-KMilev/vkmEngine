#version 420 core

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec3 Tangent;
in vec3 Bitangent;

out vec4 FragColor;

uniform vec3 u_cameraPosition;

// Material uniform block (std140 layout)
// Note: Samplers cannot be in uniform blocks, so they remain as separate uniforms
layout(std140, binding = 0) uniform MaterialBlock {
    vec4 albedo;
    vec3 emission;
    float metallic;
    float roughness;

    float ior;
    float transmission;
    float alpha;
    float ao;

    float clearcoat;
    float clearcoatRoughness;
    float anisotropy;
    vec3 anisotropyDirection;

    float subsurface;
    vec3 subsurfaceColor;

    float heightScale;

    // Texture presence flags
    int hasAlbedoTexture;
    int hasNormalTexture;
    int hasMetallicRoughnessTexture;
    int hasMetallicTexture;
    int hasRoughnessTexture;
    int hasAOTexture;
    int hasAOMetallicRoughnessTexture;
    int hasEmissionTexture;
    int hasHeightTexture;
    int hasClearcoatTexture;
    int hasTransmissionTexture;

    // Texture sampler slots (indices into texture units)
    int albedoTextureSlot;
    int normalTextureSlot;
    int metallicRoughnessTextureSlot;
    int metallicTextureSlot;
    int roughnessTextureSlot;
    int aoTextureSlot;
    int aoMetallicRoughnessTextureSlot;
    int emissionTextureSlot;
    int heightTextureSlot;
    int clearcoatTextureSlot;
    int transmissionTextureSlot;
} u_material;

// Texture samplers (must be separate uniforms, cannot be in UBO)
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

// Simple directional light for now (can be extended)
struct Light {
    vec3 direction;
    vec3 color;
    float intensity;
};

uniform Light u_light;

// Constants
const float PI = 3.14159265359;

// Normal Distribution Function (GGX/Trowbridge-Reitz)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

// Geometry Function (Schlick-GGX)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

// Geometry Function (Smith)
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Sample normal from normal map
vec3 getNormalFromMap() {
    if (u_material.hasNormalTexture == 0) {
        return normalize(Normal);
    }

    vec3 tangentNormal = texture(u_normalTexture, TexCoords).rgb * 2.0 - 1.0;

    mat3 TBN = mat3(normalize(Tangent), normalize(Bitangent), normalize(Normal));
    return normalize(TBN * tangentNormal);
}

void main() {
    // Sample textures
    vec4 albedo = u_material.albedo;
    if (u_material.hasAlbedoTexture != 0) {
        albedo *= texture(u_albedoTexture, TexCoords);
    }

    float metallic = u_material.metallic;
    float roughness = u_material.roughness;
    float ao = u_material.ao;

    // Sample AO-Metallic-Roughness combined texture (highest priority)
    if (u_material.hasAOMetallicRoughnessTexture != 0) {
        vec3 aoMr = texture(u_aoMetallicRoughnessTexture, TexCoords).rgb;
        ao *= aoMr.r;      // Red channel = AO
        metallic = aoMr.g; // Green channel = metallic
        roughness = aoMr.b; // Blue channel = roughness
    }
    // Sample metallic-roughness texture (if not using AO-MR texture)
    else if (u_material.hasMetallicRoughnessTexture != 0) {
        vec3 mr = texture(u_metallicRoughnessTexture, TexCoords).rgb;
        metallic = mr.b; // Blue channel = metallic
        roughness = mr.g; // Green channel = roughness
    } else {
        // Use separate textures
        if (u_material.hasMetallicTexture != 0) {
            metallic *= texture(u_metallicTexture, TexCoords).r;
        }
        if (u_material.hasRoughnessTexture != 0) {
            roughness *= texture(u_roughnessTexture, TexCoords).r;
        }
    }

    // Sample AO texture if not using AO-MR texture
    if (u_material.hasAOTexture != 0 && u_material.hasAOMetallicRoughnessTexture == 0) {
        ao *= texture(u_aoTexture, TexCoords).r;
    }

    vec3 emission = u_material.emission;
    if (u_material.hasEmissionTexture != 0) {
        emission *= texture(u_emissionTexture, TexCoords).rgb;
    }

    // Get surface normal (with normal mapping)
    vec3 N = getNormalFromMap();
    vec3 V = normalize(u_cameraPosition - FragPos);

    // Use light uniform or default
    vec3 lightDir = u_light.direction;
    vec3 lightColor = u_light.color;
    float lightIntensity = u_light.intensity;

    // Fallback to default light if uniform not set (direction would be vec3(0))
    if (length(lightDir) < 0.001) {
        lightDir = vec3(0.5, -1.0, 0.3);
        lightColor = vec3(1.0, 1.0, 1.0);
        lightIntensity = 1.0;
    }

    vec3 L = normalize(-lightDir);
    vec3 H = normalize(V + L);

    // Calculate F0 for dielectrics and metals
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo.rgb, metallic);

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    // Energy conservation
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    // Lambertian diffuse
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = kD * albedo.rgb / PI;

    // Combine lighting
    vec3 Lo = (diffuse + specular) * lightColor * lightIntensity * NdotL;

    // Add ambient lighting with AO
    vec3 ambient = vec3(0.03) * albedo.rgb * ao;

    // Add emission
    vec3 color = ambient + Lo + emission;

    // Apply transmission/alpha
    float finalAlpha = albedo.a * u_material.alpha;

    FragColor = vec4(color, finalAlpha);
}

