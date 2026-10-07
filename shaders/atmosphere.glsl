/**
 * The procedural atmosphere (Hillaire 2020): the air at an altitude, its two tables'
 * parameterisations (shaders/atmosphere/transmittance, shaders/atmosphere/multiscatter), the
 * sky-view table's (shaders/atmosphere/sky_view), and integrateAir, the march along a view ray.
 *
 * Geometry, ozone and ground are the prelude's ATMOSPHERE_*, the scattering coefficients these
 * uniforms (Atmosphere::coefficients), all from system/sky/atmosphere.h; the table sizes are the
 * prelude's too, from GLAtmosphere. Lengths are in metres; the planet's centre is the origin and
 * up is +Y.
 */

#include "constants.glsl"

uniform vec3  u_rayleighScattering;  // per metre at sea level; Rayleigh absorbs nothing
uniform vec3  u_mieScattering;
uniform vec3  u_mieExtinction;       // scattering plus absorption
uniform float u_mieG;                // Mie phase asymmetry: higher is a tighter glow round the sun

// Where the eye stands: the origin of everything seen from the scene.
const vec3 ATMOSPHERE_EYE = vec3(0.0, ATMOSPHERE_PLANET_RADIUS + ATMOSPHERE_EYE_ALTITUDE, 0.0);

// Nearest/farthest t where ray o + t*d meets a sphere of radius r at the origin; near > far
// on a miss.
vec2 raySphere(vec3 o, vec3 d, float r) {
    float b    = dot(o, d);
    float c    = dot(o, o) - r * r;
    float disc = b * b - c;
    if (disc < 0.0) return vec2(1.0, -1.0);
    disc = sqrt(disc);
    return vec2(-b - disc, -b + disc);
}

// How far along @p dir from @p p, at or above the ground, the planet is; negative when the
// ray misses it. A ray leaving the planet's centre never meets it, however rounding falls.
float distanceToGround(vec3 p, vec3 dir) {
    if (dot(p, dir) >= 0.0) return -1.0;
    vec2 t = raySphere(p, dir, ATMOSPHERE_PLANET_RADIUS);
    return t.x <= t.y ? max(t.x, 0.0) : -1.0;
}

// Relative densities at @p altitude above sea level: x Rayleigh, y Mie, z ozone.
vec3 airDensity(float altitude) {
    return vec3(
        exp(-altitude / ATMOSPHERE_RAYLEIGH_HEIGHT),
        exp(-altitude / ATMOSPHERE_MIE_HEIGHT),
        max(1.0 - abs(altitude - ATMOSPHERE_OZONE_ALTITUDE) / ATMOSPHERE_OZONE_HALF_WIDTH, 0.0)
    );
}

vec3 airScattering(vec3 density) {
    return u_rayleighScattering * density.x + u_mieScattering * density.y;
}

vec3 airExtinction(vec3 density) {
    return u_rayleighScattering * density.x + u_mieExtinction * density.y
        + ATMOSPHERE_OZONE_ABSORPTION * density.z;
}

// Rayleigh's phase for the cosine @p mu between the view and the light.
float rayleighPhase(float mu) {
    return 3.0 / (16.0 * PI) * (1.0 + mu * mu);
}

// Cornette and Shanks' Mie phase of asymmetry @p g, for the cosine @p mu.
float miePhase(float mu, float g) {
    return 3.0 / (8.0 * PI) * ((1.0 - g * g) * (1.0 + mu * mu))
        / ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * mu, 1.5));
}

// A unit coordinate to the texel centres of an @p size LUT, and back: the end texels hold the
// range's ends rather than half a texel inside them.
vec2 lutUv(vec2 unit, vec2 size) {
    return (0.5 + unit * (size - 1.0)) / size;
}

vec2 lutUnit(vec2 uv, vec2 size) {
    return (uv * size - 0.5) / (size - 1.0);
}

/**
 * Bruneton's transmittance parameterisation: radius @p r and the cosine @p mu of the view's
 * zenith angle to a unit square, spending its resolution near the horizon. Covers the rays that
 * leave the atmosphere without meeting the planet; one below the horizon reads the horizon's.
 */
