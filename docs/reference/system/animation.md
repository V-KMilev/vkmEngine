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
- `src/engine/ecs/component/animation/bone_socket.h` - BoneSocket component
- `src/engine/system/animation/bone_socket_system.h` - BoneSocketSystem
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
| A skinned mesh **below** the rig sitting off its origin | Its own transform is applied on top of a palette that already resolved into rig space |

The second fault is a question about descendants only, and the emphasis is the
whole of it. The **rig entity's** own transform is not a second matrix stacked on
the palette - it *is* the matrix the palette is multiplied by, and it is what
puts the character somewhere other than the world origin. A rig can carry a
skinned mesh itself (a one-mesh file whose rig is rooted at the scene node), so
it is walked like any other entity; testing its transform for identity there
would report every placed character in the scene. The rig-name half of the check
still applies to it, because that one is about the mesh, not about where it
stands.

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
skinned item's palette end to end, and each item carries a `first` / `count`
range into it. Both lists get one, not just the first: the two are gathered from
different sets, and a character standing just off-screen and casting into view
appears only in the caster list.

A drawable carries its range inline (`DrawableData::skinFirst` / `skinCount`)
because the backend partitions drawables into buckets of *pointers* and each one
has to be self-describing. A caster's range rides alongside instead, in
`RenderView::casterSkins`, index for index with `shadowCasters` - the shadow cull
addresses casters by index anyway, and `ShadowCasterData` stays 96 bytes so the
cull, which reads every caster's bounds once per atlas tile and once per cube
face, is not widened by two fields it never looks at. Same reason `Vertex` stays
48 bytes.

```
RenderView::build(scene, visibility, ui, poses)
  |-- buildDrawables      appends each visible entity's palette, stamps its range
  |-- buildShadowCasters  the same, into the parallel casterSkins array
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

**An empty palette switches the whole apparatus off.** A frame that posed nothing
leaves `skinMatrices` empty, and every stage keys off that one fact rather than
discovering it per item:

| Stage | With no palette |
|-------|-----------------|
| `RenderView::build` | resolves the pose pointer to null once; neither gather looks an entity up |
| `GLInstanceBatcher` | no mesh resolve per drawable, no per-instance palette base, no upload, no binding, and the narrow sort key |
| `GLDepthPrepass` / `GLForwardPass` | the skinned program is not bound and not given the frame's uniforms |
| `GLShadowPass` | the skinned programs are never bound, and every run is one instanced draw |

That is the same rule the vertex format follows, applied to the frame rather than
to the mesh: a scene with no characters pays nothing for the machinery that draws
them. It is worth 0.42 ms a frame on `examples/stress_arena`, which has none.

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

`GLShadowPass` also tracks which program is current across the whole pass. A
program has to be bound to be given a uniform, and the pass hands matrices to one
per atlas tile and per cube face; binding per tile makes `glUseProgram` the
largest thing it does when only one program is ever used.

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

## Bone sockets

`PoseBuffer::global()` exists so that something other than the renderer can read
a pose, and a socket is the first thing that does. A weapon in a hand, a hat on
a head, a muzzle flash at a gun tip: one component, one system, and no new
channel between them.

```cpp
struct BoneSocket {
    std::string bone;    // "hand.R"
    Transform   offset;  // placement on that joint, in bone space

