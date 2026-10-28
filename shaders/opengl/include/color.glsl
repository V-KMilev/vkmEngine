/*
 * What the passes agree on about a linear colour.
 */

// Rec. 709 relative luminance of a linear RGB colour.
float luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Linear to sRGB: the exact IEC 61966-2-1 curve, the inverse of GL's sRGB
// decode, so an sRGB texel round-trips to its byte (a 2.2 power misses by up to
// nine steps in the darks).
vec3 linearToSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    vec3 low  = c * 12.92;
    vec3 high = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), c));
}
