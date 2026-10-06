/*
 * Per-pixel noise, apart from the sampling helpers' constants.
 */

// Interleaved gradient noise (Jimenez 2014), in 0..1: neighbours differ as much
// as possible, so a pattern rotated by it becomes fine grain.
float interleavedGradientNoise(vec2 p) {
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

// Two effects turned by one value draw the same grain twice, so each has its own.
const int NOISE_GTAO   = 1;
const int NOISE_DITHER = 2;

// The pattern offset by Jimenez's 5.588238-pixel step per effect, decorrelating them.
float effectNoise(vec2 p, int effect) {
    return interleavedGradientNoise(p + 5.588238 * float(effect));
}
