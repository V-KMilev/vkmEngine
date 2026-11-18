/*
 * Per-pixel noise.
 */

// Interleaved gradient noise (Jimenez 2014), in 0..1: neighbors differ as much
// as possible, so a pattern rotated by it becomes fine grain.
float interleavedGradientNoise(vec2 p) {
    return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y));
}

// Two effects turned by one value draw the same grain twice, so each has its own.
const int NOISE_GTAO   = 1;
const int NOISE_DITHER = 2;
const int NOISE_SHADOW = 3;
const int NOISE_FADE   = 4;

// The pattern offset by Jimenez's 5.588238-pixel step per effect, decorrelating them.
float effectNoise(vec2 p, int effect) {
    return interleavedGradientNoise(p + 5.588238 * float(effect));
}

// Display-encoded @p c at window pixel @p p, ready for an 8-bit window: triangular noise of one
// step either way turns its banding into grain, equally strong at every level (Gjol).
vec3 displayDither(vec3 c, vec2 p) {
    float n = effectNoise(p, NOISE_DITHER) * 2.0 - 1.0;
    n = sign(n) * (1.0 - sqrt(1.0 - abs(n)));
    return clamp(c + n / 255.0, 0.0, 1.0);
}
