/**
 * GPU occlusion cull: reject instances hidden behind the frame's depth, and
 * compact the survivors into the buffers the draw reads.
 *
 * One invocation per instance. An instance's world AABB is projected, its
 * screen footprint looked up in the Hi-Z pyramid, and its nearest point
 * compared against the farthest occluder over that footprint. Survivors append
 * themselves into their own run's slice of the output, and bump the instance
 * count of that run's indirect draw command - so nothing ever comes back to the
 * CPU, and the draw call count is unchanged.
 *
 * Compaction is per run and in place. A run's input occupies [first, first +
 * capacity) and its output can only be smaller, so survivors are written into
 * that same range and baseInstance stays what the CPU already knew. No prefix
 * sum, no allocation.
 *
 * What gets written is the instance's *index*, not its transforms. The vertex
 * stage reads the matrices through that index, so surviving an instance costs
 * one 4-byte store rather than copying two 64-byte matrices - and the cost of
 * the pass stops scaling with how much survives.
 *
 * Conservative in one direction, like every cull: anything undecidable is kept.
 * A box crossing the near plane, or one whose footprint leaves the pyramid, is
 * drawn. Under-culling costs a draw; over-culling deletes visible geometry.
 */

layout(local_size_x = 64) in;

struct DrawCommand {
    uint count;          // indices per instance
    uint instanceCount;  // written here, read by glDrawElementsIndirect
    uint firstIndex;
    uint baseVertex;
    uint baseInstance;
};

layout(std430, binding = 3) readonly  buffer Bounds   { vec4 b_bounds[]; };    // pairs: min.xyz, max.xyz
layout(std430, binding = 4) readonly  buffer RunIndex { uint b_runIndex[]; };
layout(std430, binding = 7) writeonly buffer Visible  { uint b_visible[]; };
layout(std430, binding = 9)           buffer Commands { DrawCommand b_commands[]; };

uniform mat4  u_viewProjection;
uniform uint  u_instanceCount;
uniform vec2  u_hizSize;      ///< Level-0 dimensions of the pyramid, in texels.
uniform float u_hizMaxLod;

uniform sampler2D u_hiz;

/**
 * Screen-space extent of a world AABB, plus how near it comes.
 *
 * Returns false when the box cannot be tested safely: behind or across the near
 * plane, where the projection of a corner means nothing.
 */
bool projectBounds(vec3 lo, vec3 hi, out vec2 uvMin, out vec2 uvMax, out float nearestDepth) {
    uvMin = vec2( 1e30);
    uvMax = vec2(-1e30);
    nearestDepth = 1e30;

    for (int corner = 0; corner < 8; ++corner) {
        const vec3 p = vec3(
            (corner & 1) != 0 ? hi.x : lo.x,
            (corner & 2) != 0 ? hi.y : lo.y,
            (corner & 4) != 0 ? hi.z : lo.z);

        const vec4 clip = u_viewProjection * vec4(p, 1.0);

        // z + w < 0 is in front of the near plane. The hardware clips that away,
        // so its projection describes nothing the depth buffer knows about.
        if (clip.z + clip.w <= 0.0) return false;

        const vec3 ndc = clip.xyz / clip.w;
        uvMin = min(uvMin, ndc.xy * 0.5 + 0.5);
        uvMax = max(uvMax, ndc.xy * 0.5 + 0.5);
        nearestDepth = min(nearestDepth, ndc.z * 0.5 + 0.5);
    }
    return true;
}

bool occluded(vec3 lo, vec3 hi) {
    vec2  uvMin, uvMax;
    float nearestDepth;
    if (!projectBounds(lo, hi, uvMin, uvMax, nearestDepth)) return false;

    // Part of the footprint fell outside the pyramid, so part of it was never
    // rasterized and nothing can be concluded about it.
    if (any(lessThan(uvMin, vec2(0.0))) || any(greaterThan(uvMax, vec2(1.0)))) return false;

    // The level where the footprint spans at most two texels, so four taps
    // cover it whatever its alignment.
    const vec2  extent = (uvMax - uvMin) * u_hizSize;
    const float lod    = clamp(ceil(log2(max(max(extent.x, extent.y), 1.0))), 0.0, u_hizMaxLod);

    // Farthest occluder anywhere the box covers.
    float farthest = textureLod(u_hiz, vec2(uvMin.x, uvMin.y), lod).r;
    farthest = max(farthest, textureLod(u_hiz, vec2(uvMax.x, uvMin.y), lod).r);
    farthest = max(farthest, textureLod(u_hiz, vec2(uvMin.x, uvMax.y), lod).r);
    farthest = max(farthest, textureLod(u_hiz, vec2(uvMax.x, uvMax.y), lod).r);

    // Every pixel it covers already has something in front of its closest point.
    return nearestDepth > farthest;
}

void main() {
    const uint index = gl_GlobalInvocationID.x;
    if (index >= u_instanceCount) return;

    const vec3 lo = b_bounds[index * 2u + 0u].xyz;
    const vec3 hi = b_bounds[index * 2u + 1u].xyz;

    if (occluded(lo, hi)) return;

    const uint run  = b_runIndex[index];
    const uint slot = b_commands[run].baseInstance + atomicAdd(b_commands[run].instanceCount, 1u);

    b_visible[slot] = index;
}
