/*
 * The scene's ocean: the swell a Sea material's surface moves by. The GPU's statement of
 * OceanSurface (system/ocean/ocean_surface.cpp): the swell's grids read between their texels as
 * SwellMoment reads them, faded and surged term for term, so what gameplay floats rides what is
 * drawn. OceanBlock matches OceanUBO (frame/gl_ocean.h); SWELL_FIELD, MAX_SWELL_CASCADES,
 * MAX_OCEAN_SURGES, OCEAN_RIPPLE_FIELDS, OCEAN_INVERSE_STEPS and OCEAN_SURGE_WHOLE come from the
 * shader prelude.
 *
 * The swell moves vertices (oceanAt); GLOceanFields draws its grids into SSBO_OCEAN_SWELL each
 * frame (ocean_swell.glsl). The ripples - the spectrum too short for any vertex - are drawn once a
 * frame into three repeating fields (ocean_ripples.glsl, GLOceanPass), and each pixel reads them
 * over the waves' frame for its own normal and foam (ocean_water.glsl), so the light on the water
 * is as fine as the screen whatever the mesh, for a few texture reads.
 */

#include "constants.glsl"

// A shore's square, as GLOceanShore lays it: xy its corner on the XZ plane (least X, most Z), z
// its edge. Where @p point falls across it, 0..1 each way.
vec2 shoreUV(vec2 point, vec4 square) {
    return vec2(point.x - square.x, square.y - point.y) / square.z;
}

// The point on the XZ plane at @p uv across @p square: shoreUV's inverse.
vec2 shorePoint(vec2 uv, vec4 square) {
    return vec2(square.x + uv.x * square.z, square.y - uv.y * square.z);
}

layout(std140, binding = UBO_OCEAN) uniform OceanBlock {
    vec4 place;    // xy = the centre on the XZ plane, z = calm radius, w = still radius
    vec4 detail;   // xy = the maps' drift in repeats, z = repeats a metre (0 no ocean), w = swell cascades
    vec4 foam;     // rgb = color, a = how readily a crest whitens
    vec4 squeeze;  // x = a squeeze (1 - the Jacobian) the waves' crests rarely pass, y = ripple
                   // fields (0 none), z = the waves' significant height, w = the squeeze the
                   // ripples rarely add
    vec4 tiles;    // xyz = repeats a metre of the long, middle and short ripples' fields,
                   // w = metres of foam where the water meets anything standing in it
    vec4 bands;    // xyz = the lowest wavenumber of each ripple field, w = the highest of the last
    vec4 spectrum; // x = JONSWAP's peak, radians a second; y = its alpha g^2; z = depth, 0 deep;
                   // w = height scale
    vec4 spread;   // xy = the way the waves roll, unit; z = the spread's power; w = its scale to one
    vec4 rippling; // x = how far through the ripples' loop, 0..1; y = radians a second a turn a loop;
                   // z = choppiness; w = seed
    vec4 swell;    // xyz = metres across each swell cascade, w = how far through the loop, 0..1
    vec4 surges[2 * MAX_OCEAN_SURGES];  // centre, direction; then height (0 none), half its
                                        // width along its roll, half its length along its crest
} u_ocean;

// A texel of a swell cascade: the carry (X, up, Z) and its X's change along Z; the height's change
// along X and Z, the carry's X along X and Z along Z. SwellTexel (system/ocean/ocean_swell.h).
struct SwellTexel {
    vec4 moved;
    vec4 slopes;
};

// What reads the swell only reads it; the passes that draw it say otherwise first.
#ifndef OCEAN_SWELL_ACCESS
#define OCEAN_SWELL_ACCESS readonly
#endif

// Every cascade's grid, SWELL_FIELD texels square, rows of Z; then the foam each crest left there.
layout(std430, binding = SSBO_OCEAN_SWELL) OCEAN_SWELL_ACCESS buffer OceanSwellBlock {
    SwellTexel b_swell[MAX_SWELL_CASCADES * SWELL_FIELD * SWELL_FIELD];
    float      b_swellFoam[MAX_SWELL_CASCADES * SWELL_FIELD * SWELL_FIELD];
};

