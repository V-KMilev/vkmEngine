/*
 * The ocean's water as a pixel shows it: the waves' frame the vertices left, the ripples it can
 * show (GLOceanFields), and its foam - whitecaps and waterlines.
 */

#include "bspline.glsl"
#include "ocean.glsl"

// The ocean's ripple fields (GLOceanFields), repeating, a layer each, the longest ripples in 0.
layout(binding = OCEAN_SLOT_RIPPLES) uniform sampler2DArray u_ripples;
// Metres from each point of a square of sea to whatever breaks the water (GLOceanShore), and the
// square (shoreUV); w how far it looks round a point, 0 when there is none.
layout(binding = OCEAN_SLOT_SHORE) uniform sampler2D u_oceanShoreMap;
uniform vec4 u_oceanShore;

// How rough foam is, where it covers the water.
const float FOAM_ROUGHNESS = 0.7;
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
// side, and from the sky. Shares of the scattering color, as albedo is of diffuse.
const float SCATTER_THROUGH = 6.0;
const float SCATTER_TOWARD  = 0.4;
const float SCATTER_LIT     = 0.15;
const float SCATTER_AMBIENT = 0.5;

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
    float pressed;   // how squeezed the surface is, in squeezes its crests rarely pass (oceanFoam)
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
// wherever the light falls. @p color is the water's scattering; the caller weighs it by what the
// surface lets in, one less its Fresnel.
vec3 oceanScatter(vec3 color, vec3 N, vec3 V, vec3 L, vec3 radiance, float crest) {
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

// Metres from @p point on the XZ plane to whatever breaks the water, and how much of a waterline
// the shore can say there: 1 inside its square, fading to 0 at its edges.
vec2 oceanShoreAt(vec2 point) {
    const vec2 uv   = shoreUV(point, u_oceanShore);
    const vec2 edge = min(uv, 1.0 - uv);
    const float say = clamp(min(edge.x, edge.y) / SHORE_FADE, 0.0, 1.0);
    return vec2(texture(u_oceanShoreMap, uv).r, say);
}

// GGX's alpha for water of perceptual roughness @p roughness whose ripples, too fine for the pixel,
// spread its slope by @p variance: alpha squared takes twice their variance, as Beckmann's would.
float waterAlpha(float roughness, float variance) {
    const float alpha = roughness * roughness;
    return sqrt(alpha * alpha + 2.0 * variance);
}

// Seconds a flowing water's ripples run before they start over (bodyWater), near enough: short
// enough that they never stretch far. The cycle taken is the ripples' own loop over a whole number,
// so neither jumps where the loop ends.
const float FLOW_CYCLE = 2.0;

// Water that is not the sea (MaterialAsset::water), at @p worldPos on a surface of unit normal
// @p normal whose +U runs along @p tangent: the scene's ripples on it, @p scale of them, running
// along +U at @p speed metres a second. A flow reads the fields twice, half a cycle
// apart, each faded in and out over its cycle as it runs - the flow map's two phases (Vlachos
// 2010) - so the ripples carry on downstream and never stretch.
OceanShade bodyWater(vec3 normal, vec3 tangent, vec3 worldPos, float speed, float scale) {
    vec4 ripples = vec4(0.0);
    float spanned = 0.0;
    if (u_ocean.squeeze.y > 0.0) {
        const vec3  along   = tangent - normal * dot(normal, tangent);
        const vec2  run     = dot(along.xz, along.xz) > 1e-8 ? normalize(along.xz) : vec2(0.0);
        const vec2  flow    = run * speed;
        // The ripples' loop in seconds, from the turn a second it makes, and the cycles it holds.
        const float turning = u_ocean.rippling.y;
        const float loop    = turning > 0.0 ? 2.0 * PI / turning : FLOW_CYCLE;
        const float cycles  = max(round(loop / FLOW_CYCLE), 1.0);
        const float cycle   = loop / cycles;
        const int   phases  = speed != 0.0 ? 2 : 1;
        // Where in its cycle a place is, wandering over some fifteen metres, so the cycles' joins do not
        // fall on the whole surface at once (Vlachos 2010); still in time, so the loop stays whole.
        const float wander = 0.25 * sin(0.37 * worldPos.x + 0.21 * worldPos.z)
            + 0.25 * sin(0.17 * worldPos.x - 0.43 * worldPos.z);
        float shares = 0.0;
        for (int p = 0; p < phases; ++p) {
            const float into   = fract(u_ocean.rippling.x * cycles + 0.5 * float(p) + wander);
            const float weight = phases == 1 ? 1.0 : 1.0 - abs(2.0 * into - 1.0);
            shares += weight * weight;
            const vec2  at     = worldPos.xz - flow * (into - 0.5) * cycle;
            vec4 fields[OCEAN_RIPPLE_FIELDS];
            fields[0] = smoothRipples(at * u_ocean.tiles.x, 0.0);
            fields[1] = texture(u_ripples, vec3(at * u_ocean.tiles.y, 1.0));
            fields[2] = texture(u_ripples, vec3(at * u_ocean.tiles.z, 2.0));
            ripples += weight * (fields[0] + fields[1] + fields[2]);
            for (int f = 0; f < OCEAN_RIPPLE_FIELDS; ++f) {
                spanned += weight * (fields[f].w - dot(fields[f].xy, fields[f].xy));
            }
        }
        // Two phases blended keep their slopes' spread only scaled by the weights' length: midway
        // they would halve its variance, and the ripples pulse (Heitz and Neyret 2018).
        ripples /= sqrt(max(shares, 1e-4));
    }
    OceanShade shade;
    // The ripples' slopes are the height's change along world X and Z: they tilt the surface's normal.
    shade.normal   = normalize(normal - scale * vec3(ripples.x, 0.0, ripples.y));
    shade.tangent  = tangent;
    shade.variance = 0.5 * scale * scale * max(spanned, 0.0);
    shade.pressed  = 0.0;
    return shade;
}

// The water at @p worldPos, resting at @p rest with the waves' tangents @p alongX and @p alongZ
// and lifted @p height; @p lingering is the foam a crest left there. @p foam is its whitecaps and
// waterline, 0..1.
OceanShade waterSurface(
    vec2 rest,
    vec3 alongX,
    vec3 alongZ,
    float lingering,
    float height,
    vec3 worldPos,
    out float foam
) {
    // With no ripples the fields are not summed, and their unit holds another pass's texture.
    vec4 fields[OCEAN_RIPPLE_FIELDS];
    for (int f = 0; f < OCEAN_RIPPLE_FIELDS; ++f) fields[f] = vec4(0.0);
    if (u_ocean.squeeze.y > 0.0) {
        // The longest ripples' texels are the widest on screen: those are B-spline filtered.
        fields[0] = smoothRipples(rest * u_ocean.tiles.x, 0.0);
        fields[1] = texture(u_ripples, vec3(rest * u_ocean.tiles.y, 1.0));
        fields[2] = texture(u_ripples, vec3(rest * u_ocean.tiles.z, 2.0));
    }
    OceanShade sea = oceanShadeAt(rest, alongX, alongZ, fields);
    // Foam's share of the ripples dies away with the waves, as their light does.
    for (int f = 0; f < OCEAN_RIPPLE_FIELDS; ++f) fields[f] *= oceanReach(rest);
    // Foam where a crest squeezes the water now, dense, its edge torn by the ripples; and where one
    // did a moment ago, faded to lace: thin, and torn harder.
    const float tear  = oceanTear(fields);
    const float fresh = sea.pressed + tear;
    const float aged  = lingering + FOAM_AGED_TEAR * tear;
    foam = max(oceanFoam(fresh, fwidth(fresh)), FOAM_THIN * oceanFoam(aged, fwidth(aged)));
    // A waterline: water near whatever breaks it, torn by the ripples and washed out and back by
    // the waves.
    if (u_oceanShore.w > 0.0) {
        const vec2  shore = oceanShoreAt(worldPos.xz);
        const float past  = oceanWaterline(shore.x, u_oceanShore.w, fields, oceanCrest(height));
        foam = max(foam, shore.y * oceanFroth(past, fwidth(past)));
    }
    return sea;
}
