# Visibility System

`VisibilitySystem` determines which entities are visible each frame
through a multi-stage culling pipeline. Results land in
`FrameContext.visibility` and are consumed by `RenderSystem` (which
builds the per-frame `RenderView`) and by the editor's picking and
overlays.

## Key files

- `src/engine/system/visibility/visibility_system.h` for `VisibilitySystem`
- `src/engine/system/visibility/visibility.h` for the `Visibility` result struct
- `src/engine/system/visibility/visibility_context.h` for the per-frame culling parameters
- `src/engine/core/math/bounds.h` for the AABB helpers
- `src/engine/system/visibility/culling/` for the individual cullers

## Pipeline

```
VisibilitySystem::update(ctx)
  1. Find the active camera (cached EntityId; full scan only on miss).
  2. Build VisibilityContext: frustum planes, camera position and view
     matrix, and the thresholds (pre-squared for the sqrt-free tests).
  3. Resize the persistent flat per-index arrays to the full Mesh count
     (visible flags, caster flags, model matrices, world AABBs).
  4. parallelFor over all Mesh entities (each worker writes disjoint indices):
     - Skip if !visible, no mesh, no Transform, or degenerate bounds.
     - Resolve the world matrix inline: read WorldTransform if present, else
       compute from the local Transform (HierarchySystem already ran this stage).
     - Compute world AABB (Arvo's method, 18 mults).
     - Cache the matrix, AABB, and castShadows flag for EVERY valid mesh
       (not just camera-visible ones) so the serial caster gather below can
       reach off-screen occluders. This is why the arrays are sized to the
       full Mesh set.
     - FrustumCuller::isVisible       - reject if fully outside frustum.
     - DistanceCuller::isVisible      - reject if too far from camera.
     - ScreenSizeCuller::isVisible    - reject if projected size below minPixels.
     - Set the visible flag for survivors.
  5. Serial gather: walk the flat arrays in Mesh order, pushing visible
     entries into Visibility.entries and shadow casters into
     Visibility.shadowCasters.
  6. Set FrameContext.visibility to point at the persistent result.
```

`Visibility.view`, `projection`, and `cameraPosition` are filled from the
active camera so downstream consumers do not have to look the camera back up.
The pose comes from the camera's `WorldTransform` when it has one, so a camera
parented to a rig renders from where the rig puts it.

## Output

```cpp
struct VisibleEntity {
    EntityId   id;
    glm::mat4  model;        // pre-computed world matrix
    glm::vec3  worldMin;     // cached world-space AABB for debug/picking
    glm::vec3  worldMax;
    MeshHandle mesh;         // geometry to draw - the LOD-selected one, if any
};

struct Visibility {
    std::vector<VisibleEntity> entries;
    std::vector<VisibleEntity> shadowCasters;   // full scene, not camera-culled
    glm::mat4 view           = glm::mat4(1.0f);
    glm::mat4 projection     = glm::mat4(1.0f);
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    float     focusDistance  = 10.0f;   // depth of field: distance held in focus
    float     dofAmount      = 0.0f;    // depth of field strength (0 = off)
    bool      hasCamera      = false;
};
```

`Visibility` is owned by `VisibilitySystem` and reused across frames.
Vectors are `clear()`ed but keep their capacity to avoid per-frame
allocation.

Note: `shadowCasters` are gathered here but **not** camera-culled - the
shadow pass must draw geometry outside the camera frustum but inside a
light's volume. `VisibilitySystem` fills `Visibility.shadowCasters` from the
full Mesh set, and `RenderView::build` copies it into the view. See
[Rendering](rendering.md).

## Level of detail

An entity with an `LOD` component (`ecs/component/lod.h`) draws coarser
geometry as it recedes. Selection happens inside the cull rather than in a
pass of its own: the cull already has the camera distance and already runs in
parallel, so LOD costs one comparison per surviving entity.

```cpp
struct LODLevel { MeshHandle mesh; float maxDistance; };
struct LOD      { std::vector<LODLevel> levels; float bias = 1.0f; };
```

