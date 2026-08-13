/*
 * Per-instance transforms, read through an index rather than fetched as vertex
 * attributes.
 *
 * The instance's slot in the matrix buffer arrives as a single uint attribute,
 * sourced from the index buffer the cull writes - so GL's vertex fetch performs
 * the indirection and baseInstance does the per-run offsetting, with no per-draw
 * uniform and no lookup in the shader. The matrices themselves live in storage
 * buffers, which is what lets the GPU cull settle an instance's fate by writing
 * one 4-byte index instead of copying two 64-byte matrices.
 *
 * The indirection also carries ordering, so a pass that draws a subset in its
 * own order - the shadow pass, sorted by mesh - supplies an index list and
 * skips materialising a reordered copy of the matrices.
 */

// The attribute is read from the index buffer itself, so its value IS the
// instance's slot in the transform buffers - already resolved by the vertex
// fetch, and by baseInstance for the run's offset. Looking it up again would
// be one indirection too many: harmless while the buffer is identity, and
// silently the wrong transform once the cull compacts it.
layout (location = 4) in uint aInstance;   // divisor 1; the run's slice via baseInstance

layout(std430, binding = 10) readonly buffer InstanceModels { mat4 b_models[]; };

uint instanceSlot()  { return aInstance; }
mat4 instanceModel() { return b_models[aInstance]; }
