/*
 * The ocean's swell drawn on its cascades' grids at the frame's moment and transformed to the
 * water's movement (Tessendorf 2001), for shaders/ocean/swell and ocean/swell_columns: SwellMoment's
 * statement (system/ocean/ocean_swell.cpp), term for term, so the swell gameplay floats things on is
 * the one drawn. Each cascade's spectrum is the CPU's (OceanSwell), its grid SWELL_FIELD square,
 * a line of it transformed by a group, a butterfly a thread; a texel holds two lines' worth.
 */

#define OCEAN_SWELL_ACCESS
#include "ocean.glsl"

#define FFT_SIZE  SWELL_FIELD
#define FFT_LINES 2
#include "ocean_fft.glsl"

// Each cascade's spectrum, a texel a grid point: xy = Tessendorf's h0, z = whole turns a loop.
layout(std430, binding = SSBO_OCEAN_SWELL_SPECTRUM) readonly buffer OceanSwellSpectrum {
    vec4 b_spectrum[];
};

// The wavenumber, in grid steps, that index @p i holds: past the middle, the negative ones.
int swellBin(int i) {
    return i < SWELL_FIELD / 2 ? i : i - SWELL_FIELD;
}

// The index holding the wavenumber opposite @p i's.
int swellMirror(int i) {
    return (SWELL_FIELD - i) % SWELL_FIELD;
}

// @p a times i.
vec2 swellTurned(vec2 a) {
    return vec2(-a.y, a.x);
}

// Two real fields' spectra as one complex one: @p first + i @p second.
vec2 swellPacked(vec2 first, vec2 second) {
    return first + swellTurned(second);
}
