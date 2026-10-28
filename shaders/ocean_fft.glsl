/*
 * The inverse transform the ocean's grids share (Cooley-Tukey, radix 2): FFT_LINES lines of FFT_SIZE
 * texels in s_fft, end to end, two complex numbers a texel, a butterfly a thread for each line.
 * The including shader defines FFT_SIZE and FFT_LINES first. system/ocean/ocean_swell.cpp states the
 * same butterflies in the same order, so the swell the CPU floats things on is the one drawn.
 */

#include "constants.glsl"

shared vec4 s_fft[FFT_LINES * FFT_SIZE];

vec2 complexTimes(vec2 a, vec2 b) {
    return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

// Where line index @p i is loaded for the butterflies: its bits reversed.
uint fftShuffled(uint i) {
    return bitfieldReverse(i) >> uint(32 - findMSB(uint(FFT_SIZE)));
}

// The inverse transform of every line of s_fft, in place; @p t is this thread's butterfly. A line
// is loaded in fftShuffled order and ends in natural order.
void fftTransform(uint t) {
    for (uint span = 1u; span < uint(FFT_SIZE); span <<= 1u) {
        barrier();
        const uint  j     = t & (span - 1u);
        const uint  a     = ((t - j) << 1u) + j;
        const uint  b     = a + span;
        const float angle = PI * float(j) / float(span);
        const vec2  w     = vec2(cos(angle), sin(angle));
        for (int line = 0; line < FFT_LINES; ++line) {
            const uint at     = uint(line * FFT_SIZE);
            const vec4 low    = s_fft[at + a];
            const vec4 high   = s_fft[at + b];
            const vec4 turned = vec4(complexTimes(w, high.xy), complexTimes(w, high.zw));
            s_fft[at + a] = low + turned;
            s_fft[at + b] = low - turned;
        }
    }
    barrier();
}
