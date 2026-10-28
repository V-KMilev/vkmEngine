/*
 * One surfel of the irradiance volume's atlas: the nearest surface a probe sees through one texel
 * of its cube, as the relight lights it (GLIrradianceBaker). An RGBA32UI texel:
 *   x  the colour it bounces, unorm8 rgb, and its material AO in alpha
 *   y  its normal, octahedral, two unorm16s
 *   z  its distance from the probe, negated where the probe sees its back
 *   w  the radiance it emits, RGB9E5
 * A zero distance is the sky, the atlas's clear: no surface is nearer than the capture's near plane.
 */

#include "../normal_codec.glsl"

struct Surfel {
    vec3  bounce;    // what it reflects of the light, diffuse and specular as one: a metal its f0
    float ao;
    vec3  normal;
    float distance;  // negative where the probe sees its back
    vec3  emission;
};

// RGB9E5 (EXT_texture_shared_exponent): three 9-bit mantissas over one 5-bit exponent.
const int   RGB9E5_BIAS = 15;
const float RGB9E5_MAX  = 65408.0;  // (511 / 512) * 2^16

uint packRGB9E5(vec3 c) {
    c = clamp(c, 0.0, RGB9E5_MAX);
    float largest = max(c.r, max(c.g, c.b));
    if (largest <= 0.0) return 0u;
    int   e     = max(-RGB9E5_BIAS - 1, int(floor(log2(largest)))) + 1;
    float scale = exp2(float(e - 9));
    if (floor(largest / scale + 0.5) >= 512.0) {
        scale *= 2.0;
        ++e;
    }
    uvec3 m = uvec3(floor(c / scale + 0.5));
    return m.r | (m.g << 9) | (m.b << 18) | (uint(e + RGB9E5_BIAS) << 27);
}

vec3 unpackRGB9E5(uint v) {
    uvec3 m = uvec3(v, v >> 9, v >> 18) & 511u;
    return vec3(m) * exp2(float(int(v >> 27) - RGB9E5_BIAS - 9));
}

uvec4 packSurfel(Surfel s) {
    return uvec4(
        packUnorm4x8(vec4(s.bounce, s.ao)),
        packUnorm2x16(octEncode(s.normal)),
        floatBitsToUint(s.distance),
        packRGB9E5(s.emission)
    );
}

Surfel unpackSurfel(uvec4 v) {
    Surfel s;
    vec4 bounce = unpackUnorm4x8(v.x);
    s.bounce   = bounce.rgb;
    s.ao       = bounce.a;
    s.normal   = octDecode(unpackUnorm2x16(v.y));
    s.distance = uintBitsToFloat(v.z);
    s.emission = unpackRGB9E5(v.w);
    return s;
}

bool surfelIsSky(uvec4 v) {
    return v.z == 0u;
}
