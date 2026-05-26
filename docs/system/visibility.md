# Visibility System

`VisibilitySystem` determines which entities are visible each frame
through a multi-stage culling pipeline. Results land in
`FrameContext.visibility` and are consumed by `RenderSystem` (which
builds the per-frame `RenderView`) and `AnimationSystem` (which applies
per-track interpolated values only to visible entities).

## Key files

- `src/engine/system/visibility/visibility_system.h` for `VisibilitySystem`
- `src/engine/system/visibility/visibility.h` for the `Visibility` result struct
- `src/engine/system/visibility/visibility_context.h` for the per-frame culling parameters
- `src/engine/system/visibility/bounds_utils.h` for the AABB helpers
- `src/engine/system/visibility/culling/` for the individual cullers

## Pipeline

```
VisibilitySystem::update(ctx)
  1. Find the active camera (cached EntityId; full scan only on miss).
  2. Build VisibilityContext: frustum planes, viewport dims, thresholds.
  3. Pre-resolve world matrices for parented entities (read WorldTransform if present, else local Transform).
  4. parallelFor over all Mesh entities, per worker:
     - Skip if !visible or no Transform.
     - Compute world AABB (Arvo's method, 18 mults).
     - FrustumCuller::isVisible       - reject if fully outside frustum.
     - DistanceCuller::isVisible      - reject if too far from camera.
     - ScreenSizeCuller::isVisible    - reject if projected size below minPixels.
     - Push the survivor into the worker's local buffer.
  5. Merge per-worker buffers into Visibility.entries.
  6. Set FrameContext.visibility to point at the persistent result.
```

`Visibility.view`, `projection`, `cameraPosition`, and `cameraExposure`
are filled from the active camera so downstream consumers do not have
to look the camera back up.

## Output

```cpp
struct VisibleEntity {
    EntityId  id;
    glm::mat4 model;        // pre-computed world matrix
    glm::vec3 worldMin;     // cached world-space AABB for debug/picking
    glm::vec3 worldMax;
};

struct Visibility {
    std::vector<VisibleEntity> entries;
    glm::mat4 view           = glm::mat4(1.0f);
    glm::mat4 projection     = glm::mat4(1.0f);
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    float     cameraExposure = 1.0f;
    bool      hasCamera      = false;
};
```

`Visibility` is owned by `VisibilitySystem` and reused across frames.
Vectors are `clear()`ed but keep their capacity to avoid per-frame
allocation.

Note: shadow casters are NOT culled here. The shadow pass needs to draw
geometry that is outside the camera frustum but inside a light's volume,
so `RenderView::build` gathers `shadowCasters` from the full Mesh storage
independently. See [Rendering](rendering.md).

## Culling stages

### Frustum culling (`frustum_culler.h`)

Tests the world-space AABB against six frustum planes extracted from the
view-projection matrix. For each plane it computes the "positive vertex"
(furthest along the plane normal); if any plane rejects the AABB, the
entity is culled.

### Distance culling (`distance_culling.h`)

Squared distance from the AABB center to the camera. Rejects entities
beyond `maxDistance` (default 500 units). Disabled when `maxDistance <= 0`.

### Screen-size culling (`screen_size_culling.h`)

Projects the bounding sphere radius into screen space using
`(radius * proj[1][1]) / depth * viewportHeight`. Rejects entities
smaller than `minPixels` (default 3). Uses a pre-computed squared
threshold for a sqrt-free comparison.

### Occlusion culling (`occlusion_culler.h`)

Placeholder; always returns `true`. Reserved for a future Hi-Z or
software-depth implementation.

## AABB helpers

`bounds_utils.h` exposes two helpers used inside the culling loop:

- `localToWorldAABB` transforms a local AABB to world space in 18
  multiplications (Arvo) instead of 128 (transform all 8 corners and
  refit).
- `hasValidBounds` rejects degenerate zero-size bounds with
  `dot(extent, extent) > epsilon`.

## Configuration

```cpp
auto& vis = engine.addSystem<VisibilitySystem>(SystemStage::Visibility);
vis.setMinPixels(3.0f);
vis.setMaxDistance(500.0f);
```

## Parallelism

The cull loop uses `ThreadPool::parallelFor()` with per-worker scratch
buffers. After the parallel region, results are merged with `memcpy`
offset writes so the final `Visibility.entries` is contiguous and
sorted in `Mesh` storage order.

Camera lookup uses a cached `EntityId` for the O(1) fast path; it falls
back to a full `forEach<Camera, Transform>` only when the cache misses
(camera created/destroyed/changed).
