/**
 * Each of a row of luminances binned as the histogram bins it; tests/render/opengl/display.cpp holds every
 * one to Exposure::binOf.
 */
layout(local_size_x = 64) in;

#include "../../../../../shaders/opengl/post/exposure/bins.glsl"

layout(std430, binding = 0) buffer Binned {
    vec4 at[];  // in: x = the luminance; out: y = its bin
} u_binned;

void main() {
    const uint i = gl_GlobalInvocationID.x;
    u_binned.at[i].y = float(binOf(u_binned.at[i].x));
}
