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
- `src/engine/ecs/component/animation.h` - Animation component
- `src/engine/system/animation/skeletal_animation_system.h` - SkeletalAnimationSystem
- `src/engine/system/animation/pose_evaluator.h` - `advancePlayback` + `composePose`
- `src/engine/system/animation/pose_buffer.h` - PoseSlice, PoseWrite, PoseBuffer
- `src/engine/ecs/component/animator.h` - Animator component

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

Every field above is persisted, and blend state deliberately is not. A crossfade
is a second clip and a countdown; freezing that shape into a scene row would
outlive the blend system that wrote it, in a project with no migration path.

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
are published raw and inflated by the consumer that has the mesh in hand.

## Update model

Four phases, only the third parallel:

1. **Allocate** (serial). Walk `storage<Animator>()`, resolve each handle, drop
   any rig whose skeleton is gone, and hand out a slice per rig. Serial because
   each slice's range is a running total - and because the parallel phase must
   never touch the `ResourceManager`.
2. **Map** (serial). Stamp each rig's entity and its subtree with its slice.
3. **Evaluate** (`parallelFor`). Advance the playback head, then compose.
4. **Publish**. `ctx.poses` points at the system's own buffer.

Composition is **one forward loop with no recursion, no visited set and no
intermediate array of local transforms**:

```cpp
Transform local = skeleton.bindPose[i];       // a channel the clip lacks holds bind
if (clip) sampleBone(*clip, i, time, local);

const glm::mat4 bone = Transform::computeModelMatrix(local);
global[i]  = (parent < 0) ? bone : global[parent] * bone;
palette[i] = global[i] * skeleton.inverseBind[i];
```

That is possible only because `parent < index` is a *validated format invariant*
(enforced at import, re-checked by `AssetCook::readSkeleton`): a bone's parent is
always already composed, so its local TRS never has to outlive one iteration. It
is also where blending will attach, on `local`, before composition - blending
composed matrices skews limbs.

Time advances only when simulation time elapsed, but **composition runs every
frame regardless**, so scrubbing an `Animator` while paused shows the pose it
names. Composition is idempotent, so that costs nothing.

A clip whose per-bone table is not parallel to the rig, or whose `skeleton` names
a different rig, is refused: the bind pose stands and the mismatch is logged
once. Playing it would pose the wrong joints out of matching indices, which is a
character that moves *nearly* right.

## The frame a pose lives in

Skinned vertices resolve into the rig's model space, so the matrix that puts a
posed character in the world is the **rig entity's** world matrix:

```
worldOfBone[b] == rigWorldMatrix * poses->global()[slice->first + b]
```

Import guarantees it by putting the `Animator` on the entity whose frame the
bones are composed in - the parent of the root bone's node, or the import root
when the rig is rooted at the scene node itself. One node off and every bone is
displaced by exactly that node's transform, which looks entirely plausible until
it is compared against something.

## Seeing it

**View > Show Skeletons** draws every posed rig straight out of `ctx.poses`:
a segment from each bone to its parent, a dot at every joint, and an axis triad
per bone on the selected rig. Segments say where the joints are; only the axes
say which way they face, which is what a composition or bind-inverse mistake
actually corrupts.
