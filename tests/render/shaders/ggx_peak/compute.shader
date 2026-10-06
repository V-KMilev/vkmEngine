/**
 * The GGX lobe's peak (N.H = 1) per roughness, the first replaced by MIN_ROUGHNESS
 * and written back; tests/render/render_tests.cpp checks it against 1 / (PI alpha^2).
 */
layout(local_size_x = 4) in;

#include "../../../../shaders/brdf.glsl"

layout(std430, binding = 0) buffer Peaks {
    float roughness[4];  // perceptual, in
    float peak[4];       // D at N.H = 1, out
} u_peaks;

void main() {
    const uint  i = gl_LocalInvocationID.x;
    const float r = i == 0u ? MIN_ROUGHNESS : u_peaks.roughness[i];
    u_peaks.roughness[i] = r;
    u_peaks.peak[i]      = distributionGGX(1.0, r * r);
}
