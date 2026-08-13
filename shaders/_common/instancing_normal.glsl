/*
 * The per-instance normal matrix, for stages that shade.
 *
 * Split from instancing.glsl because the depth-only passes have no normals to
 * transform, and a storage block they declare but never bind is a hazard
 * nobody needs.
 */

layout(std430, binding = 12) readonly buffer InstanceNormals { mat4 b_normals[]; };

mat3 instanceNormalMatrix() { return mat3(b_normals[aInstance]); }
