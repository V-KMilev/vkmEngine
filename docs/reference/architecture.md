# Architecture

## Overview

vkmEngine is built around an open, type-erased ECS and a stage-based system
pipeline. The `Engine` class owns the `Scene`, `ResourceManager`, `WindowManager`,
a `SimulationClock`, and a per-stage list of systems. Each frame, stages run in
declaration order; **within a stage, systems run sequentially in registration
order.** There is no parallel layer scheduler - per-system data parallelism (via
`ThreadPool`) is the scaling lever, not framework-level system parallelism.

There is no `Engine::get()` singleton. Engine is stack-constructible, so tests and
headless tools spin up their own instance. Singletons are limited to a handful of
process-wide registries and log sinks reached via a static `get()`: `ThreadPool`,
`AsyncLoadQueue`, `BehaviorRegistry`, `AssetFactories`, and `BehaviorErrorLog`.
Profiling goes through `debug/profiler.h` (a Tracy facade), not an in-engine
statistics registry.

```
Engine
  Scene             (ECS registry, open type-erased)
  ResourceManager   (meshes, materials, textures, shaders)
  WindowManager     (GLFW window, input handle, frame limiter)
  SimulationClock   (play / pause / step / time-scale)
  m_systemsByStage  (one vector per SystemStage)
  m_fixedUpdaters   (opt-in subset of systems with a real fixedUpdate)
```

## SystemStage

Each system is registered at exactly one stage (`core/system.h`):

```cpp
enum class SystemStage : uint8_t {
    Input        = 0,   // poll devices, capture input
    Simulation   = 1,   // events, async loading, scripting, animation, physics
    Transform    = 2,   // local -> world transform resolution
    Visibility   = 3,   // culling
    Render        = 4,   // build RenderView, submit to the backend
    UI           = 5,   // ImGui, editor
    Count
};
```

Default wiring lives in `setupEngineApp` (`app/engine_app.h`, a header-only
inline bootstrap both executables include and call). `engine_editor` adds
`EditorSystem` from its own `main()` after that returns; `engine_runtime` never
does:

| Stage      | Systems                                                                         |
|------------|---------------------------------------------------------------------------------|
| Input      | `CameraController`                                                              |
| Simulation | `EventSystem`, `AsyncLoaderSystem`, `BehaviorSystem`, `AnimationSystem`, `PhysicsSystem` |
| Transform  | `HierarchySystem`                                                              |
| Visibility | `VisibilitySystem`                                                            |
| Render     | `RenderSystem`                                                                |
| UI         | `EditorSystem` (editor binary only)                                           |

`FileWatcher` is an Input-stage `System` the engine provides but `setupEngineApp`
does not register today (see [system/io.md](system/io.md)).

