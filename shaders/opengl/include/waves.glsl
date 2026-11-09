/*
 * The scene's waves, and each water's share of them (b_waterBodies): what water's surface moves by.
 * The GPU's statement of WaveSurface (ecs/water/wave_surface.cpp): the swell's grids read
 * between their texels as SwellMoment reads them, faded, surged and shared term for term, so what
 * gameplay floats rides what is drawn. WaveBlock is WaveUBO (frame/water/gl_waves.h) and WaterBody
 * WaterBodyGPU (frame/water/gl_water_bodies.h), each component as the C++ member's note says;
 * SWELL_FIELD, MAX_SWELL_CASCADES, MAX_WAVE_SURGES, RIPPLE_FIELDS, WAVE_INVERSE_STEPS and
 * SURGE_WHOLE come from the shader prelude.
 *
 * The swell moves vertices (wavesAt); GLWaveFields draws its grids into SSBO_SWELL each
 * frame (swell.glsl). The ripples - the spectrum too short for any vertex - are drawn once a
 * frame into three repeating fields (ripples.glsl, GLWaterPass), and each pixel reads them
 * over the waves' frame for its own normal and foam (water.glsl), so the light on the water
 * is as fine as the screen whatever the mesh, for a few texture reads.
 */

#include "constants.glsl"

// A shore's square, as GLWaterShore lays it: xy its corner on the XZ plane (least X, most Z), z
// its edge. Where @p point falls across it, 0..1 each way.
vec2 shoreUV(vec2 point, vec4 square) {
    return vec2(point.x - square.x, square.y - point.y) / square.z;
}

// The point on the XZ plane at @p uv across @p square: shoreUV's inverse.
vec2 shorePoint(vec2 uv, vec4 square) {
    return vec2(square.x + uv.x * square.z, square.y - uv.y * square.z);
}

layout(std140, binding = UBO_WAVES) uniform WaveBlock {
    vec4 place;
    vec4 detail;
    vec4 squeeze;
    vec4 tiles;
    vec4 bands;
    vec4 spectrum;
    vec4 spread;
    vec4 rippling;
    vec4 swell;
    vec4 surges[2 * MAX_WAVE_SURGES];
} u_waves;

// A water's share of the scene's waves and ripples, and its foam, by its Water's place in its storage.
struct WaterBody {
    vec4 shares;
    vec4 shore;
};
layout(std430, binding = SSBO_WATER_BODIES) readonly buffer WaterBodies {
    WaterBody b_waterBodies[];
};

// A texel of a swell cascade: the carry (X, up, Z) and its X's change along Z; the height's change
// along X and Z, the carry's X along X and Z along Z. SwellTexel (ecs/water/swell.h).
struct SwellTexel {
    vec4 moved;
    vec4 slopes;
};

// What reads the swell only reads it; the passes that draw it say otherwise first.
#ifndef SWELL_ACCESS
#define SWELL_ACCESS readonly
#endif

// Every cascade's grid, SWELL_FIELD texels square, rows of Z; then the foam each crest left there.
layout(std430, binding = SSBO_SWELL) SWELL_ACCESS buffer SwellBlock {
    SwellTexel b_swell[MAX_SWELL_CASCADES * SWELL_FIELD * SWELL_FIELD];
    float      b_swellFoam[MAX_SWELL_CASCADES * SWELL_FIELD * SWELL_FIELD];
};

// What a crest's foam sheds a second once the crest has passed, in squeezes it rarely passes.
const float FOAM_FADE = 0.12;

// The water resting at a point of the plane, once the waves have moved it.
struct WavePoint {
    vec3 offset;  // from the rest position
    vec3 alongX;  // the surface's tangents by rest X and Z: its frame, and how it is squeezed
    vec3 alongZ;
};

// How much of the waves reach a rest point: 1 inside the calm radius, 0 from the still one out.
float wavesReach(vec2 rest) {
    float calm  = u_waves.place.z;
    float still = u_waves.place.w;
    if (still <= calm) return 1.0;
    float u = clamp((length(rest - u_waves.place.xy) - calm) / (still - calm), 0.0, 1.0);
    return 1.0 - u * u * (3.0 - 2.0 * u);
}

