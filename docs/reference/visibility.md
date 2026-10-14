# Visibility System

`VisibilitySystem` determines which entities are visible each frame
through a multi-stage culling pipeline. Results land in
`FrameContext.visibility` and are consumed by `RenderSystem` (which
builds the per-frame `RenderView`) and by the editor's picking and
overlays.

## Key files

- `src/engine/system/visibility/visibility_system.h` for `VisibilitySystem`
- `src/engine/system/visibility/visibility.h` for the `Visibility` result struct
- `src/engine/system/visibility/host_view.h` for `HostView`, the view an authoring host renders through
- `src/engine/system/visibility/culling.h` for the per-frame culling parameters and the culls
- `src/engine/core/math/bounds.h` for the AABB helpers

## Pipeline

```
VisibilitySystem::update(ctx)
  1. Resolve the view (see "Where the view comes from" below): a host's
     view when `FrameContext::hostView` offers one, else the active camera
     via `findActiveCamera` (`ecs/component/render/camera.h`) - the camera
     already held is kept, full scan only when it is gone.
  2. Build VisibilityContext: frustum planes, camera position and view
     matrix, and the thresholds (pre-squared for the sqrt-free tests).
  3. Resize the persistent flat per-index arrays to the full Mesh count: a
     state byte per index, and the columns of `Visibility.objects` and
     `Visibility.entities`. An object is a Mesh, and its index is the Mesh's
     storage index.
  4. parallelFor over all Mesh entities (each worker writes disjoint indices).
     A fixed mesh kept from an earlier frame (see "Fixed meshes" below) takes
     only the three culls on its kept box; the rest:
     - Skip if !visible, no mesh, no material, no Transform, or degenerate
       bounds. A mesh with nothing to shade it with is left out of every list
       here, so it neither draws nor casts - one decision, not one per reader.
     - Resolve the world matrix inline, through raw storage pointers rather
       than resolvedWorldMatrix: read WorldTransform if present, else compute
       from the local Transform (HierarchySystem already ran, at the Transform stage).
     - Pick the local box: the mesh's own bounds, or - for a skinned mesh the
       frame posed - the posed one (see below), then transform it to a world
       AABB (Arvo's method, 18 mults). A `waves` surface under the scene's
       ocean grows by as far as the sea carries water
       (`OceanSurface::farthestReach`, [rendering.md](rendering.md#the-ocean)).
     - Compare the object's new columns with last frame's, which are still in
       the arrays, and mark it changed when they differ.
     - Write the object's columns - model, world AABB, chosen mesh, material,
       its rig's bone range, entity - for EVERY valid mesh (not just
       camera-visible ones), so the serial scene-wide gather below reaches
       off-screen occluders and what an offline capture sees. This is why the
       arrays are sized to the full Mesh set, and it is the only time the
       frame writes a matrix: every later reader indexes these.
     - Math::frustumIntersectsAABB    - reject if fully outside frustum.
     - Culling::isNearEnough          - reject if too far from camera.
     - Culling::isLargeEnough         - reject if projected size below minPixels.
     - Set the state bits: drawn, casts - never for a `waves` surface under
       an ocean, whose held shadow would picture one moment of it - and
       visible for survivors. Through a scene camera, `Mesh::seenBy` takes
       its say: `AllButEye` (a first-person body) is never visible but still
       drawn and cast, `EyeOnly` (arms, a weapon) neither drawn nor cast, so
       only the eye has it, and `Viewmodel` as `EyeOnly`, flagged
       `ObjectDraw::viewmodel` and culled against its own frustum, so a
       weapon held nearer than the world's near plane is still seen
       ([rendering.md](rendering.md#the-viewmodel)). A host's free view, the
       editor's, is no eye and sees each as any mesh.
  5. Serial gather: walk the state in Mesh order, appending each visible
     index to `objects.visible`, each caster's to `objects.scene` and each
     changed one's to `objects.changed`, count the casters into
     `casterCount`, then walk again appending the non-casters' indices.
  6. Set FrameContext.visibility to point at the persistent result.
```

`Visibility.camera` is the resolved view as a `CameraData`, depth of field
included, so nothing downstream looks the camera back up or derives its
matrices again.
An entity's pose comes from its `WorldTransform` when it has one, so a camera
parented to a rig renders from where the rig puts it.

### Where the view comes from

Two sources, and the frame says which:

- **A host's view.** An authoring host publishes `FrameContext::hostView`
  before the Visibility stage - the editor's `CameraControllerSystem` does, at
  the Input stage. A `HostView` is either a free view (a `Camera` for the
  projection, a position and a rotation) or, when `through` names an entity, a
  scene camera to look through, active or not, rendered exactly as the active
  one would be. It is the editor's viewpoint in Edit mode and in an ejected
  session, and a scene camera the author picked to preview.
- **The scene's active camera**, when `hostView` is null - which is every frame
  of a runtime, and every frame of an editor session that shows the game.

Null is an answer, not a missed publish: the producer sets it only on frames it
has a view to offer, and `FrameContext` is rebuilt each frame, so the frame
nobody offers one renders through the game's camera again with nothing to
clear. `Visibility::cameraEntity` names the scene camera the frame went
through, and is empty for a free view - it is how the editor knows which camera
not to draw a frustum on or drag a gizmo across. Everything downstream - the cull, LOD,
shadow cascades, fog, reflections, depth of field - reads `Visibility`, so it
follows the view without knowing whose it is. The audio listener does not: it
is an entity of its own (`AudioListener`), so an editor looking around a
running game hears what the game's ear hears.

A scene with no active camera renders nothing in a runtime and logs "No active
camera found for visibility" once per gap. A host's free view needs no scene
camera, which is what lets the editor open and edit a scene that has none.

## Output

```cpp
struct ObjectDraw {
    MeshHandle     mesh;          // geometry to draw - the LOD-selected one, if any
    MaterialHandle material;      // the Mesh component's, so the render path looks nothing up
    uint32_t       skinCount = 0; // bones in its rig's palette slice; 0 = drawn static
    bool           waves = false; // its surface is the scene's ocean
    bool           viewmodel = false; // drawn through the camera's viewmodel projection
};

struct RenderObjects {                  // system/render/data/render_objects.h
    std::vector<glm::mat4>  models;     // per object: world matrix
    std::vector<Math::AABB> bounds;     // per object: world AABB, posed when posed
    std::vector<ObjectDraw> draws;      // per object: what it is drawn with
    std::vector<uint32_t>   skinFirst;  // per object: first bone in the palette
    std::vector<uint32_t>   visible;    // what the camera sees, in object order
    std::vector<uint32_t>   scene;      // every drawn object, casters first
    uint32_t                casterCount = 0;  // scene[0, casterCount) name casters
    std::vector<uint32_t>   changed;    // objects whose columns differ from last frame's
    bool                    allChanged = true;  // every object may differ; `changed` means nothing
    uint64_t                serial = 0;         // frames the cull wrote; `changed` is against the last
};

struct Visibility {
    RenderObjects         objects;
    std::vector<EntityId> entities;     // per object: the editor's picking and outlines read it
    glm::mat4 view           = glm::mat4(1.0f);
    glm::mat4 projection     = glm::mat4(1.0f);
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    float     focusDistance  = 0.0f;    // depth of field: distance held in focus
    float     dofAmount      = 0.0f;    // depth of field strength (0 = off)
    float     dofMaxBlur     = 0.0f;    // depth of field: widest blur, a fraction of the viewport's height
    bool      hasCamera      = false;
    EntityId  camera{};                 // the scene camera rendered through; empty for a host's free view
};
```

`Visibility` is owned by `VisibilitySystem` and reused across frames.
Vectors are `clear()`ed but keep their capacity to avoid per-frame
allocation.

Note: `objects.scene` is gathered here but **not** camera-culled - the
shadow pass must draw geometry outside the camera frustum but inside a
light's volume, and a reflection probe or irradiance capture sees what is
behind the camera. `VisibilitySystem` fills it from the full Mesh set, the
shadow casters first and counted, and `RenderView::build` lends the whole of
`objects` to the backend rather than copying it: the shadow plan reads the
caster prefix and never touches a mesh that casts nothing, and every pass draws
an index list into the model matrices the backend uploads once. See
[Rendering](rendering.md).

## Fixed meshes

A mesh with `Mesh::movable` off promises that it never moves, changes or hides
while the world runs - scenery. Its columns are worked out once and kept: on
later frames the cull reads only its kept box for the frustum, distance and size
tests, and an LOD component still picks its level from the camera. On
`examples/city_block`'s 63,000 props that took the cull from 2.06 to 0.64 ms a
call and, with the backend reading the change list below, the frame from 5.7 to
2.7 ms.

