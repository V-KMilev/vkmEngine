/**
 * GPU occlusion cull - sort the opaque batch's instances into the draws of each set
 * (GLOcclusionCull).
 *
 * An invocation per instance of the batch: its object from the batch's own instance list, its
 * command found among the commands by where its instance falls, and its world box from its
 * command's mesh's local box and its object's model matrix (Arvo's), so the CPU writes nothing
 * per instance. A set is a copy of the batch's commands with no instances, and the instances it
 * is given are written into each command's own range, counted in as they come; their order within
 * a command does not matter to an opaque draw.
 *
 * Phase 0, before the prepass, gives set 0 every instance whose object the last cull saw: what
 * the prepass draws first, and builds the pyramid from. Phase 1 tests every instance's box against
 * the pyramid; what it shows goes to set 2, the forward pass's, and what it shows that the last
 * cull did not to set 1, which the prepass draws next. Then it records what it saw. A command
 * whose box is empty (lo past hi) is always shown - a posed character, whose pose leaves its bind
 * box, and the ocean's moving water - and so is a viewmodel.
 */

layout(local_size_x = GROUP_CULL) in;

struct DrawCommand {
    uint count;
    uint instanceCount;
    uint firstIndex;
    int  baseVertex;
    uint baseInstance;
};

struct CommandBounds {
    vec4 lo;  // its mesh's local box's least corner
    vec4 hi;  // its greatest
};

layout(std430, binding = SSBO_CULL_BOUNDS) readonly buffer BoundsBlock {
    CommandBounds bounds[];  // per command of the batch
} u_bounds;

layout(std430, binding = SSBO_CULL_SOURCE) readonly buffer SourceBlock {
    uint objects[];  // the batch's instances, in its commands' order
} u_source;

layout(std430, binding = SSBO_CULL_VISIBLE) buffer VisibleBlock {
    uint seen[];  // per object, whether the last cull showed it
} u_visible;

layout(std430, binding = SSBO_CULL_COMMANDS) buffer CommandBlock {
    DrawCommand commands[];  // u_commandCount a set, the sets end to end
} u_commands;

layout(std430, binding = SSBO_CULL_INSTANCES) writeonly buffer InstanceBlock {
    uint objects[];  // u_instanceCount a set, the sets end to end
} u_instances;

layout(binding = CULL_SLOT_PYRAMID) uniform sampler2D u_pyramid;

uniform int  u_againstDepth;  // 0: copy what the last cull showed; 1: test against the pyramid
uniform int  u_instanceCount;
uniform int  u_commandCount;
uniform vec2 u_sceneSize;  // the scene's pixels, which the pyramid halves

// Window-depth slack the box's nearest point must clear, as a share of the depth there: a float
// rounds by a share of its size, and the eye's reversed depth (shaders/depth.glsl) falls as one over
// the distance, so this is the same share of the distance too - a centimetre a kilometre out.
const float DEPTH_SLACK = 1.0e-5;

#include "../../depth.glsl"   // nearerDepth, fartherDepth
#include "../../camera.glsl"

layout(std430, binding = SSBO_INSTANCE_MODELS) readonly buffer InstanceModels { mat4 b_models[]; };
layout(std430, binding = SSBO_INSTANCE_FLAGS) readonly buffer InstanceFlags { uint b_flags[]; };

