/*
 * The loading screen's water, seen from above (GLLoadingPass): clear and dark over a floor of stones,
 * a slow current carrying it along, the pointer's wake running across it (u_water). Its slopes bend
 * the floor seen through it and gather the light into caustics on the floor; a light glints off them.
 * Laid over the world at the screen's opacity.
 */

#include "../../constants.glsl"
#include "../../color.glsl"
#include "../../noise.glsl"

in vec2 vUV;

out vec4 FragColor;

layout(binding = LOADING_SLOT_WATER) uniform sampler2D u_water;  // x height, y its speed

uniform float u_opacity;
uniform float u_aspect;  // the screen's width over its height
uniform float u_time;    // seconds since the screen came up
uniform vec2  u_flow;    // how far the current has carried the water, in screen heights
uniform bool  u_black;   // the project turned the water off (RenderView::loadingWater): plain black

// The light, high and ahead-left, and its color; the water's color where it is deep; how much of
// the floor's light it lets through, and the light the floor has from the sky alone.
const vec3  LIGHT_WAY = vec3(-0.30, 0.42, 0.86);
const vec3  LIGHT     = vec3(1.0, 0.97, 0.92);
const vec3  DEEP      = vec3(0.003, 0.022, 0.030);
const float CLEAR     = 0.55;
const float SKY       = 0.05;
// How steep the simulated water's heights make its slopes; how deep it is, in screen heights, for how
// far its slopes shift the floor seen through it; how much its curvature gathers the light.
const float SLOPE     = 210.0;
const float DEPTH     = 0.035;
const float FOCUS     = 2.7e4;
// The fine ripples the current carries: their size, in screen heights, how steep they are, and how
// fast they change.
const float RIPPLE_SIZE  = 0.045;
const float RIPPLE_SLOPE = 0.06;
const float RIPPLE_PACE  = 0.35;
// The caustic web on the floor: its cells, in screen heights, how bright, how fast it moves, and how
// far the water's slow swirl bends it, in cells.
const float WEB_SIZE  = 0.12;
const float WEB_LIGHT = 1.4;
const float WEB_PACE  = 0.3;
const float WEB_BEND  = 0.45;
// The glint: how tight, how bright.
const float SHINE = 500.0;
const float GLINT = 1.2;
// The sheen the wake's broad slopes catch, the lobe of the water's mean surface: how tight, how bright.
const float SHEEN_SHINE = 400.0;
const float SHEEN       = 0.3;

// A value in 0..1 for each lattice point of @p p.
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Smooth noise in 0..1 over @p p.
float valueNoise(vec2 p) {
    const vec2 i = floor(p);
    const vec2 f = fract(p);
    const vec2 u = f * f * (3.0 - 2.0 * f);
    const float low  = mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x);
    const float high = mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0)), u.x);
    return mix(low, high, u.y);
}

// Gradient noise at @p p and its exact derivative: x the noise, about -0.7..0.7, yz its slope. Smooth
// to the second derivative, so no lattice shows in a slope taken from it.
vec3 gradientNoise(vec2 p) {
    const vec2  i  = floor(p);
    const vec2  f  = fract(p);
    const vec2  u  = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    const vec2  du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    const float a0 = 2.0 * PI * hash(i);
    const float a1 = 2.0 * PI * hash(i + vec2(1.0, 0.0));
    const float a2 = 2.0 * PI * hash(i + vec2(0.0, 1.0));
    const float a3 = 2.0 * PI * hash(i + vec2(1.0));
    const vec2  g0 = vec2(cos(a0), sin(a0));
    const vec2  g1 = vec2(cos(a1), sin(a1));
    const vec2  g2 = vec2(cos(a2), sin(a2));
    const vec2  g3 = vec2(cos(a3), sin(a3));
    const float v0 = dot(g0, f);
    const float v1 = dot(g1, f - vec2(1.0, 0.0));
    const float v2 = dot(g2, f - vec2(0.0, 1.0));
    const float v3 = dot(g3, f - vec2(1.0));
    const float k  = v0 - v1 - v2 + v3;
    const float v  = v0 + u.x * (v1 - v0) + u.y * (v2 - v0) + u.x * u.y * k;
    const vec2  d  = g0 + u.x * (g1 - g0) + u.y * (g2 - g0) + u.x * u.y * (g0 - g1 - g2 + g3)
        + du * (u.yx * k + vec2(v1, v2) - v0);
    return vec3(v, d);
}

// The fine ripples' slope at @p p: two octaves of gradient noise, the finer turned against the
// other, each drifting its own way as time @p t passes.
vec2 rippleSlope(vec2 p, float t) {
    const mat2 turn = mat2(0.80, 0.60, -0.60, 0.80);
    const vec2 one  = gradientNoise(p + vec2(0.7, 0.3) * t).yz;
    const vec2 two  = turn * gradientNoise(2.1 * (transpose(turn) * p) - vec2(0.4, 0.9) * t).yz * 2.1;
    return one + 0.5 * two;
}

// The caustic web at @p p: bright where two moving cells meet, as light focused by a rippled surface
// is on the floor under it.
float web(vec2 p, float t) {
    const vec2 cell = floor(p);
    float first  = 8.0;
    float second = 8.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const vec2  node = cell + vec2(x, y);
            const vec2  seed = vec2(hash(node), hash(node + 17.0));
            const vec2  at   = node + 0.5 + 0.4 * sin(t + 2.0 * PI * seed);
            const float d    = distance(p, at);
            if (d < first) {
                second = first;
                first  = d;
            } else if (d < second) {
                second = d;
            }
        }
    }
    return exp(-9.0 * (second - first));
}

