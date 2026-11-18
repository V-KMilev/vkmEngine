/*
 * The AO target GTAO writes: r the visibility, gb its bent normal (octahedral, view space). u_hasAO
 * is 0 when there is none for the draw (GLPass::bindAO).
 */

#include "normal_codec.glsl"

layout(binding = POST_SLOT_AO) uniform sampler2D u_ao;
uniform int u_hasAO;

// The GTAO visibility at @p uv, 1 with none.
float aoAt(vec2 uv) {
    return u_hasAO == 1 ? texture(u_ao, uv).r : 1.0;
}

// GTAO's bent normal at @p uv, in view space; meaningful only where u_hasAO is 1.
vec3 bentNormalAt(vec2 uv) {
    return octDecode(texture(u_ao, uv).gb);
}
