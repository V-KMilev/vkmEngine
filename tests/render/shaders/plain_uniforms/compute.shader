/**
 * Writes back a uniform of each kind ShaderBase::zeroUniforms sets, and reads a sampler it
 * leaves; tests/render/render_tests.cpp checks they all come back zero but the sampler's unit.
 */
layout(local_size_x = 1) in;

uniform float u_scalar;
uniform ivec2 u_pair;
uniform uint  u_counts[3];
uniform mat4  u_matrix;
uniform bool  u_flag;
uniform sampler2D u_map;

layout(std430, binding = 0) buffer Seen {
    float values[9];
} u_seen;

void main() {
    u_seen.values[0] = u_scalar;
    u_seen.values[1] = float(u_pair.x);
    u_seen.values[2] = float(u_pair.y);
    u_seen.values[3] = float(u_counts[0]);
    u_seen.values[4] = float(u_counts[2]);
    u_seen.values[5] = u_matrix[0][0];
    u_seen.values[6] = u_matrix[3][3];
    u_seen.values[7] = u_flag ? 1.0 : 0.0;
    u_seen.values[8] = float(textureSize(u_map, 0).x);  // keeps the sampler active
}
