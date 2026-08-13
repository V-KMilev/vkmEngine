/*
 * Depth + G-buffer prepass fragment stage. Writes a view-space normal
 * (octahedral) plus roughness + metalness into colour attachment 1 - the
 * inputs GTAO + the decal pass need - while the depth attachment is primed for the forward
 * pass's early-Z. Only opaque / unlit geometry runs through here; alpha-masked
 * geometry skips the prepass and draws (with alpha-to-coverage) in the forward
 * pass, so there is no cutout to punch here.
 */

in vec3 vViewNormal;

out vec4 gbuffer;  // -> COLOR_ATTACHMENT1: oct view-normal.xy, roughness, metalness

// Matches MaterialBlock in shaders/forward/pbr (std140); the full layout is
// declared so the scalar-tail offsets line up with the bound material UBO.
#include "../../_common/material.glsl"

#include "../../_common/normal_codec.glsl"  // signNotZero, octEncode

void main() {
    vec2 oct = octEncode(normalize(vViewNormal));
    gbuffer = vec4(oct, u_material.roughness, u_material.metallic);
}
