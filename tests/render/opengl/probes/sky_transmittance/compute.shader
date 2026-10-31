/**
 * The transmittance table read as the sky reads it, from the eye toward each of a row of sun heights;
 * tests/render/opengl/sky.cpp holds it to Atmosphere::sunTransmittance on the CPU.
 */
layout(local_size_x = 64) in;

#include "../../../../../shaders/opengl/include/atmosphere.glsl"

layout(binding = BAKE_SLOT_TRANSMITTANCE) uniform sampler2D u_transmittance;

layout(std430, binding = 0) buffer Sunlight {
    vec4 at[];  // in: x = the sine of the sun's elevation; out: rgb = the air's transmittance toward it
} u_sunlight;

void main() {
    const uint i = gl_GlobalInvocationID.x;
    const float mu = u_sunlight.at[i].x;
    u_sunlight.at[i] = vec4(transmittanceToTop(u_transmittance, length(ATMOSPHERE_EYE), mu), mu);
}