vec2 transmittanceUnit(float r, float mu) {
    float top2 = ATMOSPHERE_TOP_RADIUS * ATMOSPHERE_TOP_RADIUS;
    float bot2 = ATMOSPHERE_PLANET_RADIUS * ATMOSPHERE_PLANET_RADIUS;
    float h    = sqrt(top2 - bot2);
    float rho  = sqrt(max(r * r - bot2, 0.0));
    float d    = max(-r * mu + sqrt(max(r * r * (mu * mu - 1.0) + top2, 0.0)), 0.0);
    float dMin = ATMOSPHERE_TOP_RADIUS - r;
    float dMax = rho + h;
    return clamp(vec2((d - dMin) / (dMax - dMin), rho / h), 0.0, 1.0);
}

// The inverse of transmittanceUnit: x is r, y is mu.
vec2 transmittanceParams(vec2 unit) {
    float top2 = ATMOSPHERE_TOP_RADIUS * ATMOSPHERE_TOP_RADIUS;
    float bot2 = ATMOSPHERE_PLANET_RADIUS * ATMOSPHERE_PLANET_RADIUS;
    float h    = sqrt(top2 - bot2);
    float rho  = h * unit.y;
    float r    = sqrt(rho * rho + bot2);
    float dMin = ATMOSPHERE_TOP_RADIUS - r;
    float dMax = rho + h;
    float d    = dMin + unit.x * (dMax - dMin);
    float mu   = d == 0.0 ? 1.0 : (h * h - rho * rho - d * d) / (2.0 * r * d);
    return vec2(r, clamp(mu, -1.0, 1.0));
}

// Transmittance from radius @p r toward zenith cosine @p mu to the top of the atmosphere.
vec3 transmittanceToTop(sampler2D lut, float r, float mu) {
    return textureLod(lut, lutUv(transmittanceUnit(r, mu), TRANSMITTANCE_LUT_SIZE), 0.0).rgb;
}

// Hillaire's multiple-scattering transfer Psi_ms at radius @p r under a sun at zenith cosine
// @p muSun: what every order past the first adds, per unit of scattering.
vec3 multipleScattering(sampler2D lut, float r, float muSun) {
    vec2 unit = vec2(
        muSun * 0.5 + 0.5,
        (r - ATMOSPHERE_PLANET_RADIUS) / (ATMOSPHERE_TOP_RADIUS - ATMOSPHERE_PLANET_RADIUS)
    );
    return textureLod(lut, lutUv(clamp(unit, 0.0, 1.0), MULTISCATTERING_LUT_SIZE), 0.0).rgb;
}

/**
 * The air's light toward @p origin along @p dir, out to @p tMax, per unit of the sun's
 * illuminance above the air: Rayleigh and Mie single scattering of the sunlight the
 * transmittance table says reaches each sample - none where the planet hides the sun - plus
 * every higher order from the multiple-scattering table.
 *
 * Each of @p steps steps is integrated over its length against the transmittance falling across
 * it (Hillaire's energy-conserving step), so a long one neither overshoots nor darkens.
 * @p quadratic spaces them short in the dense air near the origin and long far out, where a
 * horizon ray runs a thousand kilometres. @p throughput returns the whole path's transmittance.
 */
vec3 integrateAir(
    sampler2D transmittanceLut,
    sampler2D multiScatteringLut,
    vec3 origin,
    vec3 dir,
    float tMax,
    int steps,
    bool quadratic,
    vec3 sunDir,
    out vec3 throughput
) {
    float mu     = dot(dir, sunDir);
    float phaseR = rayleighPhase(mu);
    float phaseM = miePhase(mu, u_mieG);

    vec3  radiance = vec3(0.0);
    float tPrev    = 0.0;
    throughput = vec3(1.0);
    for (int i = 0; i < steps; ++i) {
        float f  = float(i + 1) / float(steps);
        float t  = tMax * (quadratic ? f * f : f);
        float dt = t - tPrev;
        vec3  p  = origin + dir * (tPrev + 0.3 * dt);
        tPrev = t;

        // Never under the ground: a path aimed below the eye's horizon reads sea-level air there.
        float r          = max(length(p), ATMOSPHERE_PLANET_RADIUS);
        float muSun      = dot(p, sunDir) / length(p);
        vec3  density    = airDensity(r - ATMOSPHERE_PLANET_RADIUS);
        vec3  rayleigh   = u_rayleighScattering * density.x;
        vec3  mie        = u_mieScattering * density.y;
        vec3  extinction = max(airExtinction(density), vec3(1e-30));

        vec3 sun = distanceToGround(p, sunDir) >= 0.0
            ? vec3(0.0)
            : transmittanceToTop(transmittanceLut, r, muSun);
        vec3 scattered = sun * (rayleigh * phaseR + mie * phaseM)
            + multipleScattering(multiScatteringLut, r, muSun) * (rayleigh + mie);

        vec3 stepT = exp(-extinction * dt);
        radiance   += throughput * scattered * (1.0 - stepT) / extinction;
        throughput *= stepT;
    }
    return radiance;
}

