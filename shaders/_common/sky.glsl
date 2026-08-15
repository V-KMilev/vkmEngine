/**
 * Shared night-sky pieces.
 *
 * Two paths need the same answer to "how much night is it": the IBL bake, which
 * decides how the scene is lit, and the skybox, which decides what it looks
 * like. One definition so they cannot drift - the same reason depth.glsl holds
 * the slice mapping for the cull and shade passes.
 */

// Twilight band, in sine-of-elevation. Roughly +/- 6 degrees around the horizon:
// wide enough that the handover reads as dusk, narrow enough that a sun clearly
// up or clearly down is unambiguous.
const float SKY_TWILIGHT = 0.10;

/**
 * How far into night the sun's elevation puts us: 0 in daylight, 1 once the sun
 * is well below the horizon.
 *
 * Cross-faded rather than switched. A hard cutover at exactly zero pops, and the
 * atmosphere is still doing real work either side of it - that band is twilight,
 * not a boundary.
 */
float skyNightFactor(vec3 sunDir) {
    return smoothstep(SKY_TWILIGHT, -SKY_TWILIGHT, sunDir.y);
}

// hash33 (Dave Hoskins): three decorrelated values in [0,1) from a cell index.
vec3 skyHash33(vec3 c) {
    vec3 p = fract(c * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx);
}

/**
 * Procedural star field along @p dir.
 *
 * Direction space is diced into cells and a hash decides which hold a star and
 * where inside it, so @p density is a dial rather than an accident of the grid
 * spacing. Procedural rather than a texture: the sky is then identical on every
 * machine, the same reason the atmosphere itself is computed rather than loaded.
 *
 * Returns radiance to add, already varied in brightness so the field does not
 * read as a uniform stipple.
 */
float skyStarField(vec3 dir, float density) {
    const float FILL  = 0.055;  // fraction of cells holding a star
    // Gaussian falloff. Sized so a star covers roughly a pixel and a half at
    // 1080p rather than a fraction of one: sub-pixel points are missed by
    // sampling outright and shimmer as the camera turns, which reads as noise
    // rather than as sky. Bloom widens what survives.
    const float SHARP = 100.0;

    vec3 p    = dir * density;
    vec3 cell = floor(p);
    vec3 h    = skyHash33(cell);

    // One decorrelated draw decides both whether this cell has a star and how
    // bright it is, so bright stars are not clustered by construction.
    float gate = fract(h.x * 137.0 + h.y * 71.0 + h.z * 29.0);
    if (gate > FILL) return 0.0;

    float d = length(p - (cell + h));
    float brightness = 0.2 + 0.8 * fract(gate * 97.0);
    return exp(-d * d * SHARP) * brightness;
}

/**
 * Soft-edged disc of angular radius @p cosOuter..@p cosInner around @p axis.
 *
 * Shared by the sun and the moon: the same limb treatment, so neither reads as
 * a harder cut-out than the other.
 */
float skyDisc(vec3 dir, vec3 axis, float cosOuter, float cosInner) {
    return smoothstep(cosOuter, cosInner, dot(dir, axis));
}
