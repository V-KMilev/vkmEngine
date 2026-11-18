/*
 * The swell drawn on its cascades' grids at the frame's moment and transformed to the water's movement
 * (Tessendorf 2001), for shaders/opengl/scene/water/swell and water/swell/columns: SwellMoment's statement
 * (ecs/water/swell.cpp), term for term, so the swell gameplay floats things on is the one drawn. Each
 * cascade's spectrum is the CPU's (Swell), its grid SWELL_FIELD square, a line of it transformed by a group,
 * a butterfly a thread; a texel holds two lines' worth.
 */

#define SWELL_ACCESS
#include "../../../include/waves.glsl"

#define FFT_SIZE  SWELL_FIELD
#define FFT_LINES 2
#include "../wave_fft.glsl"

// Each cascade's spectrum, a texel a grid point: xy = Tessendorf's h0, z = whole turns a loop.
layout(std430, binding = SSBO_SWELL_SPECTRUM) readonly buffer SwellSpectrum {
    vec4 b_spectrum[];
};

// @p a times i.
vec2 swellTurned(vec2 a) {
    return vec2(-a.y, a.x);
}

// Two real fields' spectra as one complex one: @p first + i @p second.
vec2 swellPacked(vec2 first, vec2 second) {
    return first + swellTurned(second);
}