// How fast wavesReach changes by rest X and Z: nothing but across the band the waves fade over.
vec2 wavesReachSlope(vec2 rest) {
    float calm  = u_waves.place.z;
    float still = u_waves.place.w;
    if (still <= calm) return vec2(0.0);
    vec2  off = rest - u_waves.place.xy;
    float r   = length(off);
    float u   = (r - calm) / (still - calm);
    if (r <= 0.0 || u <= 0.0 || u >= 1.0) return vec2(0.0);
    return -6.0 * u * (1.0 - u) / (still - calm) * off / r;
}

// How much a surge's crest foams, in squeezes the sea's crests rarely pass, and the meters high a
// surge must stand to foam that much: a lower one foams as much less.
const float SURGE_FOAM        = 1.1;
const float SURGE_FOAM_HEIGHT = 0.5;

// Surge @p i about @p rest: false when it is unused or @p rest lies outside it; else its height, half
// width and half length (size), the way it rolls (dir), and where @p rest lies across it, -1..1 along
// the roll (u) and along the crest (v), with how far into a tapered end v reaches (end, 0..1).
bool surgeAround(int i, vec2 rest, out vec3 size, out vec2 dir, out float u, out float v, out float end) {
    vec4 place = u_waves.surges[2 * i];
    size       = u_waves.surges[2 * i + 1].xyz;
    if (size.x <= 0.0 || size.y <= 0.0 || size.z <= 0.0) return false;
    vec2 off = rest - place.xy;
    dir      = place.zw;
    u        = dot(off, dir) / size.y;
    v        = dot(off, vec2(-dir.y, dir.x)) / size.z;
    end      = clamp((abs(v) - SURGE_WHOLE) / (1.0 - SURGE_WHOLE), 0.0, 1.0);
    return abs(u) < 1.0 && abs(v) < 1.0;
}

// The share of a surge's height its end keeps @p end into the taper: whole, falling smoothly to none.
float surgeTaper(float end) {
    return 1.0 - end * end * (3.0 - 2.0 * end);
}

// Every surge's rise at @p rest (x), and its slopes by rest X and Z (y, z): a cosine across the way
// each rolls, times a smooth taper over its ends. WaveSurface's surgeAt, term for term.
vec3 surgeAt(vec2 rest) {
    vec3 sum = vec3(0.0);
    for (int i = 0; i < MAX_WAVE_SURGES; ++i) {
        vec3  size;
        vec2  dir;
        float u, v, e;
        if (!surgeAround(i, rest, size, dir, u, v, e)) continue;
        float bump   = 0.5 + 0.5 * cos(PI * u);
        float dBump  = -0.5 * PI * sin(PI * u) / size.y;
        float taper  = surgeTaper(e);
        float dTaper = -6.0 * e * (1.0 - e) / ((1.0 - SURGE_WHOLE) * size.z) * (v < 0.0 ? -1.0 : 1.0);
        vec2  slope  = size.x * (dBump * taper * dir + bump * dTaper * vec2(-dir.y, dir.x));
        sum += vec3(size.x * bump * taper, slope);
    }
    return sum;
}

// A surge's crest foams: along its top, in the squeezes waterShade's pressed counts, about as much
// as a crest the sea rarely passes, so the ripples tear it as they do the sea's whitecaps.
float surgeFoam(vec2 rest) {
    float most = 0.0;
    for (int i = 0; i < MAX_WAVE_SURGES; ++i) {
        vec3  size;
        vec2  dir;
        float u, v, e;
        if (!surgeAround(i, rest, size, dir, u, v, e)) continue;
        float bump  = 0.5 + 0.5 * cos(PI * u);
        float taper = surgeTaper(e);
        float top   = bump * bump;
        most = max(most, SURGE_FOAM * top * top * taper * min(size.x / SURGE_FOAM_HEIGHT, 1.0));
    }
    return most;
}

