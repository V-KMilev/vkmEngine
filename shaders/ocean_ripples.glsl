/*
 * The ocean's ripples drawn on a field's grid of wavenumbers and transformed to the water's slopes
 * and squeeze (Tessendorf 2001), for shaders/ocean/ripples and ocean/ripples_columns: every
 * wavenumber of the field's band a ripple of its own, a Gaussian of the spectrum's height there.
 * rippleDensity is OceanSpectrum::density (system/ocean/ocean_spectrum.cpp) stated again.
 *
 * A field is OCEAN_FIELD texels square, a line of it transformed by a group, a butterfly a thread
 * (ocean_fft.glsl). OCEAN_FIELD, OCEAN_GRAVITY and the JONSWAP_ constants come from the shader
 * prelude.
 */

#include "constants.glsl"
#include "ocean.glsl"

#define FFT_SIZE  OCEAN_FIELD
#define FFT_LINES 1
#include "ocean_fft.glsl"

// The wavenumber, in grid steps, that line index @p i holds: past the middle, the negative ones.
int rippleBin(int i) {
    return i < OCEAN_FIELD / 2 ? i : i - OCEAN_FIELD;
}

// Two independent standard normal numbers for grid point @p bin of field @p field, by the
// ocean's seed (PCG3D, Jarzynski and Olano 2020; Box-Muller).
vec2 rippleGaussian(ivec2 bin, int field) {
    uvec3 v = uvec3(uint(bin.x + 32768) | (uint(bin.y + 32768) << 16), uint(field), uint(u_ocean.rippling.w));
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z; v.y += v.z * v.x; v.z += v.x * v.y;
    const vec2  u      = (vec2(v.xy >> 8u) + 0.5) / 16777216.0;
    const float radius = sqrt(-2.0 * log(u.x));
    const float angle  = 2.0 * PI * u.y;
    return radius * vec2(cos(angle), sin(angle));
}

// Radians a second a ripple of wavenumber @p k rolls at, and how fast that grows with k.
vec2 rippleFrequency(float k) {
    const float depth = u_ocean.spectrum.z;
    if (depth <= 0.0) {
        const float omega = sqrt(OCEAN_GRAVITY * k);
        return vec2(omega, 0.5 * OCEAN_GRAVITY / omega);
    }
    const float t     = tanh(min(k * depth, 20.0));
    const float omega = sqrt(OCEAN_GRAVITY * k * t);
    return vec2(omega, OCEAN_GRAVITY * (t + k * depth * (1.0 - t * t)) / (2.0 * omega));
}

// Height variance a square radian a metre of wavenumber @p k holds: JONSWAP, TMA's share where
// shallow and the spread, over wavenumber.
float rippleDensity(vec2 k) {
    const float wavenumber = length(k);
    const vec2  rolls      = rippleFrequency(wavenumber);
    const float omega      = rolls.x;
    const float peak       = u_ocean.spectrum.x;
    const float sigma      = omega <= peak ? JONSWAP_SIGMA_BELOW : JONSWAP_SIGMA_ABOVE;
    const float r          = exp(-(omega - peak) * (omega - peak) / (2.0 * sigma * sigma * peak * peak));
    float energy = u_ocean.spectrum.y / pow(omega, 5.0) * exp(-1.25 * pow(peak / omega, 4.0))
                 * pow(JONSWAP_GAMMA, r);

    const float depth = u_ocean.spectrum.z;
    if (depth > 0.0) {
        const float w = omega * sqrt(depth / OCEAN_GRAVITY);
        energy *= w <= 1.0 ? 0.5 * w * w : (w < 2.0 ? 1.0 - 0.5 * (2.0 - w) * (2.0 - w) : 1.0);
    }
    const float along  = max(0.5 + 0.5 * dot(k, u_ocean.spread.xy) / wavenumber, 0.0);
    const float spread = u_ocean.spread.w * pow(along, u_ocean.spread.z);
    const float scale  = u_ocean.spectrum.w;
    return energy * spread * rolls.y / wavenumber * scale * scale;
}

// What field @p field's grid, @p step radians a metre apart, holds at grid point @p bin this
// moment: the slope across X plus i the slope across Z, then the squeeze. None outside its band.
vec4 rippleSpectrum(ivec2 bin, int field, float step) {
    const vec2  k       = vec2(bin) * step;
    const float wavenumber = length(k);
    if (wavenumber < u_ocean.bands[field] || wavenumber >= u_ocean.bands[field + 1]) return vec4(0.0);

    // The ripple rolling along k and the one rolling against it, each half a Gaussian of its
    // share: the sum with its conjugate, real, holds the whole (Tessendorf's h0).
    const vec2 ahead  = rippleGaussian(bin, field) * (0.5 * step * sqrt(rippleDensity(k)));
    const vec2 behind = rippleGaussian(-bin, field) * (0.5 * step * sqrt(rippleDensity(-k)));
    // Each rolls a whole number of turns a loop, so the phase is exact for any moment of it.
    const float turns = round(rippleFrequency(wavenumber).x / u_ocean.rippling.y);
    const float angle = 2.0 * PI * fract(turns * u_ocean.rippling.x);
    const vec2  roll  = vec2(cos(angle), sin(angle));
    const vec2  h     = complexTimes(ahead, vec2(roll.x, -roll.y))
                      + complexTimes(vec2(behind.x, -behind.y), roll);

    // Slopes are i k h; the squeeze, as a wave's, choppiness |k| h.
    const vec2 slope = vec2(-h.y * k.x - h.x * k.y, h.x * k.x - h.y * k.y);
    return vec4(slope, u_ocean.rippling.z * wavenumber * h);
}