// The sky's steps from the eye to the top of the air or to the ground.
const int SKY_STEPS = 32;

/**
 * The sky's radiance along @p dir from the eye, per unit of the sun's illuminance above the air.
 *
 * A ray that meets the planet ends there and sees the ground, diffuse at ATMOSPHERE_GROUND_ALBEDO
 * in the sunlight that reaches it, through the air between: below the horizon is the ground, not
 * black, so what faces down has sky light too.
 */
vec3 skyRadiance(sampler2D transmittanceLut, sampler2D multiScatteringLut, vec3 dir, vec3 sunDir) {
    vec3  origin = ATMOSPHERE_EYE;
    float ground = distanceToGround(origin, dir);
    float tMax   = ground >= 0.0 ? ground : raySphere(origin, dir, ATMOSPHERE_TOP_RADIUS).y;

    vec3 throughput;
    vec3 radiance = integrateAir(
        transmittanceLut,
        multiScatteringLut,
        origin,
        dir,
        tMax,
        SKY_STEPS,
        true,
        sunDir,
        throughput
    );

    if (ground >= 0.0) {
        vec3  up     = normalize(origin + dir * ground);
        float cosSun = dot(up, sunDir);
        vec3  sun    = transmittanceToTop(transmittanceLut, ATMOSPHERE_PLANET_RADIUS, cosSun);
        radiance += throughput * sun * max(cosSun, 0.0) * ATMOSPHERE_GROUND_ALBEDO / PI;
    }
    return radiance;
}

// The zenith angle of the horizon seen from the eye: past a right angle, as the planet curves
// away below it.
float horizonZenithAngle() {
    float r = ATMOSPHERE_EYE.y;
    return PI - acos(sqrt(r * r - ATMOSPHERE_PLANET_RADIUS * ATMOSPHERE_PLANET_RADIUS) / r);
}

/**
 * Hillaire's sky-view parameterisation: the cosine of a view's zenith angle and the cosine of
 * its azimuth from the sun's to a unit square.
 *
 * The sky is symmetric about the sun's vertical plane, so x runs from toward the sun to away
 * from it, densest toward it. y runs from the zenith to the horizon over its first half and on to
 * the nadir over its second, densest at the horizon from either side, which keeps the ground's
 * edge sharp.
 */
vec2 skyViewUnit(float viewZenithCos, float azimuthCos) {
    float horizon = horizonZenithAngle();
    float zenith  = acos(clamp(viewZenithCos, -1.0, 1.0));
    float y = zenith < horizon
        ? 0.5 - 0.5 * sqrt(1.0 - zenith / horizon)
        : 0.5 + 0.5 * sqrt((zenith - horizon) / (PI - horizon));
    return vec2(sqrt(clamp(0.5 - 0.5 * azimuthCos, 0.0, 1.0)), y);
}

// The inverse of skyViewUnit: x is the view's zenith cosine, y its azimuth's cosine.
vec2 skyViewParams(vec2 unit) {
    float horizon = horizonZenithAngle();
    float fromMid = 2.0 * unit.y - 1.0;
    float zenith  = unit.y < 0.5
        ? horizon * (1.0 - fromMid * fromMid)
        : horizon + (PI - horizon) * fromMid * fromMid;
    return vec2(cos(zenith), 1.0 - 2.0 * unit.x * unit.x);
}