// The command whose instances include instance @p i: the last whose first is at or before it.
uint commandOf(uint i) {
    uint lo = 0u;
    uint hi = uint(u_commandCount) - 1u;
    while (lo < hi) {
        const uint mid = (lo + hi + 1u) / 2u;
        if (u_commands.commands[mid].baseInstance <= i) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    return lo;
}

// Give @p object to command @p command of set @p set.
void emit(int set, uint command, uint object) {
    const uint c    = uint(set * u_commandCount) + command;
    const uint slot = atomicAdd(u_commands.commands[c].instanceCount, 1u);
    u_instances.objects[uint(set * u_instanceCount) + u_commands.commands[c].baseInstance + slot] = object;
}

// Whether the box from @p lo to @p hi lies wholly behind the pyramid's depth where it falls.
bool occluded(vec3 lo, vec3 hi) {
    vec2  minUV = vec2(1.0);
    vec2  maxUV = vec2(0.0);
    float near  = FAR_DEPTH;
    for (int i = 0; i < 8; ++i) {
        const vec3 corner = mix(lo, hi, vec3(i & 1, (i >> 1) & 1, (i >> 2) & 1));
        const vec4 clip   = u_camera.viewProjection * vec4(corner, 1.0);
        // A corner at or behind the eye: the box reaches the near plane, and is shown.
        if (clip.w <= 1e-5) return false;
        const vec3 ndc = clip.xyz / clip.w;
        minUV = min(minUV, ndc.xy * 0.5 + 0.5);
        maxUV = max(maxUV, ndc.xy * 0.5 + 0.5);
        near  = nearerDepth(near, ndc.z);
    }
    minUV = clamp(minUV, 0.0, 1.0);
    maxUV = clamp(maxUV, 0.0, 1.0);

    // The level where the box spans at most two texels a side, and the four it can touch. A level-L
    // texel covers 2^(L+1) pixels a side, the last of a level whatever the halving left over.
    const vec2  texels = u_sceneSize * 0.5;
    const vec2  extent = (maxUV - minUV) * texels;
    const int   levels = textureQueryLevels(u_pyramid);
    const int   level  = clamp(int(ceil(log2(max(max(extent.x, extent.y), 1.0)))), 0, levels - 1);
    // A level's size by GL's halving of level 0's, not textureSize at the level: llvmpipe (Mesa
    // 23.2) answered that with 1x1 at level 5 of a 160x90 pyramid.
    const ivec2 last   = max(textureSize(u_pyramid, 0) >> level, ivec2(1)) - 1;
    const vec2  scale  = texels / exp2(float(level));
    const ivec2 a      = min(ivec2(minUV * scale), last);
    const ivec2 b      = min(ivec2(maxUV * scale), last);
    const float far    = fartherDepth(
        fartherDepth(texelFetch(u_pyramid, a, level).r, texelFetch(u_pyramid, ivec2(b.x, a.y), level).r),
        fartherDepth(texelFetch(u_pyramid, ivec2(a.x, b.y), level).r, texelFetch(u_pyramid, b, level).r)
    );
    // An object seen last frame is tested against its own depth, which the rasteriser reached by
    // another product and the depth buffer rounded: without the slack a face filling its footprint
    // can hide itself, wherever the two roundings fall apart the hiding way.
    return near < far * (1.0 - DEPTH_SLACK);
}

void main() {
    const uint i = gl_GlobalInvocationID.x;
    if (i >= uint(u_instanceCount)) return;

    const uint object  = u_source.objects[i];
    const uint command = commandOf(i);
    const bool seen    = u_visible.seen[object] != 0u;

    if (u_againstDepth == 0) {
        if (seen) emit(CULL_SET_SEEN, command, object);
        return;
    }

    // The mesh's box about its centre, through the model matrix (Arvo).
    const CommandBounds local  = u_bounds.bounds[command];
    const bool          always = any(greaterThan(local.lo.xyz, local.hi.xyz))
        || (b_flags[object] & INSTANCE_FLAG_VIEWMODEL) != 0u;
    const mat4 model  = b_models[object];
    const vec3 centre = (model * vec4((local.lo.xyz + local.hi.xyz) * 0.5, 1.0)).xyz;
    const vec3 size   = (local.hi.xyz - local.lo.xyz) * 0.5;
    const vec3 reach  = abs(model[0].xyz) * size.x + abs(model[1].xyz) * size.y + abs(model[2].xyz) * size.z;
    const bool shown  = always || !occluded(centre - reach, centre + reach);
    if (shown && !seen) emit(CULL_SET_REVEALED, command, object);
    if (shown) emit(CULL_SET_SHOWN, command, object);
    u_visible.seen[object] = shown ? 1u : 0u;
}
