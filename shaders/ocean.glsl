/*
 * The scene's ocean: the waves a `waves` material's surface moves by. The GPU's statement of
 * OceanSurface's sum (system/ocean/ocean_surface.cpp), term for term, so what gameplay floats
 * rides what is drawn. OceanBlock matches OceanUBO (frame/gl_ocean.h); MAX_OCEAN_WAVES,
 * MAX_OCEAN_SURGES, OCEAN_RIPPLE_FIELDS, OCEAN_INVERSE_STEPS and OCEAN_SURGE_WHOLE come from the
 * shader prelude.
 *
 * The waves move vertices (oceanAt). The ripples - the spectrum too short for any vertex - are
 * drawn once a frame into three repeating fields (ocean_ripples.glsl, GLOceanPass), and each pixel
 * reads them over the waves' frame for its own normal and foam (oceanShadeAt), so the light on
 * the water is as fine as the screen whatever the mesh, for a few texture reads.
 */

#include "constants.glsl"

layout(std140, binding = UBO_OCEAN) uniform OceanBlock {
    vec4 place;    // xy = the centre on the XZ plane, z = calm radius, w = still radius
    vec4 detail;   // xy = the maps' drift in repeats, z = repeats a metre (0 no ocean), w = waves summed
    vec4 foam;     // rgb = colour, a = how readily a crest whitens
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
    vec4 waves[2 * MAX_OCEAN_WAVES];  // direction, wavenumber, phase; then amplitude, steepness,
                                      // radians a second
    vec4 surges[2 * MAX_OCEAN_SURGES];  // centre, direction; then height (0 none), half its
                                        // width along its roll, half its length along its crest
} u_ocean;

// How rough foam is, where it covers the water.
const float FOAM_ROUGHNESS = 0.7;
// The squeeze over which foam's edge goes from none to covering.
const float FOAM_EDGE = 0.06;
// Past its edge, the squeeze over which foam thickens from thin lace to whole, and how much of
// the water's colour thin foam hides: fresh foam is dense, and thins as it fades.
const float FOAM_BODY = 0.45;
const float FOAM_THIN = 0.35;
// The most foam's edge widens to hide its steps: where the squeeze changes faster than a pixel,
// unbounded, the widening would lay faint foam over water nowhere near any.
const float FOAM_BLUR = 0.2;
// What a crest's foam sheds a second once the crest has passed, in squeezes it rarely passes.
const float FOAM_FADE = 0.12;
// The moments oceanLingering looks back to: so many, so many seconds apart.
const int   FOAM_LOOKS = 6;
const float FOAM_LOOK  = 0.75;
// How far the ripples' squeeze moves foam's edge, in squeezes it rarely passes: a crest foams in
// torn patches, and its foam thins to lace as it fades.
const float FOAM_TEAR  = 0.35;
// A waterline's foam: how far into its body it stands at the contact, in oceanFroth's past; how
// far the finer ripples tear it, in their squeeze's rare reach; and how much wider a crest a
// significant height high makes it. Torn more than it is solid, it froths as suds rather than a
// collar.
const float WATERLINE_CORE  = 0.8;
const float WATERLINE_TEAR  = 1.5;
const float WATERLINE_SWELL = 0.6;
// How far past its width, in widths, the suds still carry a waterline's foam.
const float WATERLINE_FRAY  = 0.5;

// The water's scattering (oceanScatter): through a backlit crest, toward the viewer, on the lit
// side, and from the sky. Shares of the scattering colour, as albedo is of diffuse.
const float SCATTER_THROUGH = 6.0;
const float SCATTER_TOWARD  = 0.4;
const float SCATTER_LIT     = 0.15;
const float SCATTER_AMBIENT = 0.5;

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

