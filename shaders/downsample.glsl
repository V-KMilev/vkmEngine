/*
 * Jimenez's Call of Duty 13-tap bloom downsample: wide enough that a chain blurs smoothly, not in
 * blocks. Reads src's level 0, so the source is bound alone.
 *
 * The taps fall in five overlapping 2x2 groups. karisAverage also divides each group's weight by its
 * brightness, so one firefly cannot carry into every level below - what a chain's first level wants.
 */

#include "color.glsl"

struct DownsampleGroups {
    vec3 g[5];
};

DownsampleGroups downsampleGroups(sampler2D src, vec2 uv) {
    const vec2 texel = 1.0 / vec2(textureSize(src, 0));
    const float x = texel.x;
    const float y = texel.y;

    const vec3 a = textureLod(src, uv + vec2(-2.0 * x,  2.0 * y), 0.0).rgb;
    const vec3 b = textureLod(src, uv + vec2( 0.0,      2.0 * y), 0.0).rgb;
    const vec3 c = textureLod(src, uv + vec2( 2.0 * x,  2.0 * y), 0.0).rgb;
    const vec3 d = textureLod(src, uv + vec2(-2.0 * x,  0.0),     0.0).rgb;
    const vec3 e = textureLod(src, uv,                            0.0).rgb;
    const vec3 f = textureLod(src, uv + vec2( 2.0 * x,  0.0),     0.0).rgb;
    const vec3 g = textureLod(src, uv + vec2(-2.0 * x, -2.0 * y), 0.0).rgb;
    const vec3 h = textureLod(src, uv + vec2( 0.0,     -2.0 * y), 0.0).rgb;
    const vec3 i = textureLod(src, uv + vec2( 2.0 * x, -2.0 * y), 0.0).rgb;
    const vec3 j = textureLod(src, uv + vec2(-x,  y), 0.0).rgb;
    const vec3 k = textureLod(src, uv + vec2( x,  y), 0.0).rgb;
    const vec3 l = textureLod(src, uv + vec2(-x, -y), 0.0).rgb;
    const vec3 m = textureLod(src, uv + vec2( x, -y), 0.0).rgb;

    DownsampleGroups out_;
    out_.g[0] = (a + b + d + e) * 0.25;
    out_.g[1] = (b + c + e + f) * 0.25;
    out_.g[2] = (d + e + g + h) * 0.25;
    out_.g[3] = (e + f + h + i) * 0.25;
    out_.g[4] = (j + k + l + m) * 0.25;
    return out_;
}

// The filter's own weights: the centre group half, the four corners an eighth each.
vec3 downsample13(DownsampleGroups t) {
    return t.g[4] * 0.5 + (t.g[0] + t.g[1] + t.g[2] + t.g[3]) * 0.125;
}

// The filter's weights, each divided by its group's brightness (1 + luma).
vec3 karisAverage(DownsampleGroups t) {
    const float filterWeight[5] = float[5](0.125, 0.125, 0.125, 0.125, 0.5);
    vec3  sum    = vec3(0.0);
    float weight = 0.0;
    for (int n = 0; n < 5; ++n) {
        const float w = filterWeight[n] / (1.0 + luma(t.g[n]));
        sum    += t.g[n] * w;
        weight += w;
    }
    return sum / max(weight, 1e-4);
}
