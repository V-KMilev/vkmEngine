/**
 * One step of the loading screen's water (GLLoadingPass): a grid of heights and the speeds they rise
 * at, its ripples running across it and dying away, carried along by a slow current, and pushed down
 * where the pointer moved.
 */
layout(local_size_x = GROUP_IMAGE, local_size_y = GROUP_IMAGE) in;

layout(binding = LOADING_SLOT_WATER) uniform sampler2D u_water;  // the last step: x height, y its speed
layout(binding = 0, rg32f) uniform writeonly image2D u_next;

uniform float u_step;   // seconds
uniform vec2  u_drift;  // the current, in cells a second
uniform vec4  u_stir;   // the pointer's path this step, from xy to zw, in cells; x below zero for none
uniform float u_push;   // how hard it pushes: how fast it moved, in cells a second

// How fast a ripple runs, in cells a second; the share of its speed it keeps over a second; how
// quickly the water's own drag evens out ripples a few cells across, and the share of the way to its
// neighbours' mean a height is drawn each step, so the grid's finest ripples never ring; how wide the
// pointer's wake is, in cells; how deep a push it gives for each cell a second it moves, and the
// fastest it counts.
const float WAVE_SPEED = 60.0;
const float KEEP       = 0.45;
const float DRAG       = 13.5;
const float EVEN       = 0.04;
const float WAKE       = 6.75;
const float PUSH       = 0.001;
const float FASTEST    = 600.0;

void main() {
    const ivec2 at   = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size = imageSize(u_next);
    if (any(greaterThanEqual(at, size))) return;

    // Carried by the current: this cell's water is what lay upstream a step ago.
    const vec2 texel  = 1.0 / vec2(size);
    const vec2 from   = (vec2(at) + 0.5 - u_drift * u_step) * texel;
    const vec2 here   = texture(u_water, from).xy;
    const vec2 east   = vec2(texel.x, 0.0);
    const vec2 north  = vec2(0.0, texel.y);
    const vec2 around = texture(u_water, from + east).xy + texture(u_water, from - east).xy
        + texture(u_water, from + north).xy + texture(u_water, from - north).xy;

    float speed = here.y + WAVE_SPEED * WAVE_SPEED * (around.x - 4.0 * here.x) * u_step;
    speed += DRAG * (around.y - 4.0 * here.y) * u_step;
    speed *= pow(KEEP, u_step);

    // The pointer pushes the water down along its path, harder the faster it moved.
    if (u_stir.x >= 0.0) {
        const vec2  cell  = vec2(at) + 0.5;
        const vec2  path  = u_stir.zw - u_stir.xy;
        const float along = clamp(dot(cell - u_stir.xy, path) / max(dot(path, path), 1e-6), 0.0, 1.0);
        const float off   = distance(cell, u_stir.xy + path * along);
        speed -= PUSH * min(u_push, FASTEST) * exp(-off * off / (WAKE * WAKE));
    }
    const float height = mix(here.x, 0.25 * around.x, EVEN) + speed * u_step;
    imageStore(u_next, at, vec4(height, speed, 0.0, 0.0));
}
