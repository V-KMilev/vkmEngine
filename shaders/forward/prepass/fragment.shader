/*
 * Depth + G-buffer prepass: an octahedral view-space normal. Alpha-masked geometry
 * skips the prepass (see GLDepthPrepass), so there is no cutout here.
 */
in vec3 vViewNormal;

layout(location = OUT_GBUFFER) out vec2 gbuffer;  // oct view-normal

#include "../../normal_codec.glsl"

void main() {
    // A double-sided material's back face faces the other way.
    vec3 n = normalize(vViewNormal);
    gbuffer = octEncode(gl_FrontFacing ? n : -n);
}
