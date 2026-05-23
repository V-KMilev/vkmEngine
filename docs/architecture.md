# Architecture

## Overview

vkmEngine is structured around an ECS core with a sequential system pipeline. The `Engine` singleton owns the `Scene`, `ResourceManager`, and system list. Each frame, systems are updated in registration order via a shared `FrameContext`.

```
Engine (singleton)
  |-- Scene (ECS registry)
  |-- ResourceManager (meshes, textures, materials, shaders)
  |-- WindowManager (GLFW window, input)
  |-- StatisticTracker (frame timing, call counters)
  |-- Systems[] (executed in SystemStage order, then registration order)
        |-- CameraController, FileWatcher  (Input)
        |-- AnimationSystem, EventSystem    (Simulation)
        |-- HierarchySystem                 (Transform)
        |-- VisibilitySystem                (Visibility)
        |-- RenderSystem                    (Render)
        |-- EditorSystem                    (UI)
```

## System Execution Order

Each frame, systems run by SystemStage; within a stage, by registration order:

1. **CameraController** (Input) -- Updates camera transform from user input
2. **FileWatcher** (Input) -- Polls watched files (shaders) and bumps asset versions on change
3. **AnimationSystem** (Simulation) -- Advances timelines (all entities), applies to transforms (visible only)
4. **EventSystem** (Simulation) -- Drains the event queue
5. **HierarchySystem** (Transform) -- Resolves local Transform + parent link into per-entity WorldTransform
6. **VisibilitySystem** (Visibility) -- Frustum/distance/screen-size culling, populates `FrameContext.visibility`
7. **RenderSystem** (Render) -- Builds `RenderView` from visibility data, drives the RenderGraph
8. **EditorSystem** (UI) -- Processes editor UI and interaction

## FrameContext

The per-frame data bundle passed to every system:

```cpp
struct FrameContext {
    Scene& scene;
    ResourceManager& resources;
    WindowManager& window;
    FrameTracker& frameTracker;
    float deltaTime;
    float fixedDeltaTime;
    uint32_t viewportX, viewportY;
    uint32_t viewportWidth, viewportHeight;
    const Visibility* visibility;  // populated by VisibilitySystem
};
```

## Directory Layout

```
src/
  engine/                         <-- include root (src/engine/)
    core/                         # Engine singleton, System base class
      memory/                     # TypeId, Storage, SparseSet, SlotAllocator
    ecs/                          # Scene, Entity
      component/                  # Transform, Camera, Mesh, Animation, Light, Hierarchy, WorldTransform, Name
    system/                       # All engine systems
      render/                     # RenderSystem, RenderBackend, RenderView, RenderGraph + builder + context + resources, RenderPass, RenderTarget, FrameResources, Environment
      visibility/                 # Visibility, VisibilityContext, BoundsUtils
        culling/                  # FrustumCuller, DistanceCulling, ScreenSizeCulling, OcclusionCuller (stub)
      animation/                  # AnimationSystem, AnimationTrack, Keyframe, Easing
      event/                      # EventSystem (typed pub/sub; subscribe<EventT>/emit<EventT>/enqueue<EventT>)
      hierarchy/                  # HierarchySystem + HierarchyOperations (free functions)
      io/                         # FileWatcher
    resource/                     # ResourceManager, Resource, ResourceHandle, MeshAsset, MaterialAsset, TextureAsset
    platform/
      window/                     # WindowManager, Window, InputHandle, FrameLimiter
      threading/                  # ThreadPool
    debug/                        # Statistics, FrameTracker, CallTracker, FrameInfo, PrintHelper
  editor/                         # EditorSystem, CameraController, panels, gizmo
  backend/opengl/                 # OpenGL backend (flat gl_-prefixed includes within a dir)
    core/                         # GLBackend, GLView, GLInstanceBatcher, GLRenderTarget, GLSceneTarget, GLFrameResources
    resource/                     # GLMesh, GLMaterial, GLTexture, GLLights, GLShaderProgram, GLShadowAtlas/Data, GLIBL, GLBloom, GLAutoExposure, GLGBuffer, GLTAA, GLPostScratch, GLCamera
    config/                       # GLConfig, GLTextureMapping, GLFormatConversion
    pass/                         # GLForwardPass, GLShadowPass, GLPrepass, GLGTAOPass, GLSkyboxPass, GLIBLBakePass, GLAABBDebugPass, GLGridPass, GLSSRPass, GLLensFlarePass, GLTAAPass, GLDofPass, GLMotionBlurPass, GLBloomPass, GLExposurePass, GLCompositePass
  tools/                          <-- include root (src/tools/)
    loader/                       # TextureLoaders, MaterialLoaders
    generator/                    # MeshGenerators, TextureGenerators, MaterialGenerators, LightGenerators
```

## Include Conventions

Engine includes use module-qualified paths from the `src/engine/` root:

```cpp
#include "core/engine.h"
#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "system/render/render_pass.h"
#include "system/visibility/visibility.h"
#include "system/event/event_system.h"
#include "resource/mesh_asset.h"
```

Backend includes are flat (all `gl_`-prefixed):

```cpp
#include "gl_backend.h"
#include "gl_forward_pass.h"
```

Tools includes use their own root:

```cpp
#include "loader/texture_loaders.h"
#include "generator/mesh_generators.h"
```

## Namespaces

- `Engine::` -- All engine code (ECS, systems, components, resources)
- `Core::` -- Low-level OpenGL wrappers from vkmGL (Shader, Context, VertexArray, etc.)

## Key Design Patterns

| Pattern | Where | Purpose |
|---------|-------|---------|
| Generational handles | `StorageIndex` | Prevent use-after-free for entities and resources |
| Sparse-dense dual array | `SparseSet<T>`, `Storage<T>` | O(1) add/remove/lookup + O(n) packed iteration |
| Type erasure + TypeId | `ISparseSet`, `typeId<T>()` | Open component registry without modifying Scene |
| Lazy component creation | `Scene::getStorage<T>()` | SparseSet created on first `add<T>()` call |
| Fold expressions | `forEach<A, B, ...>` | Compile-time multi-component query expansion |
| `if constexpr` dispatch | `ResourceManager` | Type-safe routing to correct Storage |
| Frame-local snapshots | `RenderView` | Capture scene state, avoid concurrent mutation |
| Version-based GPU sync | `Resource.version` + `GLView` | Skip redundant GPU uploads |
| Instanced rendering | `GLInstanceBatcher` | Group by (material, mesh) for single draw call per batch |
| Work-stealing pool | `ThreadPool` | Local LIFO, global FIFO, steal from back |
