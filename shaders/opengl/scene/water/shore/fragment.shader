/**
 * The water's shore, captured (GLWaterPass::captureShore): the scene looking straight down, in
 * two draws. Under: only what lies under the water, the topmost of it a texel - a face turned up
 * is something under the surface, and the texel holds how far under; a face turned away is the
 * inside of something breaking it, and holds 0. Above: only what lies over the water, the lowest
 * of it - a face turned up is the top of something whose inside the surface is in, and holds 0.
 * Either alone misses a post standing on a seabed, or one under a roof; together they see both.
 * What says nothing holds u_nothing.
 */

#include "../../../include/waves.glsl"

in vec3 vWorld;

// scene/water/shore/water: its height above its rest.
layout(binding = WATER_SLOT_SHORE_WATER) uniform sampler2D u_water;
uniform float u_level;    // where the water rests
uniform vec4  u_area;     // the shore's square (shoreUV)
uniform int   u_drawOver;    // 1 for the draw over the water, 0 for the one under it
uniform float u_nothing;  // meters that say nothing breaks the water here

layout(location = 0) out float outUnder;  // meters under the water

void main() {
    const vec2  uv    = shoreUV(vWorld.xz, u_area);
    const float water = u_level + texture(u_water, uv).r;
    if (u_drawOver == 1) {
        if (vWorld.y < water) discard;
        outUnder = gl_FrontFacing ? 0.0 : u_nothing;
    } else {
        if (vWorld.y > water) discard;
        outUnder = gl_FrontFacing ? water - vWorld.y : 0.0;
    }
}
