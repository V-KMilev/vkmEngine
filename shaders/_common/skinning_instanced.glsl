/*
 * Where an instance of the camera batch finds its own bones.
 *
 * Split from skinning.glsl for the same reason instancing_normal.glsl is split
 * from instancing.glsl: the shadow pass draws skinned casters one at a time and
 * takes its base from a uniform, so it never binds this block.
 *
 * Indexed by aInstance - the slot the vertex fetch already resolved - and never
 * by draw position. The GPU occlusion cull settles an instance by rewriting that
 * slot, so a per-instance attribute at divisor 1 would be fetched by position
 * and hand a compacted batch another character's bones.
 */

layout(std430, binding = 6) readonly buffer InstanceSkinBase { uint b_skinBase[]; };

uint instanceSkinBase() { return b_skinBase[aInstance]; }
