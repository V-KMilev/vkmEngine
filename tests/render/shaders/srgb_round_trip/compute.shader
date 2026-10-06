/**
 * Every 8-bit sRGB value, decoded by the sampler and re-encoded by the shared
 * display encode; tests/render/render_tests.cpp checks each byte round-trips.
 */
layout(local_size_x = 256) in;

#include "../../../../shaders/color.glsl"

layout(binding = 0) uniform sampler2D u_bytes;  // 256 x 1, stored as sRGB

layout(std430, binding = 0) buffer Encoded {
    float value[256];
} u_encoded;

void main() {
    const int i = int(gl_LocalInvocationID.x);
    u_encoded.value[i] = linearToSrgb(texelFetch(u_bytes, ivec2(i, 0), 0).rgb).g;
}