// What a crest's foam sheds a second once the crest has passed, in squeezes it rarely passes.
const float FOAM_FADE = 0.12;

// The water resting at a point of the plane, once the waves have moved it.
struct OceanPoint {
    vec3 offset;  // from the rest position
    vec3 alongX;  // the surface's tangents by rest X and Z: its frame, and how it is squeezed
    vec3 alongZ;
};

// Whether the scene has an ocean to move anything by: a windless one still lifts its surges and
// foams its waterlines. Its maps' repeat is never zero; with no ocean the block is.
bool oceanPresent() {
    return u_ocean.detail.z > 0.0;
}

// How much of the waves reach a rest point: 1 inside the calm radius, 0 from the still one out.
float oceanReach(vec2 rest) {
    float calm  = u_ocean.place.z;
    float still = u_ocean.place.w;
    if (still <= calm) return 1.0;
    float u = clamp((length(rest - u_ocean.place.xy) - calm) / (still - calm), 0.0, 1.0);
    return 1.0 - u * u * (3.0 - 2.0 * u);
}

// How fast oceanReach changes by rest X and Z: nothing but across the band the waves fade over.
vec2 oceanReachSlope(vec2 rest) {
    float calm  = u_ocean.place.z;
    float still = u_ocean.place.w;
    if (still <= calm) return vec2(0.0);
    vec2  off = rest - u_ocean.place.xy;
    float r   = length(off);
    float u   = (r - calm) / (still - calm);
    if (r <= 0.0 || u <= 0.0 || u >= 1.0) return vec2(0.0);
    return -6.0 * u * (1.0 - u) / (still - calm) * off / r;
}

// How much a surge's crest foams, in squeezes the sea's crests rarely pass, and the metres high a
// surge must stand to foam that much: a lower one foams as much less.
const float SURGE_FOAM        = 1.1;
const float SURGE_FOAM_HEIGHT = 0.5;

// Every surge's rise at @p rest (x), and its slopes by rest X and Z (y, z): a cosine across the way
// each rolls, times a smooth taper over its ends. OceanSurface's surgeAt, term for term.
vec3 oceanSurgeAt(vec2 rest) {
    vec3 sum = vec3(0.0);
    for (int i = 0; i < MAX_OCEAN_SURGES; ++i) {
        vec4 place = u_ocean.surges[2 * i];
        vec4 size  = u_ocean.surges[2 * i + 1];
        if (size.x <= 0.0 || size.y <= 0.0 || size.z <= 0.0) continue;
        vec2  off  = rest - place.xy;
        vec2  dir  = place.zw;
        vec2  side = vec2(-dir.y, dir.x);
        float u    = dot(off, dir) / size.y;
        float v    = dot(off, side) / size.z;
        if (abs(u) >= 1.0 || abs(v) >= 1.0) continue;
        float bump   = 0.5 + 0.5 * cos(PI * u);
        float dBump  = -0.5 * PI * sin(PI * u) / size.y;
        float e      = clamp((abs(v) - OCEAN_SURGE_WHOLE) / (1.0 - OCEAN_SURGE_WHOLE), 0.0, 1.0);
        float taper  = 1.0 - e * e * (3.0 - 2.0 * e);
        float dTaper = -6.0 * e * (1.0 - e) / ((1.0 - OCEAN_SURGE_WHOLE) * size.z) * (v < 0.0 ? -1.0 : 1.0);
        vec2  slope  = size.x * (dBump * taper * dir + bump * dTaper * side);
        sum += vec3(size.x * bump * taper, slope);
    }
    return sum;
}

