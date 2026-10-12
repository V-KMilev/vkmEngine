/**
 * ocean_ripples.glsl's line transform of two single frequencies, one in each half of a line;
 * tests/render/render_tests.cpp checks each against its plane wave.
 */
layout(local_size_x = GROUP_OCEAN_FFT) in;

#include "../../../../shaders/ocean_ripples.glsl"

layout(std430, binding = 0) buffer Line {
    vec4 texel[];  // out: xy = the first frequency's wave, zw = the second's
} u_line;

uniform ivec2 u_bins;  // the two frequencies, as line indices

void main() {
    const uint t = gl_LocalInvocationID.x;
    for (uint i = t; i < uint(OCEAN_FIELD); i += uint(GROUP_OCEAN_FFT)) {
        const vec2 first  = int(i) == u_bins.x ? vec2(1.0, 0.0) : vec2(0.0);
        const vec2 second = int(i) == u_bins.y ? vec2(1.0, 0.0) : vec2(0.0);
        s_line[rippleShuffled(i)] = vec4(first, second);
    }
    rippleTransform(t);
    for (uint i = t; i < uint(OCEAN_FIELD); i += uint(GROUP_OCEAN_FFT)) u_line.texel[i] = s_line[i];
}
