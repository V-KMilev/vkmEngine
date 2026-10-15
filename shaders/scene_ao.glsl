/*
 * The AO target GTAO and the screen shadows write: r the GTAO visibility, gb its bent normal
 * (octahedral, view space), a the sun's screen shadow. u_hasAO and u_hasScreenShadow are 0 when no
 * pass wrote them this frame.
 */

#include "normal_codec.glsl"

layout(binding = POST_SLOT_AO) uniform sampler2D u_ao;
uniform int u_hasAO;
uniform int u_hasScreenShadow;

// The GTAO visibility at @p uv, 1 with none.
float aoAt(vec2 uv) {
    return u_hasAO == 1 ? texture(u_ao, uv).r : 1.0;
}

// GTAO's bent normal at @p uv, in view space; meaningful only where u_hasAO is 1.
vec3 bentNormalAt(vec2 uv) {
    return octDecode(texture(u_ao, uv).gb);
}

// The sun's screen shadow at @p uv, 1 with none.
float screenShadowAt(vec2 uv) {
    return u_hasScreenShadow == 1 ? texture(u_ao, uv).a : 1.0;
}
