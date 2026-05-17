/**
 * Unlit fragment shader.
 *
 * Albedo (optionally textured) plus emission, no lighting. Outputs LINEAR
 * scene-referred radiance into the HDR target - the composite pass owns the
 * exposure -> AgX -> sRGB display transform.
 *
 * The MaterialBlock is declared with the full std140 layout so binding 0
 * matches the shared material UBO even though only a few fields are read.
 */
#version 420 core

in vec2 vUV;

out vec4 FragColor;

const int TEX_ALBEDO   = 1 << 0;
const int TEX_EMISSION = 1 << 7;

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
    float anisotropy;           float _mp1; float _mp2; float _mp3;
    vec3  anisotropyDirection;  float _mp4;
    float subsurface;           float _mp5; float _mp6; float _mp7;
    vec3  subsurfaceColor;      float _mp8;
    float heightScale;
    float normalScale;
    int   textureFlags;
    float _mp9;
} u_material;

uniform sampler2D u_albedoTexture;
uniform sampler2D u_emissionTexture;

bool hasTex(int flag) {
    return (u_material.textureFlags & flag) != 0;
}

void main() {
    vec4 base = u_material.albedo;
    if (hasTex(TEX_ALBEDO)) {
        base *= texture(u_albedoTexture, vUV);
    }

    vec3 emission = u_material.emission;
    if (hasTex(TEX_EMISSION)) {
        emission *= texture(u_emissionTexture, vUV).rgb;
    }

    FragColor = vec4(base.rgb + emission, base.a * u_material.alpha);
}
