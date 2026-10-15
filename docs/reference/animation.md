# Animation

Two systems, both in the Simulation stage and both on the **tick**, not the
frame: physics and the hitboxes read what a pose produces, so a pose is
simulation. They never contend for a component:

- **`AnimationSystem`** plays authored keyframe tracks onto an entity's own
  `Transform`. One entity, one animated object.
- **`SkeletalAnimationSystem`** plays a baked clip onto a *rig* and publishes the
  pose on `FrameContext::poses`. It writes no `Transform`: a rig's bones are
  indices in an array, not entities.

# Keyframe animation

## Animation component

`Animation` holds three independent tracks - position, rotation, scale - and
`length`, `speed`, `looping` and `playOnStart`. `time`, `playing` and `started`
are runtime only, never serialized.

**`playOnStart` is the authored flag and `playing` is the session's**
([engine.md](../guides/engine.md#4-what-has-already-been-decided)). The first update with a non-zero
simulation delta turns `playOnStart` into `playing` once and latches `started`,
so a one-shot that ends does not restart, and nothing moves while the editor is
paused before Play.

The duration is derived on read: the latest keyframe, or `length` when that is
longer, which holds an animation open past its last keyframe (a pause at the end
of a loop).

Each tick the system walks **every** `Animation` - no visibility filter, so
off-screen animation stays in sync - advances `time` by the fixed step times
`speed`, wraps when `looping` or else clamps and stops (`advanceHead`, the rule
every `Animator` head uses too), and writes the sampled tracks into
`Transform.position` / `.rotation` / `.scale`. A one-shot with nothing left to
play in the direction it moves is rewound to the other end first
(`rewindSpentHead`), which is how a clip played backwards starts.

## AnimationTrack<T>

A time-sorted keyframe sequence with one easing function:

```cpp
AnimationTrack<glm::vec3> track;
track.setEasing(Easing::EaseInOutSine);
track.addKeyframe(0.0f, {0, 0, 0});
track.addKeyframe(2.0f, {0, 5, 0});

glm::vec3 value = track.getValue(1.0f);  // eased, interpolated
```

`getValue(time)` clamps `time` to the keyframe range, eases within the enclosing
interval, and interpolates - `glm::slerp` for `glm::quat`, `glm::mix` otherwise.

## Easing functions

The curves are `linear` plus the In, Out and InOut forms of Quad, Cubic, Quart,
Quint, Sine, Expo, Circ, Back, Elastic and Bounce, named like `easeInOutSine`,
all listed once in `core/math/easing.h`.

**A curve's identity is its `Easing` enumerator**, never its function pointer:
`setEasing` takes the enumerator, `easingFunction` resolves it at the point of
call, and the scene file carries the name. Do not store the pointer - `vkm_core`
is a shared library, and on Windows the editor's copy of the table and the
engine's hold different addresses for the same row.

---

# Skeletal animation

A rig is a `SkeletonAsset` and a clip an `AnimationClipAsset` (see
[Resources](resources.md)). One `Animator` binds them to a character; the result
is a pose published for the frame.

## Animator

`Animator` (`animator.h`) authors `skeleton`, `clip` (empty holds the bind
pose), `speed`, `looping` and `playOnStart`. The playback head `time`,
`playing`, the crossfade state and `adjust` are session state, never
serialized; a scene saved mid-blend reloads as the clip it was blending *to*. A
start phase a game wants - two characters half a lap apart - is set by the
behavior that spawns them.

**One Animator per character, on the rig, not one per mesh.** Import spawns a
sub-entity per mesh, so a character arrives as body plus clothes plus hair; a
pose per mesh would be three clocks drifting apart.

**There is no `SkinnedMesh` component.** A mesh is skinned exactly when its
`MeshAsset::skin` is non-empty, and its rig is the nearest `Animator` at or
above it in the `Hierarchy` - the structure import produces. No `EntityId` is
stored, so prefabs, undo and scene load never remap it.

The Animator card's transport writes `playing` and its timeline scrubs `time`,
with no undo step: previewing is not an edit.

## Crossfading

```cpp
const bool looping = true;
Animator::crossFadeTo(scene().get<Animator>(character), runClip, 0.2f, looping);
```

The clip being left moves into `fadeFrom` and **keeps playing** at its own head,
so a run fading into a walk does not freeze one foot. Looping belongs to each
clip: the incoming one's is the argument, the outgoing one's is parked on
`fadeLooping`, so a death fading out of a walk still clamps at its last frame.
The blend runs in unscaled simulation seconds, not scaled by `speed`.

Calling it with the clip already playing leaves it alone; with that clip
stopped, plays it again from its start; with another stopped, blends out of the
frame it holds. No clip to blend out of, or `seconds <= 0`, is a cut. A second call mid-fade drops the clip on its way out,
so the pose jumps by its remaining weight - the price of two slots.

The countdown runs on simulation time, not on `playing`: a one-shot shorter than
the blend into it stops, and the fade still finishes on its last frame.

## Adjusting a bone from gameplay

A clip cannot know where the player aims, how far a crouch goes or which way a
hit landed. A behavior says so through `Animator::adjust`, a list of
`BoneAdjust`, applied after the clip is sampled and the crossfade blended and
before the bone is composed - so the hand, a weapon socketed to it, ragdoll
bodies placed from the pose and the skin all follow. Each `BoneAdjust`
(`animator.h`) names a bone by index and adds a rotation, an offset and a scale:

- Rotation and offset are in **rig model space** about the bone's own origin, so
  a caller never needs to know which way the authoring tool pointed a bone's
  local X. A caller in world axes turns its axis by the inverse of the rig's
  world rotation.
- An `absolute` entry is the bone's final model-space orientation whatever the
  clip and parent did - what an arm solved to hold a weapon needs.
- A `scale` *near* zero hides a bone's skin (a first-person head); exactly zero
  leaves a socket or hit box no orientation to read.
- The bone index is resolved by the behavior, from a name, against the
  Animator's skeleton. Nothing clears the list: write an identity or drop the
  entry.
- It is read when the pose is composed, on the tick. A bone following something
  that moves every frame (arms aimed along a mouse-turned camera) steps at the
  tick rate; run a tick rate at or above the frame rate, or parent the thing to
  the camera instead.

## Animation events

A clip carries **markers** - named instants, authored in its recipe (see
[AnimationClipAsset](resources.md#animationclipasset)). Crossing one enqueues an
`AnimationEvent { EntityId entity; std::string marker; }` on the `EventBus`,
naming the entity carrying the `Animator`. Gameplay subscribes - there is no
`Behavior` hook:

```cpp
subscribe([this](const AnimationEvent& e) {
    if (e.entity != m_player || e.marker != "footstep") return;
    if (!m_grounded) return;                       // gameplay decides, not the clip
    events().emit(PlaySoundEvent::at(m_footstep, scene().get<Transform>(m_player).position));
});
```

The clip says *when*; gameplay says *whether* and *what it sounds like*. A
footstep wants a request rather than an `AudioSource` because the next footfall
can come before the last one's clip ends - see
[Starting a sound with no entity](audio.md#starting-a-sound-with-no-entity).

A marker is announced once each time the head **passes** it, in either
direction: landing on it counts, moving off it again does not, a wrap announces
it on the lap it wraps into, and a frame covering several loops announces it
once (`crossesMarker`).

Nothing is announced by a paused frame (and nothing stored up for resume), by
setting `Animator::time` by hand (a teleport, not a sweep), or by a crossfade's
outgoing clip. A marker at time 0 fires when a loop wraps to it, not when the
clip starts.

The events deliver at the top of the next Simulation stage the engine runs, as
a contact's do; `AudioSystem` runs in the Transform stage after it, so the sound
starts on the frame the marker was crossed.

## Bone sockets

A weapon in a hand, a hat on a head: the attached entity carries a
`BoneSocket { std::string bone; Transform offset; }`, and `BoneSocketSystem`
places it on that bone every frame. A muzzle flash under the gun is just a
child of the gun.

- **The bone is named, never indexed** - re-export a rig with a joint inserted
  and every stored index addresses its neighbour. The lookup is memoised and
  re-resolves when the name or the rig changes.
- **The socket must be a direct child of the entity carrying the `Animator`.**
  The system writes the socket's *local* `Transform` as `global[bone] * offset`
  and `HierarchySystem` multiplies in the parent's world, which lands on the bone
  only when that parent is the rig.
- **Its `Transform` is an output**, rewritten every frame; author the `offset`.
- It runs in `SystemStage::Transform`, **ahead of `HierarchySystem`**: after the
  pose is published, and before the world resolve, so the socket is on its bone
  the frame the character moves. It runs every frame, so scrubbing a paused
  `Animator` moves what the character holds.

An unplaced socket stays where it last was, which is silent on screen, so
`BoneSocketSystem` logs each reason once per gap, such as a socket
that is not a direct child of the rig, or a bone name the rig does not have.

## Seeing it

**View > Show > Skeletons** draws every posed rig from `ctx.poses`, with an axis
triad per bone on the selected rig. The **Animator card** has a rig picker, a
clip picker and a transport that scrubs while paused, and names a clip cooked
against another rig ([editor.md](editor.md)).

`examples/potion_runner` runs the whole chain in code with no asset file:
`runner_rig.h` builds a rig and a stride clip with `footstep` markers,
limbs hang off bones through `BoneSocket`s, and the behaviour plays a footstep
per `AnimationEvent`. `./build/bin/vkm_runtime examples/potion_runner`.

---

## How it works inside

### The pose and the palette

`SkeletalAnimationSystem` publishes a `PoseBuffer` on `FrameContext::poses`, as
`VisibilitySystem` publishes `ctx.visibility`. It holds two parallel arrays:

| Array | What it is |
|-------|------------|
| `global()` | Each bone's transform in rig model space - *the pose*, what sockets and physics read. |
| `palette()` | `global[b] * inverseBind[b]` - what the vertex stage wants. |

Rigs are packed end to end; `ctx.poses->sliceOf(entity)` returns the rig's
`PoseSlice` (its range, plus the box of posed bone origins and the largest bone
scale), or null when not posed. It answers for the rig entity and every
descendant, up to a nested `Animator`. The slice's bounds are the pose's only;
`VisibilitySystem` must inflate them for skin
([visibility.md](visibility.md#posed-bounds)), or the culls delete posed skin.

### How a rig is posed

Everything that touches the `ResourceManager` or the scene's structure runs
serially before the `parallelFor` that advances the heads and runs
`composePose`, one rig per index; markers are announced serially after it, since
the `EventBus` is main-thread only.

`composePose` is one forward loop over the bones: bind pose, overwritten by the
clip's channels, blended with the outgoing clip, adjusted, then
`global[i] = global[parent] * local`. The crossfade blends the **local TRS**,
before the parent multiply; blending composed matrices pulls a joint toward the
midpoint of two world positions and shortens the limb. The single loop relies on
`parent < index`: import emits bones that way, and `findSkeletonFault` states
the rule for the cooked reader, the writer and the system to check.

The pose is composed once per tick, and once per paused frame from where the
heads stand, so a paused scrub shows. A running frame with no tick reuses the
last tick's pose unless the world was replaced (`Scene::epoch()` moved). A clip
whose bone table does not match the rig, or that names another rig, is refused
and logged once; a skeleton or clip built in code is judged by `findSkeletonFault`
and `findClipFault` before use.

A rig an active ragdoll has taken over is composed from its bodies, which are
read before `PhysicsSystem` moves them, so **a ragdoll's skin trails its bodies
by one tick**.

### The frame a pose lives in

Skinned vertices resolve into the rig's model space, so the rig entity's world
matrix places the character: `worldOfBone[b] == rigWorld * global[first + b]`.
Import puts the `Animator` on the entity whose frame the bones are composed in,
and gives every skinned mesh its own entity parented to that rig at identity
(the inverse-bind matrices already carry the mesh's placement). Bone nodes spawn
no entities.

Hand-authoring can break this, and the pose system logs both, once per gap: a
skinned mesh whose `MeshAsset::skeleton` is not the rig above it (bones address
the wrong joints), and a skinned mesh below the rig sitting off its origin (its
transform is applied twice).

### The GPU path

**The rig binding is a second vertex stream** (bone indices and weights, divisor
0) in the `GLMeshPool`'s skinned layout, used by meshes whose `MeshAsset::skin`
is non-empty. It is not more fields on `Vertex`, because every vertex of every
mesh would then pay for it - the shadow pass, which reads only `aPos`, most.
`GLSceneCapture` and `GLPreview` draw skinned meshes with programs that ignore
the stream, so GI and thumbnails see the bind pose.

**The palettes travel as one flat array**: `RenderView::skinMatrices` is the
frame's `PoseBuffer::palette()`, and each object carries its rig's range
(`RenderObjects::skinFirst`, `ObjectDraw::skinCount`). `InstanceDraw::skinned`
leads the batch sort key, so a bucket switches program once. A skinned mesh with
no rig above it draws through the static program, in bind pose. A frame that
posed nothing leaves `skinMatrices` null and the skinned programs unbound.

Each skinned program (`shaders/forward/pbr_skinned`, `shaders/shadow/depth_skinned`
and the rest named `*_skinned`) is its static pair's vertex file included under
`#define SKINNED`, not a copy. All take position from `skinnedWorldPosition` in `shaders/skinning.glsl`, so the prepass
and forward pass agree on depth and the shadow matches what the camera sees.
`GLForwardPass::bindFrameUniforms` sets the static and skinned programs' frame
uniforms from one place; a uniform added to one alone goes missing on
characters.

Two limits, on purpose:

- **Normals are exact under uniform bone scale only.** A bone scaled unevenly
  tilts the normal off the surface - a case rigs essentially never author.
- **The shader does not clamp bone indices.** `readMesh` refuses an index past
  `MAX_SKELETON_BONES`, and a mesh under its own rig is sound. A skinned mesh
  moved under a different rig reads the wrong bones or past the palette; the
  log warning above is the answer, not a per-vertex clamp.

### Key files

- `src/engine/ecs/component/animation/animation.h` - Animation component
- `src/engine/ecs/component/animation/animation_track.h` - AnimationTrack<T>
- `src/engine/core/math/easing.h` - easing curves
- `src/engine/system/animation/animation_system.h` - AnimationSystem
- `src/engine/ecs/component/animation/animator.h` - Animator, BoneAdjust
- `src/engine/ecs/component/animation/bone_socket.h` - BoneSocket
- `src/engine/system/animation/skeletal_animation_system.h` - SkeletalAnimationSystem
- `src/engine/system/animation/pose_evaluator.h` - `advancePlayback`, `crossesMarker`, `composePose`
- `src/engine/system/animation/animation_events.h` - AnimationEvent
- `src/engine/system/animation/pose_buffer.h` - PoseSlice, PoseWrite, PoseBuffer
- `src/engine/system/animation/ragdoll_pose.h` - a ragdoll bone body's pose, read before the parallel pass
- `src/engine/system/animation/bone_socket_system.h` - BoneSocketSystem
- `src/backend/opengl/frame/gl_skin_palette.h` - GLSkinPalette (the frame's palettes)
- `shaders/skinning.glsl` - the vertex-stage skinning contract