// The caustic light on the floor at @p p, in web cells: two webs bent by a slow swirl, one finer,
// brighter in some places than others, as a rippled surface's focus wanders.
float caustics(vec2 p, float t) {
    const vec2  bend   = WEB_BEND * vec2(valueNoise(p * 0.6 + t * 0.4), valueNoise(p * 0.6 - t * 0.3 + 7.0));
    const float coarse = web(p + bend, t);
    const float fine   = web(1.9 * p - bend + 3.1, 1.3 * t);
    const float wander = 0.25 + valueNoise(p * 0.3 + t * 0.1);
    return pow(0.85 * coarse + 0.25 * fine, 2.0) * wander;
}

// The floor: flat stones of a dark grey, a little uneven.
vec3 floorAt(vec2 p) {
    const mat2  turn   = mat2(0.80, 0.60, -0.60, 0.80);
    const float stones = 0.5 + 0.6 * gradientNoise(p * 14.0).x + 0.4 * gradientNoise(turn * p * 37.0).x;
    const float grit   = 0.5 + 0.7 * gradientNoise(transpose(turn) * p * 160.0).x;
    return vec3(0.16, 0.17, 0.17) * (0.55 + 0.45 * stones) * (0.85 + 0.15 * grit);
}

// The water at @p uv: x its height, yz its slope along x and y, w its curvature, all a cell's worth,
// from the 4x4 cells about it through a cubic B-spline. Read cell by cell: a texture's own filter
// steps its weights finely, which slopes this steep would show as squares across the glints.
vec4 waterAt(vec2 uv) {
    const ivec2 size = textureSize(u_water, 0);
    const vec2  at   = uv * vec2(size) - 0.5;
    const ivec2 base = ivec2(floor(at)) - 1;
    const vec2  f    = fract(at);
    // The B-spline's four weights each way, and their first and second derivatives.
    const vec2 g     = 1.0 - f;
    const vec2 w[4]  = vec2[4](
        g * g * g / 6.0,
        (3.0 * f * f * f - 6.0 * f * f + 4.0) / 6.0,
        (-3.0 * f * f * f + 3.0 * f * f + 3.0 * f + 1.0) / 6.0,
        f * f * f / 6.0
    );
    const vec2 d[4]  = vec2[4](-0.5 * g * g, 1.5 * f * f - 2.0 * f, -1.5 * f * f + f + 0.5, 0.5 * f * f);
    const vec2 d2[4] = vec2[4](g, 3.0 * f - 2.0, 1.0 - 3.0 * f, f);
    vec4 water = vec4(0.0);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const ivec2 cell  = (base + ivec2(x, y) + size) % size;
            const float h     = texelFetch(u_water, cell, 0).x;
            const float curve = d2[x].x * w[y].y + w[x].x * d2[y].y;
            water += h * vec4(w[x].x * w[y].y, d[x].x * w[y].y, w[x].x * d[y].y, curve);
        }
    }
    return water;
}

void main() {
    if (u_black) {
        FragColor = vec4(0.0, 0.0, 0.0, u_opacity);
        return;
    }
    const vec2 at = vec2(vUV.x * u_aspect, vUV.y);  // in screen heights

    // The water's slopes: the simulated ripples and wake, and the fine ripples on them.
    const vec4 water = waterAt(vUV);
    const vec2 broad = 2.0 * water.yz * SLOPE;
    const vec2 slope = broad + rippleSlope((at - u_flow) / RIPPLE_SIZE, u_time * RIPPLE_PACE) * RIPPLE_SLOPE;
    const vec3 normal = normalize(vec3(-slope, 1.0));

    // The floor through it, shifted by its slopes; lit by the sky and by a caustic web, brighter
    // where the wake's crests focus it.
    const vec2  under   = at - normal.xy * DEPTH;
    const float curve   = water.w;
    const float focused = clamp(1.0 - FOCUS * curve, 0.55, 2.5);
    const float caustic = WEB_LIGHT * caustics((under - u_flow) / WEB_SIZE, u_time * WEB_PACE) * focused;
    const float sun     = max(dot(vec3(0.0, 0.0, 1.0), normalize(LIGHT_WAY)), 0.0);
    const vec3  lit     = floorAt(under) * (SKY + LIGHT * sun * (0.15 + caustic));
    // The water takes the red out of what comes up through it first.
    vec3 shown = mix(DEEP, lit * vec3(0.70, 0.93, 1.0), CLEAR);

    // The light glinting off the slopes that face it, seen from straight above.
    const vec3 halfway = normalize(normalize(LIGHT_WAY) + vec3(0.0, 0.0, 1.0));
    // A soft sheen where the broad slopes face the light, and sparkles where the whole surface does,
    // thinned where the sheen is bright, so a wide slope lights a sheen, not a field of glitter.
    const vec3  mean    = normalize(vec3(-broad, 1.0));
    const float sheen   = SHEEN * pow(max(dot(mean, halfway), 0.0), SHEEN_SHINE);
    const float spread  = 1.0 - 0.7 * smoothstep(0.0, 0.6, sheen / SHEEN);
    const float sparkle = GLINT * pow(max(dot(normal, halfway), 0.0), SHINE) * spread;
    shown += LIGHT * min(sheen + sparkle, 1.0);

    // Darker toward the edges.
    const vec2 off = (vUV - 0.5) * vec2(u_aspect, 1.0);
    shown *= 1.0 - 0.45 * smoothstep(0.35, 1.0, length(off));

    const float grain = (interleavedGradientNoise(gl_FragCoord.xy) - 0.5) / 255.0;
    FragColor = vec4(linearToSrgb(shown) + grain, u_opacity);
}
