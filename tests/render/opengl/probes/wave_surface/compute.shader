/**
 * Where waves.glsl moves the water resting at a point, by u_share of the waves, and the surface's normal
 * there; tests/render/opengl/water.cpp holds both to WaveSurface on the CPU. A point takes two rows: its
 * rest point in the first, which then holds how far its water moved, and its normal in the second.
 */
layout(local_size_x = 64) in;

#include "../../../../../shaders/opengl/include/waves.glsl"

layout(std430, binding = 0) buffer Rows {
    vec4 at[];
} u_rows;

uniform float u_share;  // of the waves

void main() {
    const uint i = gl_GlobalInvocationID.x;
    if (i % 2u != 0u) return;
    const WavePoint water = wavesAt(u_rows.at[i].xy, u_share);
    u_rows.at[i]      = vec4(water.offset, 1.0);
    u_rows.at[i + 1u] = vec4(normalize(cross(water.alongZ, water.alongX)), 0.0);
}
