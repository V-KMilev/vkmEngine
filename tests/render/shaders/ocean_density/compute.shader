/**
 * ocean_ripples.glsl's ripple density at each of a row of wavenumbers;
 * tests/render/render_tests.cpp checks it against OceanRipples::density on the CPU.
 */
layout(local_size_x = 64) in;

#include "../../../../shaders/ocean_ripples.glsl"

layout(std430, binding = 0) buffer Density {
    vec4 at[];  // in: xy = the wavenumber; out: x = the density there
} u_density;

void main() {
    const uint i = gl_GlobalInvocationID.x;
    u_density.at[i] = vec4(rippleDensity(u_density.at[i].xy), 0.0, 0.0, 0.0);
}
