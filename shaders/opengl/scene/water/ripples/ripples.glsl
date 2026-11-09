/*
 * The ripples drawn on a field's grid of wavenumbers and transformed to the water's slopes
 * and squeeze (Tessendorf 2001), for shaders/opengl/scene/water/ripples and water/ripples/columns: every
 * wavenumber of the field's band a ripple of its own, a Gaussian of the spectrum's height there.
 * rippleDensity is WaveSpectrum::density (ecs/water/wave_spectrum.cpp) stated again.
 *
 * A field is RIPPLE_FIELD texels square, a line of it transformed by a group, a butterfly a thread
 * (wave_fft.glsl). RIPPLE_FIELD, WAVE_GRAVITY and the JONSWAP_ constants come from the shader
 * prelude.
 */

#include "../../../include/constants.glsl"
#include "../../../include/waves.glsl"

#define FFT_SIZE  RIPPLE_FIELD
#define FFT_LINES 1
#include "../wave_fft.glsl"

// Two independent standard normal numbers for grid point @p bin of field @p field, by the
// waves' seed (PCG3D, Jarzynski and Olano 2020; Box-Muller).
vec2 rippleGaussian(ivec2 bin, int field) {
    uvec3 v = uvec3(uint(bin.x + 32768) | (uint(bin.y + 32768) << 16), uint(field), uint(u_waves.rippling.w));
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
    const float depth = u_waves.spectrum.z;
    if (depth <= 0.0) {
        const float omega = sqrt(WAVE_GRAVITY * k);
        return vec2(omega, 0.5 * WAVE_GRAVITY / omega);
    }
    const float t     = tanh(min(k * depth, 20.0));
    const float omega = sqrt(WAVE_GRAVITY * k * t);
    return vec2(omega, WAVE_GRAVITY * (t + k * depth * (1.0 - t * t)) / (2.0 * omega));
}

// Height variance a square radian a meter of wavenumber @p k holds: JONSWAP, TMA's share where
// shallow and the spread, over wavenumber.
float rippleDensity(vec2 k) {
    const float wavenumber = length(k);
    const vec2  rolls      = rippleFrequency(wavenumber);
    const float omega      = rolls.x;
    const float peak       = u_waves.spectrum.x;
    const float sigma      = omega <= peak ? JONSWAP_SIGMA_BELOW : JONSWAP_SIGMA_ABOVE;
    const float r          = exp(-(omega - peak) * (omega - peak) / (2.0 * sigma * sigma * peak * peak));
    float energy = u_waves.spectrum.y / pow(omega, 5.0) * exp(-1.25 * pow(peak / omega, 4.0))
                 * pow(JONSWAP_GAMMA, r);

    const float depth = u_waves.spectrum.z;
    if (depth > 0.0) {
        const float w = omega * sqrt(depth / WAVE_GRAVITY);
        energy *= w <= 1.0 ? 0.5 * w * w : (w < 2.0 ? 1.0 - 0.5 * (2.0 - w) * (2.0 - w) : 1.0);
    }
    const float along  = max(0.5 + 0.5 * dot(k, u_waves.spread.xy) / wavenumber, 0.0);
    const float spread = u_waves.spread.w * pow(along, u_waves.spread.z);
    const float scale  = u_waves.spectrum.w;
    return energy * spread * rolls.y / wavenumber * scale * scale;
}

// What field @p field's grid, @p step radians a meter apart, holds at grid point @p bin this
// moment: the slope across X plus i the slope across Z, then the squeeze. None outside its band.
vec4 rippleSpectrum(ivec2 bin, int field, float step) {
    const vec2  k       = vec2(bin) * step;
    const float wavenumber = length(k);
    if (wavenumber < u_waves.bands[field] || wavenumber >= u_waves.bands[field + 1]) return vec4(0.0);

    // The ripple rolling along k and the one rolling against it, each half a Gaussian of its
    // share: the sum with its conjugate, real, holds the whole (Tessendorf's h0).
    const vec2 ahead  = rippleGaussian(bin, field) * (0.5 * step * sqrt(rippleDensity(k)));
    const vec2 behind = rippleGaussian(-bin, field) * (0.5 * step * sqrt(rippleDensity(-k)));
    // Each rolls a whole number of turns a loop, so the phase is exact for any moment of it.
    const float turns = round(rippleFrequency(wavenumber).x / u_waves.rippling.y);
    const float angle = 2.0 * PI * fract(turns * u_waves.rippling.x);
    const vec2  roll  = vec2(cos(angle), sin(angle));
    const vec2  h     = complexTimes(ahead, vec2(roll.x, -roll.y))
                      + complexTimes(vec2(behind.x, -behind.y), roll);

    // Slopes are i k h; the squeeze, as a wave's, choppiness |k| h.
    const vec2 slope = vec2(-h.y * k.x - h.x * k.y, h.x * k.x - h.y * k.y);
    return vec4(slope, u_waves.rippling.z * wavenumber * h);
}
