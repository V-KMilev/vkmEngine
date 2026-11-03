# Animation

Two systems, both in the Simulation stage, whose clocks advance on the **tick**,
not the frame: physics and the hitboxes read what a pose produces, so a pose is
simulation. They never contend for a component:

- **`AnimationSystem`** plays authored keyframe tracks onto an entity's own
  `Transform`. One entity, one animated object.
- **`SkeletalAnimationSystem`** plays a baked clip onto a *rig* and publishes the
  pose on `FrameProducts::poses`. It writes no `Transform`: a rig's bones are
  indices in an array, not entities.

# Keyframe animation

## Animation component

`Animation` holds three tracks - position, rotation, scale - each a time-sorted
`AnimationTrack<T>` with one easing curve, plus how long, how fast and whether it
loops:

```cpp
Animation bob;
bob.positionTrack.setEasing(Math::Easing::EaseInOutSine);
bob.positionTrack.addKeyframe(0.0f, {0, 0, 0});
bob.positionTrack.addKeyframe(2.0f, {0, 5, 0});
bob.positionTrack.addKeyframe(4.0f, {0, 0, 0});
```

**`playOnStart` is the authored flag and `playing` is the session's**
([decisions.md](decisions.md#what-has-been-decided)). The first update with a non-zero
simulation delta turns `playOnStart` into `playing` once and latches `started`,
so a one-shot that ends does not restart, and nothing moves while the editor is
paused before Play.

Each tick the system advances **every** `Animation` - no visibility filter, so
off-screen animation stays in sync - by the head rules every `Animator` uses too
(`advanceHead`, `rewindSpentHead`: a negative `speed` plays a clip backwards), and
writes the sampled tracks into the `Transform`.

## Easing functions

The curves are listed once in `base/math/easing.h`. **A curve's identity is its
`Math::Easing` enumerator**, never its function pointer: `setEasing` takes the
enumerator and the scene file carries its name. The enum's doc says why the
pointer is never stored.

---

# Skeletal animation

A rig is a `SkeletonAsset` and a clip an `AnimationClipAsset` (see
[resources.md](resources.md#skeletonasset)). One `Animator` binds them to a
character; the result is a pose published for the frame. `examples/potion_runner`
runs the whole chain in code with no asset file: `runner_rig.h` builds a rig and a
stride clip with `footstep` markers, limbs hang off bones through `BoneSocket`s,
and the game plays a footstep per `AnimationEvent`.

## Animator

`Animator` (`animator.h`) authors `skeleton`, `clip` (empty holds the bind
pose), `speed`, `looping` and `playOnStart`. The playback head, `playing`, the
crossfade and `adjust` are session state, never serialized; a scene saved
mid-blend reloads as the clip it was blending *to*.

**One Animator per character, on the rig, not one per mesh.** Import spawns a
sub-entity per mesh, so a character arrives as body plus clothes plus hair; a
pose per mesh would be three clocks drifting apart. **There is no `SkinnedMesh`
component**: a skinned mesh's rig is the nearest `Animator` at or above it in the
`Hierarchy`, the structure import produces, so no `EntityId` is stored to remap.

## Crossfading

```cpp
const bool looping = true;
Animator::crossFadeTo(scene().get<Animator>(character), runClip, 0.2f, looping);
```

The clip being left **keeps playing** at its own head while it fades, so a run
fading into a walk does not freeze one foot, and keeps its own looping, so a
one-shot swing fading into a run still clamps at its last frame rather than
wrapping. A call mid-fade back to the
clip on its way out **turns the fade round**, so a character flicking between two
states does not restart a stride each time. `crossFadeTo`'s declaration has the
rest of its rules.

## Adjusting a bone from gameplay

A clip cannot know where the player aims, how far a crouch goes or which way a
hit landed. A behavior says so through `Animator::adjust`, a list of
`BoneAdjust` (`animator.h`), applied after the clip is sampled and the crossfade
blended and before the bone is composed - so the hand, a weapon socketed to it,
ragdoll bodies placed from the pose and the skin all follow.

Rotation and offset are in **rig model space** about the bone's own origin, so a
caller never needs to know which way the authoring tool pointed a bone's local X;
an `absolute` entry is the bone's final orientation whatever the clip and parent
did, as an arm solved to hold a weapon needs. The behavior resolves the bone's
index from its name, and nothing clears the list. It is read whenever the pose is
composed, every running frame included, so arms aimed along a mouse-turned camera
keep up with it.

## Animation events

A clip carries **markers** - instants it announces, a footstep, the frame a swing
connects. They live on the clip, so every character playing it gets them, and
crossing one enqueues an `AnimationEvent { EntityId entity; std::string marker; }`
on the `EventBus`, naming the entity carrying the `Animator`. Neither glTF nor
FBX carries markers, so they are authored in the clip's recipe:

```json
{ "kind": "model", "path": "assets/hero.glb", "clip": 2,
  "markers": [ { "name": "footstep", "time": 0.12 },
               { "name": "footstep", "time": 0.42 } ] }
```

The import drops a marker with no name or a time outside the clip and sorts the
rest by time. Gameplay subscribes - there is no `Behavior` hook:

```cpp
subscribe([this](const AnimationEvent& e) {
    if (e.entity != m_player || e.marker != "footstep") return;
    if (!m_grounded) return;                       // gameplay decides, not the clip
    playSoundAt(footstep, scene().get<Transform>(m_player).position);
});
```

The clip says *when*; gameplay says *whether* and *what it sounds like*. A
footstep wants a request rather than an `AudioSource` because the next footfall
can come before the last one's clip ends - see
[Starting a sound with no entity](audio.md#starting-a-sound-with-no-entity).

A marker is announced once each time the head **passes** it, in either
direction (`crossesMarker`), and only by the clip the `Animator` is playing, so a
crossfade cannot double a footstep. A paused frame announces nothing and stores nothing
up for resume; setting `Animator::time` by hand is a teleport, not a sweep; and a
marker at time 0 fires when a loop wraps to it, not when the clip starts. The
events deliver at the top of the next Simulation stage, as a contact's do, and
`AudioSystem` runs in the Transform stage after it, so the sound starts on the
frame the marker was crossed.

## Bone sockets

A weapon in a hand, a hat on a head: the attached entity carries a
`BoneSocket { std::string bone; Transform offset; }`, and `BoneSocketSystem`
places it on that bone every frame. A muzzle flash under the gun is just a
child of the gun.

- **The socket must be a direct child of the entity carrying the `Animator`.**
  The system writes the socket's *local* `Transform` as `global[bone] * offset`
  ahead of `HierarchySystem`, which multiplies in the parent's world the same
  frame - landing on the bone only when that parent is the rig.
- **Its `Transform` is an output**, rewritten every tick and every frame; author the `offset`.
  A frame between ticks places it on the pose drawn for its moment, and it goes back after the
  frame as a carried body does ([physics.md](physics.md#drawn-between-ticks)), so a tick reads a
  socketed muzzle or hitbox where the last tick's pose put it, at any frame rate and on a server. One
  the frame took its `BoneSocket` from, or hung from another parent, stays where the frame put it.
- **The bone is named, never indexed**: a joint inserted on re-export shifts every
  index.

An unplaced socket stays where it last was, which is silent on screen, so
`BoneSocketSystem` logs each reason once per gap.

---

## How it works inside

### How a rig is posed

`SkeletalAnimationSystem` publishes a `PoseBuffer` on `FrameProducts::poses`, as
`VisibilitySystem` publishes `ctx.visibility`: each bone's transform in rig model
space - *the pose*, what sockets and physics read - and beside it the palette the
vertex stage wants. `PoseBuffer::sliceOf` says which entities a rig's slice
answers for. A slice's bounds are the bone origins only; the culls inflate them
for skin ([visibility.md](visibility.md#posed-bounds)).

`composePose` is one forward loop over the bones - bind pose, the clip's
channels, the outgoing clip blended in, the adjustments, then
`global[i] = global[parent] * local` - which relies on `parent < index`
([resources.md](resources.md#skeletonasset)). The crossfade blends the **local
TRS**: blending composed matrices pulls a joint toward the midpoint of two world
positions and shortens the limb.

A running frame composes the pose again where it falls between ticks, so a
character does not step against a camera that moves every frame, as a moving body
does not ([physics.md](physics.md#drawn-between-ticks)), and a rig an active
ragdoll has taken over is composed from its bodies where `PhysicsSystem` left
them, so the skin is drawn where its bodies are. `SkeletalAnimationSystem` says
when else a pose is composed, and what runs in parallel.

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
`GLSceneCapture` draws skinned meshes with the static programs, so GI sees the
bind pose; `GLPreview` poses one when its request brings a palette
(`PreviewDraw::bones`), and draws it in bind pose otherwise.

**The palettes travel as one flat array**: `RenderView::SceneLists::skinMatrices` is the
frame's `PoseBuffer::palette()`, and each object carries its rig's range
(`RenderObjects::skinFirst`, `ObjectDraw::skinCount`). `InstanceDraw::skinned`
leads the batch sort key, so a bucket switches program once. A skinned mesh with
no rig above it draws through the static program, in bind pose.

Each skinned program is its static pair's folder built with the define `SKINNED`
(`Vkm::OpenGL::Shader(path, {"SKINNED"})`), not a copy. All take position from `instanceWorldPosition` in
`shaders/opengl/include/instancing.glsl`, so the prepass and forward pass agree on depth and the
shadow matches what the camera sees. `GLForwardPass::bindFrameUniforms` sets the
static and skinned programs' frame uniforms from one place; a uniform added to one
alone goes missing on characters.

Two limits, on purpose:

- **Normals are exact under uniform bone scale only.** A bone scaled unevenly
  tilts the normal off the surface - a case rigs essentially never author.
- **The shader does not clamp bone indices.** `findMeshFault`, which the cook and
  the cooked reader hold a mesh to, refuses an index past `SkeletonAsset::MAX_BONES`,
  and a mesh under its own rig is sound. A skinned mesh
  moved under a different rig reads the wrong bones or past the palette; the
  log warning above is the answer, not a per-vertex clamp.

### Key files

- `src/engine/ecs/component/animation/animation.h` - `Animation` and its tracks, AnimationTrack<T>
- `src/engine/base/math/easing.h` - easing curves
- `src/engine/system/animation/animation_system.h` - AnimationSystem
- `src/engine/ecs/component/animation/animator.h` - Animator, BoneAdjust
- `src/engine/ecs/component/animation/bone_socket.h` - BoneSocket
- `src/engine/system/animation/skeletal_animation_system.h` - SkeletalAnimationSystem
- `src/engine/system/animation/playback.h` - `advancePlayback`, `crossesMarker`
- `src/engine/system/animation/pose/clip_pose.h` - `composePose`, `sampleAhead`
- `src/engine/system/animation/animation_events.h` - AnimationEvent
- `src/engine/core/frame/pose_buffer.h` - PoseSlice, PoseWrite, PoseBuffer
- `src/engine/system/animation/pose/ragdoll_pose.h` - a ragdoll bone body's pose, read before the parallel pass
- `src/engine/system/animation/bone_socket_system.h` - BoneSocketSystem
- `src/pipeline/import/model/model_import.cpp` - a clip recipe's markers
- `src/backend/opengl/frame/gl_skin_palette.h` - GLSkinPalette (the frame's palettes)
- `shaders/opengl/include/instancing.glsl` - the vertex-stage skinning contract, under `SKINNED`