// A surge's crest foams: along its top, in the squeezes oceanShadeAt's pressed counts, about as much
// as a crest the sea rarely passes, so the ripples tear it as they do the sea's whitecaps.
float oceanSurgeFoam(vec2 rest) {
    float most = 0.0;
    for (int i = 0; i < MAX_OCEAN_SURGES; ++i) {
        vec4 place = u_ocean.surges[2 * i];
        vec4 size  = u_ocean.surges[2 * i + 1];
        if (size.x <= 0.0 || size.y <= 0.0 || size.z <= 0.0) continue;
        vec2  off   = rest - place.xy;
        float u     = dot(off, place.zw) / size.y;
        float v     = dot(off, vec2(-place.w, place.z)) / size.z;
        if (abs(u) >= 1.0 || abs(v) >= 1.0) continue;
        float bump  = 0.5 + 0.5 * cos(PI * u);
        float taper = 1.0 - smoothstep(OCEAN_SURGE_WHOLE, 1.0, abs(v));
        float top   = bump * bump;
        most = max(most, SURGE_FOAM * top * top * taper * min(size.x / SURGE_FOAM_HEIGHT, 1.0));
    }
    return most;
}

// The four texels of swell cascade @p c round @p rest, and the weights between them: SwellMoment's
// reading between texels, term for term.
void oceanSwellCorners(int c, vec2 rest, out int at[4], out vec4 weight) {
    const vec2  p    = rest / u_ocean.swell[c] * float(SWELL_FIELD);
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

// The swell at @p rest: where the water goes, and the surface's tangents there; a surge only lifts
// it. Faded: the fade's own slope tilts the tangents too, where it fades.
OceanPoint oceanAt(vec2 rest) {
    float fade   = oceanReach(rest);
    vec3  offset = vec3(0.0);
    vec3  alongX = vec3(0.0);
    vec3  alongZ = vec3(0.0);
    int   count  = fade > 0.0 ? int(u_ocean.detail.w) : 0;
    for (int c = 0; c < count; ++c) {
        int  at[4];
        vec4 weight;
        oceanSwellCorners(c, rest, at, weight);
        for (int i = 0; i < 4; ++i) {
            const vec4 m = b_swell[at[i]].moved;
            const vec4 s = b_swell[at[i]].slopes;
            offset += m.xyz * weight[i];
            alongX += vec3(s.z, s.x, m.w) * weight[i];
            alongZ += vec3(m.w, s.y, s.w) * weight[i];
        }
    }
    vec2 slope = oceanReachSlope(rest);
    vec3 surge = oceanSurgeAt(rest);

    OceanPoint water;
    water.offset = offset * fade + vec3(0.0, surge.x, 0.0);
    water.alongX = vec3(1.0, 0.0, 0.0) + alongX * fade + offset * slope.x + vec3(0.0, surge.y, 0.0);
    water.alongZ = vec3(0.0, 0.0, 1.0) + alongZ * fade + offset * slope.y + vec3(0.0, surge.z, 0.0);
    return water;
}

// The water's height above its rest over the world point @p point on the XZ plane: the water
// there rested elsewhere, found by fixed-point steps, as OceanSurface::heightAt finds it.
float oceanHeightAt(vec2 point) {
    vec2 rest = point;
    for (int i = 0; i < OCEAN_INVERSE_STEPS; ++i) rest = point - oceanAt(rest).offset.xz;
    return oceanAt(rest).offset.y;
}

// The foam a crest left at @p rest: the most each cascade's water there was squeezed lately, less
// what that foam has shed since, in oceanShadeAt's pressed - kept by ocean/swell_columns, a frame
// at a time - summed over the cascades and faded.
float oceanLingering(vec2 rest) {
    if (u_ocean.squeeze.x <= 0.0) return 0.0;
    float most  = 0.0;
    int   count = int(u_ocean.detail.w);
    for (int c = 0; c < count; ++c) {
        int  at[4];
        vec4 weight;
        oceanSwellCorners(c, rest, at, weight);
        for (int i = 0; i < 4; ++i) most += b_swellFoam[at[i]] * weight[i];
    }
    return oceanReach(rest) * most;
}

// The maps' coordinates at a rest point: laid over the water, drifting with the wind.
vec2 oceanUV(vec2 rest) {
    return rest * u_ocean.detail.z - u_ocean.detail.xy;
}
