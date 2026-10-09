/**
 * Where ocean.glsl moves the water resting at each of a row of points, and its normal there;
 * tests/render/render_tests.cpp checks it against OceanSurface on the CPU.
 */
layout(local_size_x = 64) in;

#include "../../../../shaders/ocean.glsl"

layout(std430, binding = 0) buffer Moved {
    vec4 point[];  // in: xy = the rest point; out: xyz = where the water there went, w = 1
                   // where the block says there is an ocean
} u_moved;

layout(std430, binding = 1) buffer Normals {
    vec4 normal[];  // out: xyz = the surface's unit normal there
} u_normals;

void main() {
    const uint i = gl_GlobalInvocationID.x;
    const OceanPoint water = oceanAt(u_moved.point[i].xy);
    u_moved.point[i]   = vec4(water.offset, oceanPresent() ? 1.0 : 0.0);
    u_normals.normal[i] = vec4(normalize(cross(water.alongZ, water.alongX)), 0.0);
}
