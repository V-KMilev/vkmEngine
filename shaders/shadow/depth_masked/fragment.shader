// Alpha-masked caster: a texel the forward pass's cut removes (alpha times the albedo map's,
// against the cutoff) casts no shadow, so a leaf card's shadow is the leaf.
#include "../../material.glsl"

layout(binding = MATERIAL_SLOT_ALBEDO) uniform sampler2D u_albedoTexture;

in vec2 vUV;

void main() {
    float a = u_material.albedo.a;
    if (hasTex(MATERIAL_SLOT_ALBEDO)) a *= texture(u_albedoTexture, vUV).a;
    if (a < u_material.alphaCutoff) discard;
}