    // Transient - runtime only, never serialized.
    SkeletonHandle resolvedRig;
    std::string    resolvedName;
    int32_t        boneIndex = -1;
};
```

**The socket is the attached entity**, not a marker something else hangs off. A
marker would be a second entity per attachment carrying a Transform nobody
authors, listed in the panel and written to the scene file - the cost an entity
per bone was refused for, charged per attachment instead - and the thing being
held would sit one level below it for no information gained. So the gun carries
the `BoneSocket`, and a muzzle flash parented under the gun is just a child.

**The bone is named, never indexed.** An index is what the pose arrays are
addressed by and it is a lookup cheaper, but it describes one export of one rig:
re-export a character with a joint inserted and every stored index silently
addresses its neighbour - a weapon on the elbow, and no error anywhere. The name
is the joint's durable identity, which is already what a clip binds by at cook
time. It is the same call `PrefabEntity` makes for the same reason: an authored
reference has to survive the thing it points into being rebuilt.

The lookup is linear, and `SkeletonAsset::indexOf` says it is not a per-frame
call, so the answer is memoised on the component against both halves of the
pairing - the rig handle and the name. Change either and it re-resolves on the
next frame, which is what makes retargeting a socket in the inspector immediate
rather than a reload away. Failure is memoised too: a name the rig does not
carry resolves to -1 once instead of rescanning a hundred bones every frame to
fail again.

### The parent is the rig, and has to be

A socket must be a **direct child of the entity carrying the `Animator`** - the
same arrangement import already produces for skinned meshes. That is not a
convenience, it is what the ordering below buys:

```
worldOfSocket == rigWorldMatrix * poses->global()[slice->first + bone] * offset
```

`BoneSocketSystem` writes the socket's **local** `Transform` as
`global[bone] * offset` and lets `HierarchySystem` supply the `rigWorldMatrix`
half by its usual `parentWorld * local`. That only lands on the bone when the
parent's world matrix is the frame the pose was composed in, which is the rig's.

What the hierarchy reads is a TRS, not a matrix, so the product goes back
through `Transform::fromModelMatrix` - the inverse of the `computeModelMatrix`
beside it, added for this. It is exact for a chain of translations, rotations and
uniform scales, which is what a rig composes, and it carries a mirrored basis on
one scale axis rather than handing `quat_cast` a reflection to read as a
rotation. Shear is not representable as a TRS at all and is dropped; it appears
only when a non-uniformly scaled joint carries a rotated child, the same case the
skinned vertex stage already approximates the lighting of.

An axis scaled to **nothing** is answered rather than refused. A clip that hides
a joint by keying its scale to zero produces exactly that, and so does an offset
scale dragged through zero in the inspector; dividing the basis by it would hand
`quat_cast` a NaN, and the socket would then write a NaN `Transform` that spreads
through every world matrix under it. The scale comes back as zero and the
rotation is read from the axes that survived - the lost one is the axis those two
imply, and with two of them gone there is no rotation left to recover and the
identity's column stands in.

### Which stage, and why the frame it runs in matters

`SystemStage::Transform`, registered **ahead of `HierarchySystem`**. A socket is a
derived transform and that is the stage derived transforms belong to; both
neighbours in the ordering are load-bearing:

| Boundary | What it buys |
|----------|--------------|
| After the Simulation stage | `ctx.poses` is a per-frame product. Reading it from Simulation would depend on registration order inside a stage; reading it from Transform cannot. |
| Before the world resolve | The socket is on its bone the frame the character moves, including the **first** frame, when there is no previous frame to have cached anything. |

Get the second one wrong and the failure is invisible in a screenshot. The
obvious alternative - read the rig's `WorldTransform`, write the socket's - reads
a matrix `HierarchySystem` last wrote *a frame ago*, so the attachment trails the
character by exactly one frame while it runs and is exactly right whenever it
stands still. It also strands anything parented **under** the socket, because
that walk has already happened by the time such a system would run: the muzzle
flash would resolve against last frame's gun.

Placement is unconditional rather than gated on simulation time, for the reason
composition is: scrubbing an `Animator` while paused has to move what the
character is holding, or a paused preview shows a pose its props disagree with.

Because the `Transform` is an output, it is rewritten every frame - authoring one
on the socket entity does nothing. The offset is the authored half, and the
inspector says so on both cards.

### The four ways a socket has nothing to stand on

Every one of them is silent on screen - an unplaced socket simply stays where it
last was, which for a fresh one is the world origin and for a moved one is a
plausible-looking lie. So each is named in the log, edge-latched like the pose
system's own faults, so it is reported once per gap rather than once a frame.

| Fault | What is said |
|-------|--------------|
| No pose published at all | Named once for the whole scene, not per socket: this is `BoneSocketSystem` running without `SkeletalAnimationSystem` ahead of it, which is a wiring mistake |
| Not a direct child of a rig | The socket hangs off the entity carrying the `Animator`, not off a mesh under it |
| The rig above it posed nothing | Its `Animator` names no live skeleton |
| The rig has no bone of that name | Including the empty name a freshly added component starts with |

### What it costs a scene with no sockets

One null check on `storage<BoneSocket>()`. No pose lookup, no hierarchy walk, no
allocation - the same rule the vertex format and the palette follow, applied to
one more stage: a scene without characters pays nothing for the machinery that
attaches things to them.

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
