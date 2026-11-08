# Visibility System

`VisibilitySystem` decides each frame what the renderer draws: it resolves the view, works out
every mesh's transform, box and draw, and culls them against the camera. The result,
`FrameProducts::visibility`, is what `RenderView` is built from ([rendering.md](rendering.md)) and
what the editor picks with.

A scene says what it wants drawn through its `Mesh` components (`ecs/component/render/mesh.h`):
`visible`, `castShadows`, `seenBy` - who draws it through a scene camera, as a first-person body,
arms or a weapon - and `movable`. The sections up to [How it works inside](#how-it-works-inside)
are what those promise.

## Where the view comes from

- **A host's view.** An authoring host publishes `FrameProducts::hostView` before the Visibility
  stage (the editor's `CameraControllerSystem` does). A `HostView` is a free view, or, when
  `through` names an entity, a scene camera to look through exactly as if it were active.
- **The scene's active camera**, when `hostView` is null - every runtime frame.

Null is an answer, not a missed publish: the frame's products are cleared as each frame begins
(`FrameContext::clearProducts`). The active camera is held while it stays alive, active and posed,
even if a lower-slot one turns active, so the view does not jump; only when it goes, or the world
is replaced, does `findActiveCamera` pick the lowest-slot active one. One `ActiveCamera`
(`FrameContext::camera`) holds it, so the commands' view, a behavior's `screen()` and the ear with no
listener look through the camera the frame does. A camera's pose is its
`WorldTransform` when it has one, so a camera parented to a rig renders from the rig. A runtime
scene with no active camera renders nothing and logs once per gap; a host's free view needs none.

## Fixed meshes

A mesh with `Mesh::movable` off promises it never moves, changes or hides while the world runs -
scenery. Its columns are worked out once and kept; later frames only cull its kept box, and an
`LOD` still picks its level. The cull flags it `ObjectDraw::fixed`, and
`RenderObjects::fixedSerial` moves on any frame one of them changes - what the shadow tiles sign
fixed casters by ([lighting.md](lighting.md#shadow-atlas)) - and the lightmaps bake what is fixed
([lighting.md](lighting.md#baked-shadows)).

A broken promise is caught: each frame one in `VisibilitySystem::FIXED_CHECK_STRIDE` kept meshes
is worked out again and compared, and a mismatch logs a warning once per world and rebuilds
everything the next frame. A skinned mesh, water with waves and a viewmodel are never kept,
whatever the flag. A host that may edit the world (`HostChrome::authoring`) keeps nothing. No
material is read, so editing one keeps every kept mesh.

## Level of detail

An entity with an `LOD` component (`ecs/component/render/lod.h`) draws coarser meshes with
distance; the header says how a level is picked, and `generateLOD`
(`src/pipeline/mesh/lod_generator.h`) builds the levels and sets where each hands over. Selection
runs for every mesh, not only visible ones, because the shadow pass draws the same level; the
choice is published as `ObjectDraw::mesh`, so the renderer never reads `LOD`. A level does not pop
into the next: it fades ([The cull](#the-cull)). A terrain's plants take levels by a rule of their
own ([terrain.md](terrain.md#plants)).

## Occlusion culling, on the GPU, as an option

This system does not cull by occlusion. With `RenderSettings::occlusionCulling` on, the backend
culls the opaque batch on the GPU in two phases against a depth pyramid (`GLOcclusionCull`, run by
the depth prepass), testing each object by its world box from this cull - posed, grown for waves -
so characters and the sea are culled too; a viewmodel is always shown. The image is the same
either way, except that two instances of one mesh overlapping at the same depth may swap which
shows from frame to frame. It saves only hidden instances' vertex work and is off by default;
when it pays is [decisions.md](decisions.md#what-has-been-decided)'s.

## How it works inside

### What it produces

`Visibility` (`visibility.h`) holds the frame's `RenderObjects`
(`core/frame/render_objects.h`): parallel per-object columns indexed by the Mesh's storage
index, then the terrains' chunks and plants after them
([terrain.md](terrain.md#how-it-works-inside)), and three index lists:

- **`visible`** - what the camera sees, unsorted.
- **`scene`** - every drawn mesh, shadow casters first. **Not camera-culled**: shadows need what
  is outside the frustum but inside a light's, and a probe or irradiance capture sees behind the
  camera.
- **`changed`** - objects whose columns differ from last frame's (or `allChanged`); the backend
  refreshes its GPU copies from this alone.

Beside them, `levels` names every level of detail a fixed object may draw but its given mesh,
written when `fixedSerial` moves: the backend readies each, and what reads the object's bake
through it, before the eye picks one, so a camera cut that picks many costs nothing that frame.

`Visibility` is owned by the system and reused across frames; each field is documented on the
struct.

### The cull

Every Mesh is worked out in parallel (`visibility_system.cpp`), its columns written whether it is
seen or not, then a serial gather builds the lists in Mesh order:

- **A mesh with no mesh asset, no material, no `Transform`, degenerate bounds or `visible` off is
  in no list** - it neither draws nor casts.
- **Its box** is its posed one for a posed skinned mesh ([Posed bounds](#posed-bounds)); water
  with waves grows by its share of how far they carry water ([water.md](water.md#limits)).
- **It is visible** if it passes the frustum (`Math::frustumIntersectsAABB`; the far plane,
  `Camera::zFar`, is the draw distance) and the screen-size test (`Culling::sizeOverCull`, seen
  at 1 or more).
- **It casts** if `Mesh::castShadows` is set, unless it is water with waves or seen by the eye
  alone.
- **`Mesh::seenBy` applies through a scene camera alone**: `Viewmodel` is culled against its own
  frustum ([rendering.md](rendering.md#the-viewmodel)), and a host's free view, the editor's, sees
  each mesh as any.

The thresholds are constants of `Culling` (`culling.h`): `Culling::MIN_PIXELS` and
`Culling::FADE_BAND`, the same for every project.

**What fades.** A mesh does not pop at the screen-size cull or at a level's hand-over. Within
`Culling::FADE_BAND` over the screen-size cull it fades out, giving up a share of its samples to
what lies behind, all of them at the cull. Past a
level's range, for `FADE_BAND` of it, the next level fades in on the samples the level gives up;
across that band `ObjectDraw::mesh` is still the level, and past it the next. A hand-over near the
cull fades out both levels together, each keeping its share of what still shows. The share rides
in `RenderObjects::fades`, which only the eye draws
([rendering.md](rendering.md#fades)); shadows and captures draw `ObjectDraw::mesh` whole and
switch at the band's end, so a held shadow is not drawn again across the band.

**What is kept of a fixed mesh** is worked out again when what it was made from changes - another
Mesh at its index (`SparseSet::stampAt`) or another mesh asset given to it, its entity gaining or
losing a `Transform`, `WorldTransform`, an `LOD` with levels or `Water`, or a source every object
reads (`VisibilitySystem::ObjectSources`).

### Posed bounds

A skinned mesh's stored bounds are its bind pose, and a posed character leaves that box. The
frustum, shadow and occlusion culls trust the box, so an undersized one deletes visible geometry -
a character would vanish for raising an arm. A skinned mesh posed this frame is therefore bounded
by its pose: the box of the posed bone origins, padded by `MeshAsset::skinRadius` times the
largest bone scale. `skinRadius` is owed by whoever fills `skin`
([resources.md](resources.md#meshasset)). Unskinned meshes, and skinned ones with no rig above
them, keep their stored bounds.

## Key files

- `src/engine/system/visibility/visibility_system.h` - `VisibilitySystem`
- `src/engine/core/frame/visibility.h` - `Visibility`
- `src/engine/core/frame/render_objects.h` - `RenderObjects`
- `src/engine/core/frame/host_view.h` - `HostView`
- `src/engine/system/visibility/culling.h` - the culling constants and tests
- `src/engine/ecs/component/render/lod.h` - `LOD`