Place a new system by responsibility and let stage order schedule it - see
[../guides/development.md](../guides/development.md#4-the-seams-you-must-not-cross).

## fixedUpdate

`System::fixedUpdate(FrameContext&)` runs on an accumulator clocked at the fixed
timestep (1/60 s), clamped at a max accumulator (0.25 s) to prevent the
spiral-of-death after a frame hitch (the clamp warning is throttled to once per
second). It is the deterministic-simulation hook (physics, networking tick); read
`ctx.fixedDeltaTime`, not `deltaTime`. `System::hasFixedUpdate()` opts a system into
the filtered `m_fixedUpdaters` list so the empty virtual isn't dispatched across
every system every tick.

## FrameContext

The per-frame bundle passed to every system (`core/system.h`). Note the **three**
time deltas:

```cpp
struct FrameContext {
    Scene&            scene;
    ResourceManager&  resources;
    WindowManager&    window;
    FrameTracker&     frameTracker;

    float deltaTime;       // real elapsed seconds (input, camera, UI, file watching)
    float simDeltaTime;    // simulation time: deltaTime scaled by SimulationClock
                           //   (0 while paused, one step while single-stepping)
    float fixedDeltaTime;  // constant fixed-step (1/60), read in fixedUpdate()

    uint32_t viewportX, viewportY, viewportWidth, viewportHeight;

    const Visibility* visibility = nullptr;  // populated by VisibilitySystem;
                                             // null before that stage / first frame
};
```

Simulation systems read `simDeltaTime` so pause, time-scale, and single-step apply
uniformly; anything that must advance regardless of play state (camera, UI) reads
`deltaTime`.

## Engine config constants

Cross-cutting compile-time limits live in `core/engine_config.h` (treat it as the
source of truth for exact names/values): `MAX_LIGHTS = 32`;
`MAX_SHADOW_CASTERS_2D = 6` 2D atlas tiles (4 reserved for the first directional
light's CSM cascades via `NUM_CASCADES`) + `MAX_SHADOW_CASTERS_CUBE = 2` cube
slots; the `FIXED_TIME_STEP` (1/60) and the `MAX_FRAME_ACCUMULATOR` (0.25 s) cap.
The CMake build generates `shaders/_generated/engine_config.glsl` from this header
so cross-language constants *can* be single-sourced - though the forward shaders
still hand-define their copies today (see
[system/lighting.md](system/lighting.md#limits-and-the-must-match-shader-contract)).
Per-system tunables (cull distance, camera sensitivity) live in a nested
`Settings` struct on the owning system, not here.

## Directory layout

Engine code, single include root `src/engine/`:

| Path                       | Contents                                                                 |
|----------------------------|--------------------------------------------------------------------------|
| `core/`                    | `Engine`, `System`, `FrameContext`, `SystemStage`, `SimulationClock`, `engine_config` |
| `core/math/`               | math helpers (rotation, axes)                                            |
| `core/memory/`             | `TypeId`, `SparseSet`, `SlotAllocator`, `StorageIndex`                   |
| `ecs/`                     | `Scene`, `Entity`, `Environment`                                        |
| `ecs/component/`           | `Transform`, `WorldTransform`, `Camera`, `Mesh`, `Light`, `Animation`, `Hierarchy`, `Name`, `Collider`, `RigidBody`, `PhysicsWorld`, `ReflectionProbe` |
| `system/animation/`        | `AnimationSystem`, `AnimationTrack`, `Keyframe`, `Easing`               |
| `system/async/`            | `AsyncLoaderSystem`                                                      |
| `system/camera/`           | `CameraController`                                                       |
| `system/event/`            | `EventSystem` (typed pub/sub)                                            |
| `system/hierarchy/`        | `HierarchySystem`, `HierarchyOperations` (free functions)               |
| `system/io/`               | `FileWatcher` (polling hot-reload)                                       |
| `system/physics/`          | `PhysicsSystem`, `collision/`                                            |
| `system/render/`           | `RenderSystem`, `RenderBackend`, `RenderView`, `RenderSettings`, `data/` |
| `system/script/`           | `BehaviorSystem`, `Behavior`, `ReflectedBehavior`, `BehaviorRegistry`, `ScriptComponent`, `ScriptModule` (see [system/scripting.md](system/scripting.md)) |
| `system/visibility/`       | `VisibilitySystem`, `Visibility`, `VisibilityContext`, `BoundsUtils`    |
| `system/visibility/culling/` | `FrustumCuller`, `DistanceCulling`, `ScreenSizeCulling`                |
| `resource/`                | `ResourceManager`, `Resource`, `Handle`, `texture_format`               |
| `resource/asset/`          | `MeshAsset`, `MaterialAsset`, `TextureAsset`, `ShaderAsset`             |
| `io/`                      | `SceneSerializer`, `AssetSerializer`, `ComponentSerializer`, `reflect.h`, `project_paths` |
| `platform/window/`         | `WindowManager`, `Window`, input handles, `FrameLimiter`                |
| `platform/threading/`      | `ThreadPool`, `Task` (shared-deque pool, see [threading.md](threading.md)) |
| `platform/library/`        | `DynamicLibrary` (cross-platform `.dll`/`.so` loader for gameplay hot-reload) |
| `debug/`                   | `FrameTracker`, `FrameRateInfo`, `profiler` (Tracy facade), error logs   |

OpenGL backend, `src/backend/opengl/` (flat `gl_`-prefixed includes):

| Path          | Contents                                                                  |
|---------------|---------------------------------------------------------------------------|
| (top level)   | `GLBackend`, `GLView`, `GLTarget`, `GLPass`, `GLFrameContext`             |
| `convention/` | `gl_bindings` (UBO/sampler contract), `gl_format_conversion`              |
| `data/`       | `GLMesh`, `GLMaterial`, `GLTexture`, `GLLights`, `GLCamera`, `GLShadowAtlas`/`Data`, `GLIBL`, `GLBloom`, probe + preview helpers |
| `pass/`       | the 10 passes: shadow, depth-prepass, gtao, skybox, forward, ssr, motion-blur, bloom, grid, composite |

Editor (`src/editor/`): `EditorSystem` at the root; `framework/`, `panels/`,
`overlays/`, `gizmo/`, `input/`, `ui/`. Tools (`src/tools/`): `generator/` plus
the runtime-safe cooked loaders and `asset_registration.cpp` (the `cooked`/
`inline` runtime factories) build into `EngineTools`; the heavy importers
(`loader/model_loader`, `texture_loaders`, `material_loaders`) and the asset
cooker (`cook/`) build into the editor-only `EngineCooker`, so the runtime links
neither Assimp nor the heavy image decode.

Application and gameplay layers sit **outside** the `src/engine/` include root:

| Path                | Contents                                                                  |
|---------------------|---------------------------------------------------------------------------|
| `app/engine_app.h`  | `setupEngineApp`: the shared, header-only bootstrap that registers the default systems, installs the GL backend, and seeds the default scene. Both mains include it directly (there is no `EngineApp` library) |
| `app/editor/`       | `engine_editor` entry point; loads the gameplay module for hot-reload      |
| `app/runtime/`      | `engine_runtime` entry point; static-links gameplay, can boot a saved scene |
| `game/`             | concrete gameplay `Behavior`s (the reusable engine is `src/`; `game/` is built on top) |

## Include conventions

Engine includes use module-qualified paths from `src/engine/`:

```cpp
#include "core/engine.h"
#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "system/render/render_view.h"
#include "resource/asset/mesh_asset.h"
```

The backend uses flat `gl_`-prefixed includes; engine code never reaches into it,
seeing only `RenderBackend` through engine headers. Tools use their own root
(`#include "loader/texture_loaders.h"`). Full rules:
[../guides/code-style.md](../guides/code-style.md#1-include-roots-and-include-order).

## Namespaces

- `Engine::` for all engine code (ECS, systems, components, resources, editor).
- `Core::` for low-level OpenGL wrappers from `vkmGL` (`Shader`, `Context`, ...).

## Key design patterns

| Pattern                   | Where                                | Purpose                                                  |
|---------------------------|--------------------------------------|----------------------------------------------------------|
| Generational handles      | `StorageIndex`                       | Prevent use-after-free for entities, resources, slots    |
| Sparse-dense dual array   | `SparseSet<T>`                       | O(1) add/remove/lookup, packed iteration                 |
| Type erasure + TypeId     | `ISparseSet`, `typeId<T>()`          | Open component registry without modifying Scene          |
| Fold expressions          | `forEach<A, B, ...>`                 | Compile-time multi-component query                       |
| `if constexpr` dispatch   | `ResourceManager`                    | Type-safe routing to the correct storage                 |
| Compile-time reflection   | `io/reflect.h` `Field` + `Traits`    | Field iteration driving (de)serialization and inspectors |
| Frame-local snapshot      | `RenderView`                         | Capture scene state for the backend, no shared mutation  |
| Version-based GPU sync    | `Resource::version` + `GLView`       | Skip redundant GPU uploads                               |
| Instanced rendering       | sorted drawables + instance batches  | One draw per (material, mesh) batch                      |
| Shared-deque thread pool  | `ThreadPool` + free `parallelFor`    | Data-parallel loops; main thread participates            |
| Command pattern           | `Command`, `CommandStack`            | Editor undo/redo with drag-coalesce                      |
| Staging-and-swap          | `SceneSerializer::load`              | Transactional scene load; failed loads leave live scene intact |
