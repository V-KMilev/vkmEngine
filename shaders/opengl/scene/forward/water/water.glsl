/*
 * Water as a pixel shows it: the waves' frame the vertices left, its share of the ripples it can
 * show (GLWaveFields), running at its flow, and its foam - whitecaps and waterlines.
 */

#include "../../../include/bspline.glsl"
#include "../../../include/waves.glsl"

// The ripple fields (GLWaveFields), repeating, a layer each, the longest ripples in 0.
layout(binding = WATER_SLOT_RIPPLES) uniform sampler2DArray u_ripples;
// Meters from each point of a square of sea to whatever breaks the water (GLWaterShore), and the
// square (shoreUV); w how far it looks round a point, 0 when there is none.
layout(binding = WATER_SLOT_SHORE) uniform sampler2D u_shoreMap;
uniform vec4 u_shore;
uniform float u_shoreLevel;  // the height of the water the shore was captured at

// How rough foam is, where it covers the water, and its color, linear.
const float FOAM_ROUGHNESS = 0.7;
const vec3  FOAM_COLOR     = vec3(0.9, 0.94, 0.96);
// The squeeze over which foam's edge goes from none to covering.
const float FOAM_EDGE = 0.06;
// Past its edge, the squeeze over which foam thickens from thin lace to whole, and how much of
// the water's color thin foam hides: fresh foam is dense, and thins as it fades.
const float FOAM_BODY = 0.45;
const float FOAM_THIN = 0.35;
// The most foam's edge widens to hide its steps: where the squeeze changes faster than a pixel,
// unbounded, the widening would lay faint foam over water nowhere near any.
const float FOAM_BLUR = 0.2;
// How far the ripples' squeeze moves foam's edge, in squeezes it rarely passes: a crest foams in
// torn patches, and its foam thins to lace as it fades.
const float FOAM_TEAR  = 0.35;
// How much harder the ripples tear the foam a crest left than the crest's own.
const float FOAM_AGED_TEAR = 2.0;
// A waterline's foam: how far into its body it stands at the contact, in foamFroth's past; how
// far the finer ripples tear it, in their squeeze's rare reach; and how much wider a crest a
// significant height high makes it. Torn more than it is solid, it froths as suds rather than a
// collar.
const float WATERLINE_CORE  = 0.8;
const float WATERLINE_TEAR  = 1.5;
const float WATERLINE_SWELL = 0.6;
// How far past its width, in widths, the suds still carry a waterline's foam.
const float WATERLINE_FRAY  = 0.5;

// The water's scattering (waterScatter): through a backlit crest, toward the viewer, on the lit
// side, and from the sky. Shares of the scattering color, as albedo is of diffuse.
const float SCATTER_THROUGH = 6.0;
const float SCATTER_TOWARD  = 0.4;
const float SCATTER_LIT     = 0.15;
const float SCATTER_AMBIENT = 0.5;

// How much the ripples squeeze the surface, for a crest's foam: the long ones whole and the middle
// half; the shortest are left out, too fine to read as anything but grain.
float rippleLace(vec4 fields[RIPPLE_FIELDS]) {
    return fields[0].z + 0.5 * fields[1].z;
}

// How much the ripples move foam's edge: their lace, in the squeezes a crest rarely passes.
float rippleTear(vec4 fields[RIPPLE_FIELDS]) {
    return u_waves.squeeze.w > 0.0 ? FOAM_TEAR * rippleLace(fields) / u_waves.squeeze.w : 0.0;
}

// The light on the water at a pixel.
struct WaterShade {
    vec3  normal;    // world, unit: the waves' frame with the ripples on it
    vec3  tangent;   // world, unit
    float pressed;   // how squeezed the surface is, in squeezes its crests rarely pass (crestFoam)
    float variance;  // slope variance of the ripples the pixel spans: the shading's extra roughness
};

