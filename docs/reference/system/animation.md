# Animation

Two systems, both in the Simulation stage, both reading `simDelta` so pause /
time-scale / single-step apply uniformly. They do not overlap:

- **`AnimationSystem`** plays authored keyframe tracks onto an entity's own
  `Transform`. One entity, one animated object.
- **`SkeletalAnimationSystem`** plays a baked clip onto a *rig* and publishes the
  resulting pose on `FrameContext::poses`. It writes no `Transform` at all,
  because a rig's bones are indices in an array rather than entities - which is
  also why the two can never contend for the same component.

## Key files

- `src/engine/system/animation/animation_system.h` - AnimationSystem
- `src/engine/system/animation/animation_track.h` - AnimationTrack<T> (keyframe storage lives here)
- `src/engine/core/math/easing.h` - easing functions (interpolation curves)
- `src/engine/ecs/component/animation/animation.h` - Animation component
- `src/engine/system/animation/skeletal_animation_system.h` - SkeletalAnimationSystem
- `src/engine/system/animation/pose_evaluator.h` - `advancePlayback` + `composePose`
- `src/engine/system/animation/pose_buffer.h` - PoseSlice, PoseWrite, PoseBuffer
- `src/engine/ecs/component/animation/animator.h` - Animator component
- `src/backend/opengl/data/gl_skin_palette.h` - GLSkinPalette (the frame's palettes, SSBO 5)
- `shaders/_common/skinning.glsl` - the vertex-stage skinning contract

# Keyframe animation

## Update model

The system makes a single pass over **every** entity with an `Animation` component
(it does not filter by visibility). For each playing animation it:

1. Advances `time` by the frame's simulation delta scaled by `speed`.
2. Handles the end of the timeline - wraps when `looping`, otherwise clamps and
   stops.
3. Samples each track and writes `Transform.position` / `.rotation` / `.scale`.

`HierarchySystem` runs later in the same frame and rebuilds every
`WorldTransform`, so an animated entity inside a hierarchy needs nothing
recorded here.

Because it applies to all animated entities (not just visible ones), off-screen
animation stays in sync; the cost is bounded by the number of *animated* entities,
not the scene size.

## Animation component

```cpp
struct Animation {
    AnimationTrack<glm::vec3> positionTrack;
    AnimationTrack<glm::quat> rotationTrack;
    AnimationTrack<glm::vec3> scaleTrack;

    float length  = 0.0f;   // explicit minimum length (0 = auto from last keyframe)
    float time    = 0.0f;   // current playback time
    float speed   = 1.0f;   // playback multiplier
    bool  playing = false;
    bool  looping = true;

    static float computeDuration(const Animation&);  // = max(each track's last keyframe, length)
};
```

The effective duration is derived on read, never stored: `computeDuration()` is
three O(1) reads and a `max`, so editing keyframes, tracks, or `length` cannot
leave anything stale. `length` lets you hold an animation open past its last
keyframe (e.g. a pause at the end of a loop); with `length == 0` the duration is
just the latest keyframe across the three tracks. Each track is independent - an
entity can animate position only, or any combination.

## AnimationTrack<T>

A time-sorted keyframe sequence with one easing function:

```cpp
AnimationTrack<glm::vec3> track;
track.setEasing(Easing::easeInOutSine);
track.addKeyframe(0.0f, {0, 0, 0});
track.addKeyframe(2.0f, {0, 5, 0});

glm::vec3 value = track.getValue(1.0f);  // eased, interpolated
```

`getValue(time)`:

1. Clamp `time` to the `[first, last]` keyframe range.
2. Binary-search the enclosing interval.
3. Compute the normalized `t` within it and apply the easing function.
4. Interpolate: `glm::slerp` for `glm::quat`, `glm::mix` otherwise - selected with
   `if constexpr (is_same_v<T, glm::quat>)`.

`addKeyframe()` inserts and keeps the sequence sorted by time.

## Keyframe storage

There is no `Keyframe<T>` struct. An `AnimationTrack<T>` stores its keyframes as
two parallel, time-sorted vectors:

```cpp
std::vector<float> m_times;   // keyframe times, ascending
std::vector<T>     m_values;  // value at each time (vec3 or quat)
```

`addKeyframe(time, value)` inserts into both at the position that keeps `m_times`
sorted, and `getValue` binary-searches `m_times` for the enclosing interval. The
parallel-array layout keeps the time lookup cache-friendly and avoids an
array-of-structs.

## Easing functions

`using EasingFunction = float(*)(float)` - takes normalized `t` in `[0, 1]`, returns
the eased value. 31 functions (`linear` plus ten In/Out/InOut families):

| Family | In | Out | InOut |
|--------|----|-----|-------|
| Linear | `linear` | - | - |
| Quadratic | `easeInQuad` | `easeOutQuad` | `easeInOutQuad` |
| Cubic | `easeInCubic` | `easeOutCubic` | `easeInOutCubic` |
| Quartic | `easeInQuart` | `easeOutQuart` | `easeInOutQuart` |
| Quintic | `easeInQuint` | `easeOutQuint` | `easeInOutQuint` |
| Sine | `easeInSine` | `easeOutSine` | `easeInOutSine` |
| Exponential | `easeInExpo` | `easeOutExpo` | `easeInOutExpo` |
| Circular | `easeInCirc` | `easeOutCirc` | `easeInOutCirc` |
| Back | `easeInBack` | `easeOutBack` | `easeInOutBack` |
| Elastic | `easeInElastic` | `easeOutElastic` | `easeInOutElastic` |
| Bounce | `easeInBounce` | `easeOutBounce` | `easeInOutBounce` |

---

# Skeletal animation

A rig is a `SkeletonAsset` and a clip is an `AnimationClipAsset` (both described
in [Resources](../resources.md)). What binds them to a character is one
`Animator`, and what comes out is a pose published for the frame.

## Animator

```cpp
struct Animator {
    SkeletonHandle      skeleton;   // the rig posed
    AnimationClipHandle clip;       // empty holds the bind pose

    float time    = 0.0f;
    float speed   = 1.0f;
    bool  playing = true;
    bool  looping = true;

    // Transient - runtime only, never serialized.
    AnimationClipHandle fadeFrom;
    float               fadeTime      = 0.0f;
    float               fadeRemaining = 0.0f;
    float               fadeDuration  = 0.0f;

    static void crossFadeTo(Animator&, AnimationClipHandle, float seconds);
};
```

**One Animator per character, not one per mesh.** Import spawns a sub-entity per
`aiMesh`, so a rigged character arrives as body plus clothes plus hair; a pose
held on the mesh would mean three clocks drifting apart, or two of the three
silently frozen in bind pose.

The repo carries no rigged model to see that in, so it carries a file that is
nothing but the case. `assets/models/multimesh_rig.gltf` is three skinned
meshes over one skin, one skeleton and one clip, each weighting a different
subset of the joints. Import it and the hierarchy shows one rig entity carrying
one `Animator`, the three meshes parented under it, and all three resolving the
same `PoseSlice`. It is authored rather than exported -
`tools/make_multimesh_rig.py` writes it, every joint a pure translation - so the
bind matrices can be checked by eye. `BrainStem.glb`, if a project has it, is
the same shape at scale: 59 skinned meshes over one 18-bone rig.

**There is no `SkinnedMesh` component.** A mesh is skinned exactly when its
`MeshAsset::skin` is non-empty - the asset already knows - and the rig driving it
is the nearest `Animator` at or above it in the `Hierarchy`, which is the
structure import produces anyway. That relationship needs no `EntityId` in any
serialized row, so prefabs, undo and scene load never have to remap it.

The first six fields are persisted, and the blend state deliberately is not. A
crossfade is a second clip and a countdown; freezing that shape into a scene row
would outlive the blend system that wrote it, in a project with no migration
path. A scene saved mid-blend reloads as the clip it was blending *to*, already
there - which is where it was going, one fade early.

## Crossfading

```cpp
Animator::crossFadeTo(scene.get<Animator>(character), runClip, 0.2f);
```

One fade, two slots. The clip being left moves into `fadeFrom` and **keeps
playing** at its own head, so a run fading into a walk does not freeze one foot
while the other keeps moving. The countdown runs in unscaled simulation seconds,
because "blend over 0.2 seconds" is a duration the caller can predict, while
`speed` is about how fast the clips themselves run.

| Call | Result |
|------|--------|
| The clip already playing | Left alone. The caller means "keep going", not "restart from zero" |
| Nothing playing, or `seconds <= 0` | A cut. There is nothing to blend out of, or no time to do it in |
| Again, mid-fade | The clip already on its way out is dropped; the blend runs from the one that was being faded *to*, which is the one still on screen |

The countdown is gated on simulation time, **not** on `playing`. A one-shot clip
shorter than the blend into it sets `playing` false partway through, and a fade
that stopped with it would hold the character at a weight no field names and
nothing clears - visibly mostly the clip it already left, with no way out but
another `crossFadeTo`. The blend is about reaching the clip, not about that clip
still advancing; it finishes, the outgoing slot clears, and the pose settles on
the target clip's last frame.

The blend happens on the **local TRS**, inside the same loop that composes, before
the parent multiply:

```cpp
local.position = glm::mix (leaving.position, local.position, weight);
local.rotation = glm::slerp(leaving.rotation, local.rotation, weight);
local.scale    = glm::mix (leaving.scale,    local.scale,    weight);
```

Blending the composed matrices instead is the mistake that looks nearly right: it
pulls a joint toward the midpoint of two *world* positions, which shortens the
limb hanging off it. A bone blended from 0 to 90 degrees sits one unit from its
root at every weight when the blend is local, and 0.707 units out halfway through
when it is not.

`weight` is `1 - fadeRemaining / fadeDuration`, and the second sample and the
three interpolations above are skipped entirely at weight 1 - which is every
frame that is not mid-fade.

## The pose, and the palette derived from it

`SkeletalAnimationSystem` publishes a `PoseBuffer` on `FrameContext::poses`, the
same way `VisibilitySystem` publishes `ctx.visibility`. It holds **two** parallel
matrix arrays, and one never overwrites the other:

| Array | What it is |
|-------|------------|
| `global()` | Each bone's transform in rig model space. This is *the pose*. |
| `palette()` | `global[b] * inverseBind[b]` - the form a vertex stage wants. |

The palette is derived from the pose, never the reverse: recovering the pose from
the palette means inverting the bind matrices per bone, and the pose is what an
attachment, a socket or a physics body reads.

Rigs are packed end to end and addressed by slice:

```cpp
struct PoseSlice {
    uint32_t  first, count;       // into global() and palette()
    glm::vec3 originMin, originMax;  // box of the posed bone origins, rig space
    float     maxBoneScale;       // largest scale any bone carries in this pose
};

const PoseSlice* slice = ctx.poses->sliceOf(entity.index);   // null = not posed
```

`sliceOf` answers for the rig entity **and every descendant of it**, stopping
wherever a nested `Animator` takes over - so a mesh entity three levels down
finds its own character's pose with one lookup.

The two bounds fields are what the *pose* knows; they are not a bounding box on
their own, because skin hangs off a bone by a distance only the mesh knows. They
are published raw and inflated by `VisibilitySystem`, which has the mesh in hand
- see [visibility.md](visibility.md#posed-bounds). That inflation is mandatory,
not polish: the occlusion cull keeps conservatively, so a box that misses the
posed skin deletes it.

## Update model

Four phases, only the third parallel:

1. **Allocate** (serial). Walk `storage<Animator>()`, resolve each handle, drop
   any rig whose skeleton is gone, and hand out a slice per rig. Serial because
   each slice's range is a running total - and because the parallel phase must
   never touch the `ResourceManager`.
2. **Map** (serial). Stamp each rig's entity and its subtree with its slice.
3. **Evaluate** (`parallelFor`). Advance the playback head - and the outgoing
   one, and the fade countdown - then compose.
4. **Publish**. `ctx.poses` points at the system's own buffer.

Composition is **one forward loop with no recursion, no visited set and no
intermediate array of local transforms**:

```cpp
Transform local = skeleton.bindPose[i];       // a channel the clip lacks holds bind
if (clip) sampleBone(*clip, i, sample.time, local);
if (blending) {                               // the crossfade, before composition
    Transform leaving = skeleton.bindPose[i];
    sampleBone(*from, i, sample.fromTime, leaving);
    local.position = glm::mix (leaving.position, local.position, weight);
    local.rotation = glm::slerp(leaving.rotation, local.rotation, weight);
    local.scale    = glm::mix (leaving.scale,    local.scale,    weight);
}

const glm::mat4 bone = Transform::computeModelMatrix(local);
global[i]  = (parent < 0) ? bone : global[parent] * bone;
palette[i] = global[i] * skeleton.inverseBind[i];
```

That is possible only because `parent < index` is a *validated format invariant*
(enforced at import, re-checked by `AssetCook::readSkeleton`): a bone's parent is
always already composed, so its local TRS never has to outlive one iteration. It
is also where blending attaches - on `local`, before composition - and where a
later layer system will attach for the same reason.

What to sample is a per-frame `PoseSample`, not the `Animator`: one clip, or two
and a weight. That is the shape a layer list replaces later, and it is per-frame,
so replacing it costs nothing that was written to a file.

Time advances only when simulation time elapsed, but **composition runs every
frame regardless**, so scrubbing an `Animator` while paused shows the pose it
names. Composition is idempotent, so that costs nothing.

A clip whose per-bone table is not parallel to the rig, or whose `skeleton` names
a different rig, is refused: the bind pose stands and the mismatch is logged
once. Playing it would pose the wrong joints out of matching indices, which is a
character that moves *nearly* right. Both clips of a fade are checked separately,
so a bad outgoing clip cannot take the incoming one down with it.

## The frame a pose lives in

Skinned vertices resolve into the rig's model space, so the matrix that puts a
posed character in the world is the **rig entity's** world matrix:

```
worldOfBone[b] == rigWorldMatrix * poses->global()[slice->first + b]
```

Import guarantees it twice over:

- the `Animator` goes on the entity whose frame the bones are composed in - the
  parent of the root bone's node, or the import root when the rig is rooted at
  the scene node itself. One node off and every bone is displaced by exactly that
  node's transform, which looks entirely plausible until it is compared against
  something;
- every **skinned mesh gets its own entity, parented to that rig entity at
  identity**. The inverse-bind matrices already carry whatever placed the mesh in
  rig space, so a mesh left under its own node would be transformed twice.

Bone nodes themselves spawn no entities. A bone is an index in the skeleton
asset, and an entity per bone would put a hundred of them per character into the
hierarchy panel, the Transform walk and the scene file for data nobody authors.
Pruning is by whole subtree - a node is dropped only when it and everything under
it is a bone with no mesh - so a prop parented to a hand keeps the chain of bones
that places it, and nothing is ever re-parented to an ancestor it did not sit
under.

Hand-authoring can still break either invariant, so the pose system names both,
once per gap:

| Fault | What it does if unnamed |
|-------|------------------------|
| A skinned mesh whose `MeshAsset::skeleton` is not the rig above it | Its bone indices address the wrong joints - a character that moves *nearly* right |
| A skinned mesh sitting off its rig's origin | Its own transform is applied on top of a palette that already resolved into rig space |

## The GPU path

Skinning is a vertex-stage difference and nothing else, expressed as two extra
programs rather than one program with a branch.

**The rig binding is a second vertex buffer**, at locations 8/9 with divisor 0,
built by `GLMesh::update` when `MeshAsset::skin` is non-empty. It is not four
more fields on `Vertex`, which stays 48 bytes: folding it in would cost every
vertex of every mesh in the engine 25% more bandwidth, paid hardest by the shadow
pass, which reads only `aPos` and replays the geometry per cascade tile and per
cube face. A rock does not pay for skinning.

Because the stream is exactly `vertices.size()` long at divisor 0, leaving 8/9
enabled costs nothing when a program that never declares them draws the same VAO.
`GLSceneCapture` and `GLPreview` do exactly that, so a character bakes into GI
and thumbnails in bind pose - the right answer for both.

**The palettes travel as one flat array.** `RenderView::skinMatrices` holds every
skinned item's palette end to end, and both `DrawableData` and `ShadowCasterData`
carry a `skinFirst` / `skinCount` range into it. Both, and not just the first:
the two lists are gathered from different sets, and a character standing just
off-screen and casting into view appears only in the caster list.

```
RenderView::build(scene, visibility, ui, poses)
  |-- buildDrawables      appends each visible entity's palette, stamps its range
  |-- buildShadowCasters  the same, for the scene-wide caster set
GLBackend::render
  |-- GLSkinPalette::update(view.skinMatrices)   one upload, SSBO binding 5
  |-- GLInstanceBatcher                           per-instance skinFirst, SSBO binding 6
```

The per-instance base is indexed by the **instance slot**, never by draw
position: the GPU occlusion cull settles an instance by rewriting that slot, so a
divisor-1 attribute would be fetched by position and hand a compacted batch
another character's bones.

**A run is skinned or it is not**, and `InstanceRun::skinned` leads the batch sort
key ahead of material and mesh, so a bucket switches program once. It takes two
things to be true - the GPU mesh carries a skin stream, and the frame posed this
instance. A skinned mesh with no rig above it fails the second, draws through the
static program, and renders the vertices it stored, which *is* its bind pose. No
per-instance branch in any vertex stage decides that.

The skinned programs are plain path-constructed shaders, so hot reload tracks
them with no new code:

| Program | Pairs with | Fragment stage |
|---------|-----------|----------------|
| `shaders/forward/pbr_skinned` | `forward/pbr` | `#include "../pbr/fragment.shader"` |
| `shaders/forward/prepass_skinned` | `forward/prepass` | `#include "../prepass/fragment.shader"` |
| `shaders/shadow/shadow_2d_skinned` | `shadow/shadow_2d` | `#include "../shadow_2d/fragment.shader"` |
| `shaders/shadow/shadow_cube_skinned` | `shadow/shadow_cube` | `#include "../shadow_cube/fragment.shader"` |

The fragment stages are includes rather than copies: a lobe added to the
ubershader can never reach only half the scene.

**The shadow pass finds its palettes differently, and is forced to.** It takes
its transforms as vertex attributes rather than out of storage, and
`gl_InstanceID` does not include `baseInstance` before GL 4.6 - so there is no
instance slot to index a base array by. The base arrives as a `u_skinBase`
uniform instead, and a uniform describes one draw, so skinned casters are drawn
one at a time. That costs N draws per cascade tile and per cube face, for skinned
casters only; an unskinned run is still one instanced draw. The palettes
themselves are the same buffer at the same binding, because the shadow pass runs
after the backend has uploaded it and before anything else reads it.

Both shadow variants reach the position through the same
`skinnedWorldPosition` the camera path uses, so a character's shadow is cast by
the geometry the camera sees rather than by something standing near it.

Both skinned vertex stages take their position from one expression,
`skinnedWorldPosition(model, base)` in `shaders/_common/skinning.glsl`. That is
what makes the depth agreement structural - the forward pass draws against the
depth the prepass primed under LEQUAL with writes off, so the two programs must
compute `gl_Position` identically, and there is only one expression for them to
compute it from.

Uniform state is per program in GL, so `GLForwardPass::bindFrameUniforms` gives
both programs the identical frame set from one place. A uniform added to only one
of them would go silently missing on characters and nowhere else.

### Two limits the vertex stage carries on purpose

**Normals are exact under uniform bone scale, approximate under non-uniform.**
The skinned stages compute `instanceNormalMatrix() * (mat3(skinMatrix) * aNormal)`
- the inverse-transpose covers the *model* matrix, and the skin matrix reaches
the normal directly. A bone scaled evenly only changes the vector's length, which
the following `normalize` absorbs, so the common case is exact. A bone scaled
unevenly tilts the normal off the posed surface, and the lighting is wrong by
that much. Inverse-transposing the skin matrix means a 3x3 inverse per vertex in
four programs on a frame that is already GPU-bound, to correct a case rigs
essentially never author. Clips keep their scale tracks, and this is the price.

**A bone index is bounds-checked at cook time, not in the shader.** `readMesh`
refuses any index past `MAX_SKELETON_BONES`, but the shader reads
`b_skin[base + aBones.x]` with no clamp against the rig's own bone count. That is
sound for a mesh under the rig it was skinned to, which is the only arrangement
import produces. Move a skinned mesh under a *different* rig and its indices
address that rig's slice, or run off the end of the palette entirely - the same
misuse `SkeletalAnimationSystem` already names in the log ("its bone indices
address the wrong joints"). It reads garbage bones, not memory outside the
buffer's allocation; the fix is the warning, not a per-vertex clamp.

## Seeing it

**View > Show Skeletons** draws every posed rig straight out of `ctx.poses`:
a segment from each bone to its parent, a dot at every joint, and an axis triad
per bone on the selected rig. Segments say where the joints are; only the axes
say which way they face, which is what a composition or bind-inverse mistake
actually corrupts.

The **Animator card** in the inspector authors the same thing: a rig picker, a
clip picker, and a transport whose scrub works while paused, for the same reason
the overlay does - composition runs every frame whether or not time advanced. A
clip cooked against another rig is named on the card, beside the pickers that
made the pairing. See [editor.md](../editor.md).
