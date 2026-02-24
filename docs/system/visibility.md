# Visibility System

The VisibilitySystem determines which entities are visible each frame through a multi-stage culling pipeline. Results are consumed by RenderSystem and AnimationSystem.

## Key Files

- `src/engine/system/visibility/visibility_system.h` -- VisibilitySystem
- `src/engine/system/visibility/visibility.h` -- Visibility result struct
- `src/engine/system/visibility/visibility_context.h` -- Per-frame culling parameters
- `src/engine/system/visibility/bounds_utils.h` -- AABB utilities
- `src/engine/system/visibility/culling/` -- Individual cullers

## Pipeline

```
VisibilitySystem::update()
  |-- Find active camera (cached entity, fallback to full scan)
  |-- Build VisibilityContext (frustum planes, thresholds)
  |-- Pre-compute world matrices for parented entities
  |-- parallelFor over all Mesh entities:
  |     |-- Skip if !visible or no Transform
  |     |-- Compute world AABB (Arvo's method)
  |     |-- FrustumCuller::isVisible()    -- reject if fully outside frustum
  |     |-- DistanceCuller::isVisible()   -- reject if too far from camera
  |     |-- ScreenSizeCuller::isVisible() -- reject if projected size < minPixels
  |     |-- Push survivors to per-worker buffer
  |-- Merge per-worker results into Visibility
  |-- Set FrameContext.visibility
```

## Output

```cpp
struct VisibleEntity {
    EntityId id;
    glm::mat4 model;  // pre-computed world matrix
};

struct Visibility {
    std::vector<VisibleEntity> entries;
    glm::mat4 view, projection;
    glm::vec3 cameraPosition;
    bool hasCamera;
};
```

The `Visibility` struct is persistent across frames (owned by VisibilitySystem). Vectors are cleared but keep capacity to avoid per-frame allocation.

## Culling Stages

### Frustum Culling (`frustum_culler.h`)

Tests world-space AABB against 6 frustum planes extracted from the view-projection matrix. For each plane, computes the "positive vertex" (furthest point along the plane normal). If any plane rejects the AABB, the entity is culled.

### Distance Culling (`distance_culling.h`)

Computes squared distance from AABB center to camera position. Rejects entities beyond `maxDistance` (default 500 units). Disabled when `maxDistance <= 0`.

### Screen-Size Culling (`screen_size_culling.h`)

Projects the bounding sphere radius to screen space: `(radius * proj[1][1]) / depth * viewportHeight`. Rejects entities smaller than `minPixels` (default 3). Uses a pre-computed squared threshold for sqrt-free comparison.

### Occlusion Culling (`occlusion_culler.h`)

Placeholder -- always returns `true`. Reserved for future Hi-Z or software depth implementation.

## AABB Utilities

### `localToWorldAABB` (Arvo's method)

Transforms a local-space AABB to world space using only 18 multiplications (vs 128 for transforming all 8 corners). Works by projecting each matrix column onto the extent.

### `hasValidBounds`

Checks that `dot(extent, extent) > epsilon` to reject degenerate zero-size bounds.

## Configuration

```cpp
VisibilitySystem& vis = engine.addSystem<VisibilitySystem>();
vis.setMinPixels(3.0f);    // screen-size threshold
vis.setMaxDistance(500.0f); // distance culling radius
```

## Parallelism

The main culling loop uses `ThreadPool::parallelFor()` with per-worker scratch buffers. Results are merged with `memcpy` offset writes after all workers complete.

Camera entity lookup uses a cached `EntityId` for O(1) fast path, falling back to a full `forEach<Camera, Transform>` scan on cache miss.