// The foam on water @p past its onset, 0..1: a sharp edge, then thin where barely past and dense
// FOAM_BODY on. @p blur is how fast @p past changes across the pixel (its fwidth), which widens the
// edge, up to FOAM_BLUR, so it never steps from texel to texel.
float foamFroth(float past, float blur) {
    blur = min(blur, FOAM_BLUR);
    float cover = smoothstep(-blur, FOAM_EDGE + blur, past);
    float body  = smoothstep(0.0, FOAM_BODY, past);
    return cover * mix(FOAM_THIN, 1.0, body);
}

// The foam on water squeezed @p pressed, whose fwidth is @p blur, as readily as @p readiness
// (Water::foam) whitens it.
float crestFoam(float pressed, float blur, float readiness) {
    return readiness > 0.0 ? foamFroth(pressed - (1.0 - readiness), blur) : 0.0;
}

// How far into the foam where something stands in the water a pixel is, for foamFroth: @p apart
// meters from the nearest thing standing in it, the most at the contact and none from @p width,
// the water's waterline (Water::waterline), out. The middle and short ripples (@p fields, as the
// pixel reads them) tear it into suds, and a crest standing @p crest high (crestOf) widens it,
// so it washes out and back as the waves pass. Past the width the suds alone carry it, for
// WATERLINE_FRAY of a width; and that keeps inside @p reach, as far as the shore looks, past which
// it says nothing.
float waterlineFoam(float apart, float reach, vec4 fields[RIPPLE_FIELDS], float crest, float width) {
    if (width <= 0.0 || apart >= reach) return -1.0;
    float suds  = u_waves.squeeze.w > 0.0 ? (fields[1].z + fields[2].z) / u_waves.squeeze.w : 0.0;
    float swell = width * max(1.0 + WATERLINE_SWELL * crest, 0.0);
    float wide  = min(swell, reach / (1.0 + WATERLINE_FRAY));
    float near  = 1.0 - apart / max(wide, 1e-3);
    // The suds tear it whole to the width's edge, and fray it out over WATERLINE_FRAY of a width
    // past that; further, nothing foams however the suds stand.
    float torn  = clamp(1.0 + near / WATERLINE_FRAY, 0.0, 1.0);
    return WATERLINE_CORE * near + WATERLINE_TEAR * suds * torn;
}

// How high the water stands over its rest, in the waves' significant heights: what lifts a crest
// thin enough to light through.
float crestOf(float height) {
    return u_waves.squeeze.z > 0.0 ? height / u_waves.squeeze.z : 0.0;
}

// The light the water scatters back out of light @p radiance arriving along @p L (after Mihelich
// and Tcheblokov 2019): through a crest standing @p crest high when looking toward the light, most
// where the face turns from it; toward the viewer from a face turned to them; and a little
// wherever the light falls. @p color is the water's scattering; the caller weighs it by what the
// surface lets in, one less its Fresnel.
vec3 waterScatter(vec3 color, vec3 N, vec3 V, vec3 L, vec3 radiance, float crest) {
    float through = max(crest, 0.0) * pow(clamp(dot(L, -V), 0.0, 1.0), 4.0) * pow(0.5 - 0.5 * dot(L, N), 3.0);
    float toward  = pow(clamp(dot(V, N), 0.0, 1.0), 2.0);
    float lit     = clamp(dot(N, L), 0.0, 1.0);
    float share   = SCATTER_THROUGH * through + SCATTER_TOWARD * toward + SCATTER_LIT * lit;
    return color / PI * radiance * share;
}

// How much of the shore's square a waterline fades out over at its edges.
const float SHORE_FADE = 0.1;

// Ripple field @p layer at @p uv, B-spline filtered: foam's threshold over a field magnified on
// screen would pick out every texel's square of a bilinear read.
vec4 smoothRipples(vec2 uv, float layer) {
    vec2 at0, at1, g0, g1;
    bsplineReads(uv, vec2(textureSize(u_ripples, 0).xy), at0, at1, g0, g1);
    return bsplineCombine(
        g0,
        g1,
        texture(u_ripples, vec3(at0.x, at0.y, layer)),
        texture(u_ripples, vec3(at1.x, at0.y, layer)),
        texture(u_ripples, vec3(at0.x, at1.y, layer)),
        texture(u_ripples, vec3(at1.x, at1.y, layer))
    );
}

