/**
 * Each luminance binned as the histogram bins it; tests/render/render_tests.cpp checks every one
 * against Exposure::binOf.
 */
layout(local_size_x = 256) in;

#include "../../../../shaders/exposure/bins.glsl"

layout(std430, binding = 0) buffer Probe {
    float luminance[256];
    int   bin[256];
} u_probe;

void main() {
    const uint i = gl_LocalInvocationID.x;
    u_probe.bin[i] = binOf(u_probe.luminance[i]);
}
