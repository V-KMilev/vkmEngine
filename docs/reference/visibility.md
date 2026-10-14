# Visibility System

`VisibilitySystem` decides each frame what the renderer draws: it resolves the
view, works out every mesh's transform, box and draw, and culls them against
the camera. The result, `FrameContext::visibility`, is what `RenderView` is
built from ([rendering.md](rendering.md)) and what the editor picks with.

## What it produces

`Visibility` (`visibility.h`) holds the frame's `RenderObjects`
(`system/render/data/render_objects.h`): parallel per-object columns (model,
world box, draw, skin range), indexed by the Mesh's storage index, and three
index lists:

- **`visible`** - what the camera sees, unsorted.
- **`scene`** - every drawn mesh, shadow casters first (`casterCount` of them).
  **Not camera-culled**: shadows need what is outside the frustum but inside a
  light's, and a probe or irradiance capture sees behind the camera.
- **`changed`** - objects whose columns differ from last frame's (or
  `allChanged`); the backend refreshes its GPU copies from this alone.

Plus the per-object `entities`, the resolved camera as a `CameraData` (depth of
field included, so nothing downstream recomputes it), and `cameraEntity`.
`Visibility` is owned by the system and reused across frames. Each field is
documented on the struct.

## The cull

For every Mesh, in parallel (`update` in `visibility_system.cpp`):

- A mesh with no mesh asset, no material, no `Transform`, degenerate bounds or
  `visible` off is in no list - it neither draws nor casts.
- Its local box (the posed one for a posed skinned mesh, see
  [Posed bounds](#posed-bounds)) goes to world space; a `waves` surface under an
  ocean grows by how far the sea carries water
  ([rendering.md](rendering.md#the-ocean)).
- Its columns are written for **every** valid mesh, not only visible ones.
- It is visible if it passes the frustum (`Math::frustumIntersectsAABB`),
  distance (`Culling::isNearEnough`, `RenderSettings::cullMaxDistance`, off at
  0 or below) and screen-size (`Culling::isLargeEnough`,
  `RenderSettings::cullMinPixels`) tests.
- It casts if `Mesh::castShadows` is set, unless it is a `waves` surface under an
  ocean.
- Through a scene camera, `Mesh::seenBy` has its say: `AllButEye` (a
  first-person body) is in `scene` and casts but is never visible; `EyeOnly` (arms,
  a weapon) is seen by the eye alone: visible, but not in `scene` and casting
  nothing; `Viewmodel` is `EyeOnly`, flagged
  `ObjectDraw::viewmodel` and culled against its own frustum
  ([rendering.md](rendering.md#the-viewmodel)). A host's free view, the
  editor's, sees each as any mesh.

A serial gather then builds the lists in Mesh order. The thresholds are read off
`FrameContext::render` each frame - the engine's one copy, which the host seeds
from the project and the editor's Culling card edits live - so the system holds
none of its own.

### Where the view comes from

- **A host's view.** An authoring host publishes `FrameContext::hostView`
  before the Visibility stage (the editor's `CameraControllerSystem` does). A
  `HostView` is a free view, or, when `through` names an entity, a scene camera
  to look through exactly as if it were active.
- **The scene's active camera**, when `hostView` is null - every runtime frame.

Null is an answer, not a missed publish: `FrameContext` is rebuilt each frame.
The active camera is held while it stays alive, active and posed, even if a
lower-slot one turns active, so the view does not jump; only when it goes, or
the world is replaced, does `findActiveCamera` pick the lowest-slot active one.
A camera's pose is its `WorldTransform` when it has one, so a camera parented to
a rig renders from the rig. A runtime scene with no active camera renders
nothing and logs once per gap; a host's free view needs none.

## Fixed meshes

A mesh with `Mesh::movable` off promises it never moves, changes or hides while
the world runs - scenery. Its columns are worked out once and kept; later
frames only cull its kept box, and an LOD component still picks its level. The
cull flags it `ObjectDraw::fixed`, and `RenderObjects::fixedSerial` moves on any
frame one of them changes - what the shadow tiles sign fixed casters by
([lighting.md](lighting.md#shadow-atlas)).

What is kept is worked out again when what it was made from changes - another
Mesh at its index (`SparseSet::stampAt`), its entity gaining or losing a
Transform, WorldTransform or LOD, or a source every object reads
(`VisibilitySystem::ObjectSources`). A host that may edit the world
(`HostChrome::setAuthoring`) keeps nothing.

A broken promise is caught: each frame one in `FIXED_CHECK_STRIDE` kept meshes
is recomputed and compared; a mismatch logs a warning once per world and
rebuilds everything the next frame. A skinned mesh, a `waves` surface under an
ocean and a viewmodel are never kept, whatever the flag.

## Level of detail

An entity with an `LOD` component (`ecs/component/render/lod.h`) draws coarser
meshes with distance. Levels are ordered near to far, each with a `maxDistance`
set for a 60-degree vertical field of view (`LOD::REFERENCE_P11`), so a zoomed
view holds each level further out; `bias` scales them all. Past the last level
the last keeps drawing - hiding is `Culling::isNearEnough`'s job. Selection runs
for every mesh, not only visible ones, because the shadow pass draws the same
level; the choice is published as `ObjectDraw::mesh`, so the renderer never reads
`LOD`.

**Generate Levels** on the inspector's LOD card (`generateLOD`,
`src/tools/cook/lod_generator.h`) simplifies the mesh into named assets
(`<mesh>:lod1`, ...) and sets each hand-over where the next level's error
projects to about a pixel.

## Occlusion culling, on the GPU, as an option

This system does not cull by occlusion. With `RenderSettings::occlusionCulling`
on, the backend culls the opaque batch on the GPU (`GLOcclusionCull`, run by the
depth prepass) in two phases against a depth pyramid: last frame's survivors
first, then whatever that reveals. Each object is tested by its world box from
this system's cull - posed, grown for waves - so characters and the sea are
culled too; a viewmodel is always shown. The image is the same either way,
except that two instances of one mesh overlapping at the same depth may swap
which shows from frame to frame. It saves only hidden instances' vertex work and
is off by default; when it pays is
[engine.md](../guides/engine.md#4-what-has-already-been-decided)'s.

## Posed bounds

A skinned mesh's stored bounds are its bind pose, and a posed character leaves
that box. The frustum, shadow and occlusion culls trust the box, so an
undersized one deletes visible geometry - a character would vanish for raising
an arm. A skinned mesh posed this frame is therefore bounded by its pose: the
box of the posed bone origins, padded by `MeshAsset::skinRadius` times the
largest bone scale (`poseLocalBounds`). `skinRadius` is owed by whoever fills
`skin` ([resources.md](resources.md#meshasset)). Unskinned meshes, and
skinned ones with no rig above them, keep their stored bounds.

## Key files

- `src/engine/system/visibility/visibility_system.h` - `VisibilitySystem`
- `src/engine/system/visibility/visibility.h` - `Visibility`
- `src/engine/system/render/data/render_objects.h` - `RenderObjects`
- `src/engine/system/visibility/host_view.h` - `HostView`
- `src/engine/system/visibility/culling.h` - the culling parameters and tests
- `src/engine/ecs/component/render/lod.h` - `LOD`
