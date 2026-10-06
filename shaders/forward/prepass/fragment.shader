/*
 * Depth + G-buffer prepass: an octahedral view-space normal in .rg. Alpha-masked
 * geometry skips the prepass (see GLDepthPrepass), so there is no cutout here.
 *
 * Roughness and metalness in .ba are the material's authored scalars: no map is
 * sampled, so a textured material writes its fallback, not what the forward
 * pass shades with (engine.md records why).
 */
in vec3 vViewNormal;

layout(location = OUT_GBUFFER) out vec4 gbuffer;  // oct view-normal.xy, roughness, metalness

#include "../../material.glsl"

#include "../../normal_codec.glsl"

void main() {
    vec2 oct = octEncode(normalize(vViewNormal));
    gbuffer = vec4(oct, u_material.roughness, u_material.metallic);
}