Levels are ordered near to far and matched against `distance <= maxDistance *
bias`; `bias` is the global quality knob. Past the last level the last level
keeps drawing - making something vanish is `DistanceCuller`'s job, and two
components able to do it would make it ambiguous which one did.

The chosen handle is published as `VisibleEntity::mesh`, so the render path
never looks at the `LOD` component. An entity without one publishes its `Mesh`
handle unchanged.

Levels can be authored by hand or generated: `generateLOD` (`tools/generator/
lod_generator.h`, exposed as **Generate Levels** on the inspector's LOD card)
decimates the source mesh, registers each level as a named asset
(`<mesh>:lod1`) so it serializes like any other, and drops a level that
decimation could not usefully coarsen. For geometry the engine generated
itself, re-tessellating at a lower resolution beats decimating it.

## Culling stages

### Frustum culling (`frustum_culler.h`)

Tests the world-space AABB against six frustum planes extracted from the
view-projection matrix. For each plane it uses a center + projected-half-extent
test: the signed distance from the AABB center to the plane plus the box radius
projected onto the (absolute) plane normal. If `dist + radius < 0` on any plane
the box is fully outside and the entity is culled. This is branchless (no
per-corner selects) thanks to the pre-computed `absNormals`.

### Distance culling (`distance_culler.h`)

Squared distance from the AABB center to the camera. Rejects entities
beyond `maxDistance` (default 500 units). Disabled when `maxDistance <= 0`.

### Screen-size culling (`screen_size_culler.h`)

Projects the bounding sphere radius into screen space using
`(radius * proj[1][1]) / depth * viewportHeight`. Rejects entities
smaller than `minPixels` (default 3). Uses a pre-computed squared
threshold for a sqrt-free comparison.

### Occlusion culling (GPU)

Not a stage here. Occlusion is resolved on the GPU, after the depth prepass has
laid down the frame's opaque depth: `GLHiZPass` reduces that into a
hierarchical depth pyramid and `GLOcclusionCullPass` tests every opaque
instance's world AABB against it, so what the forward pass draws is only what
can be seen. See [Rendering](rendering.md).

It sits there rather than here for two reasons. The depth it needs does not
exist until the prepass has run, which is after this system has finished; and
the answer is wanted by the draw, not by the cull, so keeping it on the GPU
avoids a readback that would stall the frame it is meant to speed up.

That also means occlusion needs no authoring. An earlier CPU implementation
rasterized entities marked with an `Occluder` component into a small software
depth buffer - a well-established technique, and what Godot and Unreal's mobile
path still do - but it could only ever see the geometry someone had remembered
to mark. The pyramid is built from the real depth buffer, so every opaque
surface occludes, and the component is gone.

## AABB helpers

`core/math/bounds.h` exposes two helpers used inside the culling loop:

- `localToWorldAABB` transforms a local AABB to world space in 18
  multiplications (Arvo) instead of 128 (transform all 8 corners and
  refit).
- `hasValidBounds` rejects degenerate zero-size bounds with
  `dot(extent, extent) > epsilon`.

## Configuration

```cpp
auto& vis = engine.addSystem<VisibilitySystem>(SystemStage::Visibility);
vis.setSettings({ .minPixels = 3.0f, .maxDistance = 500.0f });
```

Thresholds live in a nested `Settings` struct read/written through
`getSettings()` / `setSettings(const Settings&)`.

## Parallelism

The cull loop uses `ThreadPool::parallelFor()` over persistent flat
per-index arrays sized to the full Mesh count (visible/caster flags,
model matrices, world AABBs). Each worker writes only its own indices, so
there is zero contention and no atomics. After the parallel region a
serial gather walks those arrays in `Mesh` storage order, pushing visible
entries and shadow casters into the result vectors - so the final
`Visibility.entries` stays contiguous and ordered by `Mesh` storage. The
arrays are `resize`d (not reallocated) each frame, reusing capacity.

Camera lookup uses a cached `EntityId` for the O(1) fast path; it falls
back to a full `forEach<Camera, Transform>` only when the cache misses
(camera created/destroyed/changed).
