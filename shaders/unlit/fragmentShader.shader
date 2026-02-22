/**
 * Unlit Fragment Shader
 *
 * Outputs albedo color directly without any lighting calculations.
 * Uses the same MaterialBlock UBO layout as PBR for compatibility.
 */
#version 420 core

in vec2 TexCoords;

out vec4 FragColor;

// Texture flags (must match MaterialTextureFlags in C++)
const int TEXTURE_FLAG_ALBEDO   = 1 << 0;
const int TEXTURE_FLAG_EMISSION = 1 << 7;

layout(std140, binding = 0) uniform MaterialBlock {
    vec4 albedo;
    vec3 emission;
    float pad0;

    float metallic;
    float roughness;
    float ior;
    float transmission;

    float alpha;
    float ao;
    float clearcoat;
    float clearcoatRoughness;

    float anisotropy;
    float pad1_0;
    float pad1_1;
    float pad1_2;

    vec3 anisotropyDirection;
    float pad2;

    float subsurface;
    float pad3_0;
    float pad3_1;
    float pad3_2;

    vec3 subsurfaceColor;
    float pad4;

    float heightScale;
    float normalScale;
    int   textureFlags;
    float pad5;
} u_material;

uniform sampler2D u_albedoTexture;
uniform sampler2D u_emissionTexture;

bool hasTexture(int flags, int flag) {
    return (flags & flag) != 0;
}

void main() {
    vec4 color = u_material.albedo;

    if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_ALBEDO)) {
        color *= texture(u_albedoTexture, TexCoords);
    }

    vec3 emission = u_material.emission;
    if (hasTexture(u_material.textureFlags, TEXTURE_FLAG_EMISSION)) {
        emission *= texture(u_emissionTexture, TexCoords).rgb;
    }

    float finalAlpha = color.a * u_material.alpha;
    FragColor = vec4(color.rgb + emission, finalAlpha);
}