// The four texels of swell cascade @p c round @p rest, and the weights between them: SwellMoment's
// reading between texels, term for term.
void swellCorners(int c, vec2 rest, out int at[4], out vec4 weight) {
    const vec2  p    = rest / u_waves.swell[c] * float(SWELL_FIELD);
    const vec2  low  = floor(p);
    const vec2  f    = p - low;
    const int   x0   = ((int(low.x) % SWELL_FIELD) + SWELL_FIELD) % SWELL_FIELD;
    const int   z0   = ((int(low.y) % SWELL_FIELD) + SWELL_FIELD) % SWELL_FIELD;
    const int   x1   = (x0 + 1) % SWELL_FIELD;
    const int   z1   = (z0 + 1) % SWELL_FIELD;
    const int   base = c * SWELL_FIELD * SWELL_FIELD;
    at[0] = base + z0 * SWELL_FIELD + x0;
    at[1] = base + z0 * SWELL_FIELD + x1;
    at[2] = base + z1 * SWELL_FIELD + x0;
    at[3] = base + z1 * SWELL_FIELD + x1;
    weight = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
}

// @p share of the swell at @p rest: where the water goes, and the surface's tangents there; a surge
// only lifts it. Faded: the fade's own slope tilts the tangents too, where it fades.
WavePoint wavesAt(vec2 rest, float share) {
    float fade   = wavesReach(rest);
    vec3  offset = vec3(0.0);
    vec3  alongX = vec3(0.0);
    vec3  alongZ = vec3(0.0);
    int   count  = fade > 0.0 ? int(u_waves.detail.w) : 0;
    for (int c = 0; c < count; ++c) {
        int  at[4];
        vec4 weight;
        swellCorners(c, rest, at, weight);
        for (int i = 0; i < 4; ++i) {
            const vec4 m = b_swell[at[i]].moved;
            const vec4 s = b_swell[at[i]].slopes;
            offset += m.xyz * weight[i];
            alongX += vec3(s.z, s.x, m.w) * weight[i];
            alongZ += vec3(m.w, s.y, s.w) * weight[i];
        }
    }
    vec2 slope = wavesReachSlope(rest);
    vec3 surge = surgeAt(rest);

    const vec3 tiltX = alongX * fade + offset * slope.x + vec3(0.0, surge.y, 0.0);
    const vec3 tiltZ = alongZ * fade + offset * slope.y + vec3(0.0, surge.z, 0.0);

    WavePoint water;
    water.offset = (offset * fade + vec3(0.0, surge.x, 0.0)) * share;
    water.alongX = vec3(1.0, 0.0, 0.0) + tiltX * share;
    water.alongZ = vec3(0.0, 0.0, 1.0) + tiltZ * share;
    return water;
}

// Moves @p worldPos, a vertex of the Water at @p body in b_waterBodies, by that body's share of the
// waves, which it returns; @p water is the waves there, set only where the share is above 0. Both
// of water's stages place it by this, so they agree under GLDepth::EYE_TEST_EQUAL.
float moveWater(uint body, inout vec4 worldPos, out WavePoint water) {
    const float share = b_waterBodies[body].shares.x;
    if (share > 0.0) {
        water = wavesAt(worldPos.xz, share);
        worldPos.xyz += water.offset;
    }
    return share;
}

// The water's height above its rest over the world point @p point on the XZ plane, moved by
// @p share of the waves: the water there rested elsewhere, found by fixed-point steps, as
// WaveSurface::heightAt finds it.
float wavesHeightAt(vec2 point, float share) {
    vec2 rest = point;
    for (int i = 0; i < WAVE_INVERSE_STEPS; ++i) rest = point - wavesAt(rest, share).offset.xz;
    return wavesAt(rest, share).offset.y;
}

// The foam a crest left at @p rest: the most each cascade's water there was squeezed lately, less
// what that foam has shed since, in waterShade's pressed - kept by water/swell/columns, a frame
// at a time - summed over the cascades and faded.
float lingeringFoam(vec2 rest) {
    if (u_waves.squeeze.x <= 0.0) return 0.0;
    float most  = 0.0;
    int   count = int(u_waves.detail.w);
    for (int c = 0; c < count; ++c) {
        int  at[4];
        vec4 weight;
        swellCorners(c, rest, at, weight);
        for (int i = 0; i < 4; ++i) most += b_swellFoam[at[i]] * weight[i];
    }
    return wavesReach(rest) * most;
}

// The maps' coordinates at a rest point: laid over the water, drifting with the wind.
vec2 wavesUV(vec2 rest) {
    return rest * u_waves.detail.z - u_waves.detail.xy;
}