// The Gerstner sum at @p rest: where the water goes, and the surface's tangents there. A wave
// carries water q amplitudes toward its crests and lifts it one amplitude; a surge only lifts it.
// Summed whole, then faded: the fade's own slope tilts the tangents too, where it fades.
OceanPoint oceanAt(vec2 rest) {
    float fade   = oceanReach(rest);
    vec3  offset = vec3(0.0);
    vec3  alongX = vec3(0.0);
    vec3  alongZ = vec3(0.0);
    int   count  = fade > 0.0 ? int(u_ocean.detail.w) : 0;
    for (int i = 0; i < count; ++i) {
        vec4  shape = u_ocean.waves[2 * i];
        vec4  size  = u_ocean.waves[2 * i + 1];
        vec2  dir   = shape.xy;
        float k     = shape.z;
        float amp   = size.x;
        float q     = size.y;
        float theta = k * dot(dir, rest) + shape.w;
        float s     = sin(theta);
        float c     = cos(theta);
        offset += vec3(q * amp * c * dir.x, amp * s, q * amp * c * dir.y);
        float wa   = k * amp;
        float pull = q * wa * s;
        float xz   = dir.x * dir.y;
        alongX += vec3(-pull * dir.x * dir.x, wa * dir.x * c, -pull * xz);
        alongZ += vec3(-pull * xz, wa * dir.y * c, -pull * dir.y * dir.y);
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

// The foam a crest left at @p rest: the most the water there was squeezed in the last few seconds,
// less what that foam has shed since, in oceanShadeAt's pressed. A Gerstner wave carries its water
// round and back to its rest point, so the water's past is the sum's at earlier moments, each
// wave's phase turned back; to first order its squeeze is the sum of q k a sin(theta).
float oceanLingering(vec2 rest) {
    if (u_ocean.squeeze.x <= 0.0) return 0.0;
    float fade  = oceanReach(rest);
    float most  = 0.0;
    int   count = int(u_ocean.detail.w);
    for (int look = 1; look <= FOAM_LOOKS; ++look) {
        float ago     = FOAM_LOOK * float(look);
        float squeeze = 0.0;
        for (int i = 0; i < count; ++i) {
            vec4 shape = u_ocean.waves[2 * i];
            vec4 size  = u_ocean.waves[2 * i + 1];
            float theta = shape.z * dot(shape.xy, rest) + shape.w + size.z * ago;
            squeeze += size.y * shape.z * size.x * sin(theta);
        }
        most = max(most, fade * squeeze / u_ocean.squeeze.x - FOAM_FADE * ago);
    }
    return most;
}

// How much the ripples squeeze the surface, for a crest's foam: the long ones whole and the middle
// half; the shortest are left out, too fine to read as anything but grain.
float oceanLace(vec4 fields[OCEAN_RIPPLE_FIELDS]) {
    return fields[0].z + 0.5 * fields[1].z;
}

// How much the ripples move foam's edge: their lace, in the squeezes a crest rarely passes.
float oceanTear(vec4 fields[OCEAN_RIPPLE_FIELDS]) {
    return u_ocean.squeeze.w > 0.0 ? FOAM_TEAR * oceanLace(fields) / u_ocean.squeeze.w : 0.0;
}

// The light on the water at a pixel.
struct OceanShade {
    vec3  normal;    // world, unit: the waves' frame with the ripples on it
    vec3  tangent;   // world, unit
    float pressed;   // how squeezed the surface is, in squeezes its crests rarely pass: oceanFoam's
    float variance;  // slope variance of the ripples the pixel spans: the shading's extra roughness
};

// The water at @p rest: the waves' tangents @p alongX and @p alongZ as the vertices interpolate
// them, and the three ripple fields as the pixel's footprint filters them (ocean/ripples_columns:
// slope in x and z, squeeze, and slope squared). Slope squared filters to the mean of the squares, so what
// the pixel spans is its variance, not lost (LEAN mapping, Olano and Baker 2010), and a far sea
// grows rough rather than noisy.
OceanShade oceanShadeAt(vec2 rest, vec3 alongX, vec3 alongZ, vec4 fields[OCEAN_RIPPLE_FIELDS]) {
    float fade    = oceanReach(rest);
    vec4  ripples = (fields[0] + fields[1] + fields[2]) * fade;
    float spanned = 0.0;
    for (int f = 0; f < OCEAN_RIPPLE_FIELDS; ++f) spanned += fields[f].w - dot(fields[f].xy, fields[f].xy);

    vec3 rippledX = alongX + vec3(0.0, ripples.x, 0.0);
    vec3 rippledZ = alongZ + vec3(0.0, ripples.y, 0.0);

    OceanShade shade;
    shade.normal   = normalize(cross(rippledZ, rippledX));
    shade.tangent  = normalize(rippledX);
    shade.variance = 0.5 * fade * fade * max(spanned, 0.0);

    float jacobian = alongX.x * alongZ.z - alongX.z * alongZ.x;
    float squeezed = u_ocean.squeeze.x > 0.0 ? (1.0 - jacobian) / u_ocean.squeeze.x : 0.0;
    shade.pressed  = squeezed;
    return shade;
}

// The foam on water @p past its onset, 0..1: a sharp edge, then thin where barely past and dense
// FOAM_BODY on. @p blur is how fast @p past changes across the pixel (its fwidth), which widens the
// edge, up to FOAM_BLUR, so it never steps from texel to texel.
float oceanFroth(float past, float blur) {
    blur = min(blur, FOAM_BLUR);
    float cover = smoothstep(-blur, FOAM_EDGE + blur, past);
    float body  = smoothstep(0.0, FOAM_BODY, past);
    return cover * mix(FOAM_THIN, 1.0, body);
}

// The foam on water squeezed @p pressed, whose fwidth is @p blur.
float oceanFoam(float pressed, float blur) {
    return u_ocean.foam.a > 0.0 ? oceanFroth(pressed - (1.0 - u_ocean.foam.a), blur) : 0.0;
}

// How far into the foam where something stands in the water a pixel is, for oceanFroth: @p apart
// metres from the nearest thing standing in it, the most at the contact and none from the ocean's
// waterline width out. The middle and short ripples (@p fields, as the pixel reads them) tear it
// into suds, and a crest standing @p crest high (oceanCrest) widens it, so it washes out and back
// as the waves pass. Past the width the suds alone carry it, for WATERLINE_FRAY of a width; and
// that keeps inside @p reach, as far as the shore looks, past which it says nothing.
float oceanWaterline(float apart, float reach, vec4 fields[OCEAN_RIPPLE_FIELDS], float crest) {
    if (u_ocean.tiles.w <= 0.0 || apart >= reach) return -1.0;
    float suds  = u_ocean.squeeze.w > 0.0 ? (fields[1].z + fields[2].z) / u_ocean.squeeze.w : 0.0;
    float swell = u_ocean.tiles.w * max(1.0 + WATERLINE_SWELL * crest, 0.0);
    float width = min(swell, reach / (1.0 + WATERLINE_FRAY));
    float near  = 1.0 - apart / max(width, 1e-3);
    // The suds tear it whole to the width's edge, and fray it out over WATERLINE_FRAY of a width
    // past that; further, nothing foams however the suds stand.
    float torn  = clamp(1.0 + near / WATERLINE_FRAY, 0.0, 1.0);
    return WATERLINE_CORE * near + WATERLINE_TEAR * suds * torn;
}

// How high the water stands over its rest, in the waves' significant heights: what lifts a crest
// thin enough to light through.
float oceanCrest(float height) {
    return u_ocean.squeeze.z > 0.0 ? height / u_ocean.squeeze.z : 0.0;
}

// The light the water scatters back out of light @p radiance arriving along @p L (after Atlas's
// ocean, GDC 2019): through a crest standing @p crest high when looking toward the light, most
// where the face turns from it; toward the viewer from a face turned to them; and a little
// wherever the light falls. @p colour is the water's scattering; the caller weighs it by what the
// surface lets in, one less its Fresnel.
vec3 oceanScatter(vec3 colour, vec3 N, vec3 V, vec3 L, vec3 radiance, float crest) {
    float through = max(crest, 0.0) * pow(clamp(dot(L, -V), 0.0, 1.0), 4.0) * pow(0.5 - 0.5 * dot(L, N), 3.0);
    float toward  = pow(clamp(dot(V, N), 0.0, 1.0), 2.0);
    float lit     = clamp(dot(N, L), 0.0, 1.0);
    float share   = SCATTER_THROUGH * through + SCATTER_TOWARD * toward + SCATTER_LIT * lit;
    return colour / PI * radiance * share;
}

// The maps' coordinates at a rest point: laid over the water, drifting with the wind.
vec2 oceanUV(vec2 rest) {
    return rest * u_ocean.detail.z - u_ocean.detail.xy;
}