What is kept goes only as far as what changed. Each object is keyed by its Mesh's
stamp (`SparseSet::stampAt`), so spawning or destroying an entity reworks only
the indices that now hold another Mesh. An entity gaining or losing a Transform,
WorldTransform or LOD is checked against the meshes kept for it. Everything kept
is worked out again when a source every object reads moves
(`VisibilitySystem::ObjectSources`): a replaced world or asset graph, a mesh
asset changed (`ResourceManager::revisionOf`), a material changed while an
ocean moves water, or the view switching to or from a scene camera. A host that
may edit the world (`HostChrome::setAuthoring`) keeps nothing.

A promise broken anyway is caught: each frame one in `FIXED_CHECK_STRIDE` (64)
kept meshes is worked out again and held to what was kept; a mismatch logs a
warning once a world and rebuilds everything the next frame, so a moved fixed
mesh is drawn where it went within about a second. A posed mesh, a `waves`
surface under an ocean and a viewmodel are never kept, whatever the flag.

Every object's new columns are compared with last frame's, fixed or not, and the
differing ones listed in `RenderObjects::changed` (`allChanged` when the world or
the asset graph was replaced, the frame before published none, or a fixed object
moved). The backend keeps copies - the instance matrices in video memory, its
assets, the shadow casters' keys - and refreshes from the list alone, while it
saw the frame before (`RenderObjects::serial`); see
[rendering.md](rendering.md#opengl-backend).

## Level of detail

An entity with an `LOD` component (`ecs/component/render/lod.h`) draws coarser
geometry as it recedes. Selection happens inside the cull rather than in a
pass of its own: the cull already has the camera distance and already runs in
parallel. It costs a storage lookup per mesh, and a distance and a walk of the
levels for an entity with an `LOD`; it runs for every mesh rather than only the
visible ones, because the scene-wide list the shadow pass draws takes the same
level.

```cpp
struct LODLevel { MeshHandle mesh; float maxDistance; };
struct LOD      { std::vector<LODLevel> levels; float bias = 1.0f; };
```

Levels are ordered near to far and matched against `distance * scale <=
maxDistance * bias`, where `scale` is `LOD::REFERENCE_P11` over the view's
`projection[1][1]`: a level's range is set for a 60-degree vertical field of
view, and a narrower one magnifies, so a zoomed view holds each level further
out (an orthographic view keeps the reference). `bias` is the global quality
knob. Past the last level the last level keeps drawing - making something
vanish is `Culling::isNearEnough`'s job, and two components able to do it would
make it ambiguous which one did.

The chosen handle is published as `ObjectDraw::mesh`, so the render path
never looks at the `LOD` component. An entity without one publishes its `Mesh`
handle unchanged.

Levels can be authored by hand or generated: `generateLOD` (`src/tools/cook/
lod_generator.h`, exposed as **Generate Levels** on the inspector's LOD card)
simplifies the source mesh to half its triangles, then a quarter of that per
level (meshoptimizer's edge collapse, `decimateMesh` in
`cook/mesh_processing.h`), registers each level as a named asset
(`<mesh>:lod1`) so it serializes like any other, and drops a level that
simplification could not usefully coarsen. Each level hands over to the next
where the next one's error - how far simplification moved its surface, the
simplifier's own measure scaled by the mesh's extent - projects to about a
pixel of a 1080-line image through a 60-degree field of view; the last level's
range is twice the hand-over into it, since past it the last level keeps
drawing. The error is in the mesh's own units, so a bigger mesh hands over
further out; the cull answers a different field of view, and `bias` a scaled
entity or a taller window. For geometry the engine generated itself, re-tessellating at
a lower resolution beats decimating it.

## Culling stages

### Frustum culling (`Math::frustumIntersectsAABB`)

Tests the world-space AABB against six frustum planes extracted from the
view-projection matrix. For each plane it uses a center + projected-half-extent
test: the signed distance from the AABB center to the plane plus the box radius
projected onto the (absolute) plane normal. If `dist + radius < 0` on any plane
the box is fully outside and the entity is culled. This is branchless (no
per-corner selects) thanks to the pre-computed `absNormals`.

### Distance culling (`Culling::isNearEnough`)

Squared distance from the AABB center to the camera. Rejects entities beyond
`maxDistance` (`RenderSettings::cullMaxDistance`). Disabled when
`maxDistance <= 0`.

### Screen-size culling (`Culling::isLargeEnough`)

Projects the bounding sphere radius into screen space using
`(radius * proj[1][1]) / depth * viewportHeight`. Rejects entities smaller
than `minPixels` (`RenderSettings::cullMinPixels`). Uses a pre-computed squared
threshold for a sqrt-free comparison.

### Occlusion culling, on the GPU, as an option

Nothing here culls by occlusion. With `RenderSettings::occlusionCulling` on, the
backend culls the opaque batch on the GPU after this system's cull has built it
(`GLOcclusionCull`, run by the depth prepass), in two phases: the prepass draws
first what the last frame's cull showed, then every instance's box is tested
against a farthest-depth pyramid of what that drew, and what it shows that was
hidden is drawn next. The forward pass draws what the second test showed. The
image is the same with it on or off - the render suite draws every golden both
ways - and the last frame's verdicts only choose what draws first. One thing it
does not keep is the order of a run's instances, which the GPU counts in as it
finds them: two instances of one mesh overlapping in one plane, the same depth
at every pixel, may swap which shows from frame to frame.

After the depth prepass the forward pass already rejects a hidden fragment at
the depth test, so all the cull can save is the GPU's vertex work on hidden
instances. That shows in the frame only where the GPU is what bounds it and much
of the scene is hidden behind the rest - `examples/city_block`, a city walked
at street level, takes about a third off its forward pass, though its frame is
bound by the CPU - and costs the frame in an open scene, so it is off by
default. See [engine.md](../guides/engine.md#4-what-has-already-been-decided).

## Posed bounds

A skinned mesh's stored bounds describe its bind pose, and a posed character
leaves that box - an arm goes up, a leg swings out. That is not a
cosmetic problem: the frustum cull and the shadow cull keep exactly what the
box says, so an under-sized box does not over-draw, it deletes geometry that
was visible. A character would vanish, or lose its shadow, for raising an arm.

So a skinned mesh with a pose this frame is bounded by the pose instead:

```cpp
const PoseSlice* slice = ctx.poses->sliceOf(ctx.scene.entityAt(entityIdx));
const glm::vec3 pad(mesh.skinRadius * slice->maxBoneScale);
localMin = slice->originMin - pad;
localMax = slice->originMax + pad;
```

The pose publishes only what *it* knows - the box of
the posed bone origins in rig space, and the largest scale any bone carries -
because skin hangs off a bone by a distance no pose can see. `VisibilitySystem`
already has the mesh in hand, so it supplies the rest: `MeshAsset::skinRadius`
is the furthest a vertex sits from a bone that moves it, computed by
`MeshAsset::computeAndSetSkinRadius` - which the importer calls, and which is the
one implementation anything authoring a skinned mesh has to call too. The
scale multiplies the radius, because a bone scaled 2x stretches its skin twice
as far from the joint; it is floored at 1, so the product only ever inflates.

The result is exact while the bind transform between mesh space and rig space is
rigid, which it is in every real rig, and conservative otherwise.

Unskinned meshes, and skinned ones with no rig above them, take the stored
bounds unchanged - which is the right box for both, since neither moves, but
for the sea's own surface, grown as step 4 says.

## AABB helpers

`core/math/bounds.h` exposes two helpers used inside the culling loop:

- `Math::transform` takes a `Math::AABB` from local to world space in 18
  multiplications (Arvo) instead of 128 (transform all 8 corners and
  refit).
- `AABB::valid()` rejects degenerate zero-size bounds with
  `dot(extent, extent) > BOUNDS_EPSILON_SQ`.

## Configuration

The two thresholds are `cullMinPixels` and `cullMaxDistance` on
`RenderSettings` (beside `occlusionCulling`, which the backend reads), read off `FrameContext::render` each frame - the engine's one
copy, which the host seeds from the project's render block and the editor's
Culling card or a game's settings screen edits live:

```cpp
engine.getRenderSettings() = project.render;   // what the host does on open
ctx.render.cullMinPixels   = 3.0f;             // what a live edit does
ctx.render.cullMaxDistance = 500.0f;
```

The system holds no copy of its own, so there is nothing to push and nothing
to drift.

## Parallelism

The cull loop uses the free `parallelFor()` over persistent flat
per-index arrays sized to the full Mesh count (a state byte, plus the columns
of `Visibility.objects` and `Visibility.entities`). Each worker writes only its
own indices, so there is zero contention and no atomics. After the parallel
region a serial gather walks the state in `Mesh` storage order, appending
indices to `objects.visible` and `objects.scene` - so both lists stay ordered by
`Mesh` storage. The arrays are `resize`d (not reallocated) each frame, reusing
capacity.

The camera is held, not hinted: the one already in use stays in use while it
is alive, active and posed, even when a lower-slot camera turns active beside
it, so the view does not jump. Only when it is destroyed, deactivated or loses
its `Transform` - or the world is replaced (`Scene::epoch`), whose slots a
stranger may now hold - does the lookup scan for the lowest-slot active one. A
host's view skips the lookup altogether.