// Meters from @p point on the XZ plane to whatever breaks the water, and how much of a waterline
// the shore can say there: 1 inside its square, fading to 0 at its edges.
vec2 shoreAt(vec2 point) {
    const vec2 uv   = shoreUV(point, u_shore);
    const vec2 edge = min(uv, 1.0 - uv);
    const float say = clamp(min(edge.x, edge.y) / SHORE_FADE, 0.0, 1.0);
    return vec2(texture(u_shoreMap, uv).r, say);
}

// GGX's alpha for water of perceptual roughness @p roughness whose ripples, too fine for the pixel,
// spread its slope by @p variance: alpha squared takes twice their variance, as Beckmann's would.
float waterAlpha(float roughness, float variance) {
    const float alpha = roughness * roughness;
    return sqrt(alpha * alpha + 2.0 * variance);
}

// Seconds a flowing water's ripples run before they start over (readRipples), near enough: short
// enough that they never stretch far. The cycle taken is the ripples' own loop over a whole number,
// so neither jumps where the loop ends.
const float FLOW_CYCLE = 2.0;

// Meters a water's rest may lie off the shore's level and still foam at the shore's waterlines.
const float SHORE_LEVEL_SPAN = 0.05;

// The three ripple fields at @p at as the pixel's footprint filters them (water/ripples/columns:
// slope in x and z, squeeze, and slope squared), running at @p flow meters a second across the
// plane; @p spanned the slope variance the pixel spans of them. Slope squared filters to the mean of
// the squares, so what the pixel spans is its variance, not lost (LEAN mapping, Olano and Baker
// 2010), and a far water grows rough rather than noisy. Running water reads them twice, half a cycle
// apart, each faded in and out over its cycle as it runs - the flow map's two phases (Vlachos 2010) -
// so the ripples carry on downstream and never stretch.
void readRipples(vec2 at, vec2 flow, out vec4 fields[RIPPLE_FIELDS], out float spanned) {
    for (int f = 0; f < RIPPLE_FIELDS; ++f) fields[f] = vec4(0.0);
    spanned = 0.0;
    // With no ripples the fields are not read, and their unit holds another pass's texture.
    if (u_waves.squeeze.y <= 0.0) return;
    // The ripples' loop in seconds, from the turn a second it makes, and the cycles it holds.
    const bool  runs    = flow != vec2(0.0);
    const float turning = u_waves.rippling.y;
    const float loop    = turning > 0.0 ? 2.0 * PI / turning : FLOW_CYCLE;
    const float cycles  = max(round(loop / FLOW_CYCLE), 1.0);
    const float cycle   = loop / cycles;
    const int   phases  = runs ? 2 : 1;
    // Where in its cycle a place is, wandering over some fifteen meters, so the cycles' joins do not
    // fall on the whole surface at once (Vlachos 2010); still in time, so the loop stays whole.
    const float wander = 0.25 * sin(0.37 * at.x + 0.21 * at.y) + 0.25 * sin(0.17 * at.x - 0.43 * at.y);
    float shares = 0.0;
    for (int p = 0; p < phases; ++p) {
        const float into   = fract(u_waves.rippling.x * cycles + 0.5 * float(p) + wander);
        const float weight = phases == 1 ? 1.0 : 1.0 - abs(2.0 * into - 1.0);
        shares += weight * weight;
        const vec2 from = runs ? at - flow * (into - 0.5) * cycle : at;
        // The longest ripples' texels are the widest on screen: those are B-spline filtered.
        vec4 read[RIPPLE_FIELDS];
        read[0] = smoothRipples(from * u_waves.tiles.x, 0.0);
        read[1] = texture(u_ripples, vec3(from * u_waves.tiles.y, 1.0));
        read[2] = texture(u_ripples, vec3(from * u_waves.tiles.z, 2.0));
        for (int f = 0; f < RIPPLE_FIELDS; ++f) {
            fields[f] += runs ? weight * read[f] : read[f];
            spanned   += weight * (read[f].w - dot(read[f].xy, read[f].xy));
        }
    }
    // Two phases blended keep their slopes' spread only scaled by the weights' length: midway they
    // would halve its variance, and the ripples pulse (Heitz and Neyret 2018).
    if (runs) {
        for (int f = 0; f < RIPPLE_FIELDS; ++f) fields[f] /= sqrt(max(shares, 1e-4));
    }
}

