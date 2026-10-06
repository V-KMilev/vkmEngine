/*
 * Each object's first bone in the frame's palette, indexed by aInstance like
 * the models.
 */
layout(std430, binding = SSBO_INSTANCE_SKIN_BASE) readonly buffer InstanceSkinBase { uint b_skinBase[]; };

uint instanceSkinBase() { return b_skinBase[aInstance]; }