// A point of water as its vertices left it (forward/water).
struct WaterPoint {
    vec2  rest;       // where it rests on the XZ plane: where its ripples and reach are read
    vec3  position;   // world, where the waves carried it
    float height;     // how high the waves lifted it over its rest
    vec3  normal;     // its mesh's, unit: the frame of water with no waves
    vec3  tangent;    // its mesh's +U
    vec3  alongX;     // the waves' tangents there (WavePoint): the frame of water with waves, and
    vec3  alongZ;     // how they squeeze it
    float lingering;  // the foam a crest left there
};

// The light on @p body's water at @p at, and its whitecaps and waterline, @p foam, 0..1. Its ripples
// fade with its waves out past the calm radius; with no waves they keep to its mesh's own surface, and
// nothing squeezes it.
WaterShade waterShade(WaterBody body, WaterPoint at, out float foam) {
    const float waves = body.shares.x;
    const vec3  along = at.tangent - at.normal * dot(at.normal, at.tangent);
    const vec2  run   = dot(along.xz, along.xz) > 1e-8 ? normalize(along.xz) : vec2(0.0);
    vec4  fields[RIPPLE_FIELDS];
    float spanned;
    readRipples(at.rest, run * body.shares.z, fields, spanned);
    const float shown = body.shares.y * (waves > 0.0 ? wavesReach(at.rest) : 1.0);
    for (int f = 0; f < RIPPLE_FIELDS; ++f) fields[f] *= shown;
    const vec4 ripples = fields[0] + fields[1] + fields[2];

    WaterShade shade;
    shade.variance = 0.5 * shown * shown * max(spanned, 0.0);
    if (waves > 0.0) {
        // The ripples' slopes added to the waves' tangents.
        const vec3 rippledX = at.alongX + vec3(0.0, ripples.x, 0.0);
        const vec3 rippledZ = at.alongZ + vec3(0.0, ripples.y, 0.0);
        shade.normal  = normalize(cross(rippledZ, rippledX));
        shade.tangent = normalize(rippledX);
    } else {
        // The ripples' slopes are the height's change along world X and Z: they tilt the normal.
        shade.normal  = normalize(at.normal - vec3(ripples.x, 0.0, ripples.y));
        shade.tangent = at.tangent;
    }
    const float jacobian = at.alongX.x * at.alongZ.z - at.alongX.z * at.alongZ.x;
    shade.pressed = u_waves.squeeze.x > 0.0 ? (1.0 - jacobian) / u_waves.squeeze.x : 0.0;

    // Foam where a crest squeezes the water now, dense, its edge torn by the ripples; and where one
    // did a moment ago, faded to lace: thin, and torn harder.
    const float tear  = rippleTear(fields);
    const float fresh = shade.pressed + tear;
    const float aged  = at.lingering + FOAM_AGED_TEAR * tear;
    const float ready = body.shares.w;
    foam = max(crestFoam(fresh, fwidth(fresh), ready), FOAM_THIN * crestFoam(aged, fwidth(aged), ready));
    // A waterline, at the shore's level alone: water near whatever breaks it, torn by the ripples and
    // washed out and back by the waves.
    if (u_shore.w > 0.0 && body.shore.x > 0.0) {
        const vec2  shore = shoreAt(at.position.xz);
        const float crest = crestOf(at.height);
        const float past  = waterlineFoam(shore.x, u_shore.w, fields, crest, body.shore.x);
        const float level = abs(at.position.y - at.height - u_shoreLevel) <= SHORE_LEVEL_SPAN ? 1.0 : 0.0;
        foam = max(foam, level * shore.y * foamFroth(past, fwidth(past)));
    }
    return shade;
}
