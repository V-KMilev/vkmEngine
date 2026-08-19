# Architecture

## Overview

vkmEngine is built around an open, type-erased ECS and a stage-based system
pipeline. The `Engine` class owns the `Scene`, `ResourceManager`, `WindowManager`,
the `Clock`, the `EventBus`, the `InputMap`, and a per-stage list of systems. Each
frame, stages run in declaration order; **within a stage, systems run sequentially
in registration order.** There is no parallel layer scheduler - per-system data parallelism (via
`ThreadPool`) is the scaling lever, not framework-level system parallelism.

There is no `Engine::get()` singleton. Engine is stack-constructible, so tests and
headless tools spin up their own instance. Singletons are limited to a handful of
process-wide registries reached via a static `get()`: `ThreadPool`,
`AsyncLoadQueue`, and `BehaviorRegistry`. (Asset construction goes through the
`AssetFactory` function-pointer seam in `io/asset/asset_factory.h`, not a singleton;
recoverable errors go through the `reportError()` sink, captured by an
editor-owned `EngineErrorLog`.) Profiling goes through `debug/profiler.h` (a
Tracy facade), not an in-engine
statistics registry.

```
Engine
  Scene             (ECS registry, open type-erased)
  ResourceManager   (meshes, materials, textures, fonts)
  WindowManager     (GLFW window, input handle, frame limiter)
  Clock             (real + sim time, play / pause / step / time-scale)
  EventBus          (typed pub/sub, flushed at the top of Simulation)
  InputMap          (named actions, resolved once per frame)
  m_systemsByStage  (one vector per SystemStage)
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
inline bootstrap both executables include and call). `vkm_editor` adds
`EditorSystem` from its own `main()` after that returns; `vkm_runtime` never
does:

| Stage      | Systems                                                                         |
|------------|---------------------------------------------------------------------------------|
| Input      | `CameraControllerSystem`                                                              |
| Simulation | (EventBus flush), `AsyncLoaderSystem`, `BehaviorSystem`, `AnimationSystem`, `ParticleSystem`, `PhysicsSystem`, `SkySystem` |
| Transform  | `HierarchySystem`, `UISystem` (the game UI; runs in **both** binaries)         |
| Visibility | `VisibilitySystem`                                                            |
| Render     | `RenderSystem`                                                                |
| UI         | `EditorSystem` (editor binary only)                                           |

`FileWatcherSystem` is an Input-stage `System` the engine provides but `setupEngineApp`
does not register today (see [system/io.md](system/io.md)).

Place a new system by responsibility and let stage order schedule it - see
[../guides/development.md](../guides/development.md#4-the-seams-you-must-not-cross).

## fixedUpdate

`System::fixedUpdate(FrameContext&)` runs on an accumulator clocked at the fixed
timestep (1/60 s), clamped at a max accumulator (0.25 s) to prevent the
spiral-of-death after a frame hitch. It is the deterministic-simulation hook
(physics, networking tick); take the step length from `ctx.clock.getFixedStep()`,
never from the frame delta. `System::hasFixedUpdate()` declares that a system has
a real fixedUpdate body, and the loop calls `fixedUpdate()` only on the systems
that answer true.

## FrameContext

The per-frame bundle passed to every system (`core/system.h`). The field type says
which kind of state it is: **references are engine-owned services**, valid for the
whole session; **pointers are per-frame products**, null until the stage that
produces them has run:

```cpp
struct FrameContext {
    Scene&           scene;
    ResourceManager& resources;

    Clock&           clock;
    EventBus&        events;
    WindowManager&   window;
    InputMap&        input;

    const Visibility* visibility = nullptr;  // VisibilitySystem's culling result
    const UIDrawData* ui         = nullptr;  // UISystem's draw list
};
```

Time is read off the clock, not the context: `ctx.clock.getDeltaTime()` is real
elapsed seconds (input, camera, UI, file watching), `getSimDelta()` is that delta
scaled by play state (0 while paused, exactly one step while single-stepping), and
`getFixedStep()` is the constant 1/60 to use in `fixedUpdate()`. Simulation systems
read the sim delta so pause, time-scale, and single-step apply uniformly; anything
that must advance regardless of play state reads the real delta.

The context is rebuilt from scratch each frame and the fixed-step loop runs before
any producer stage, so `visibility` and `ui` are always null inside
`fixedUpdate()` - read products from `update()` only.

## Engine config constants

Cross-cutting compile-time limits live in `core/engine_config.h` (treat it as the
source of truth for exact names/values): `MAX_LIGHTS = 256`;
`MAX_SHADOW_CASTERS_2D = 6` 2D atlas tiles (4 reserved for the first directional
light's CSM cascades via `NUM_CASCADES`) + `MAX_SHADOW_CASTERS_CUBE = 2` cube
slots; the `FIXED_TIME_STEP` (1/60) and the `MAX_FRAME_ACCUMULATOR` (0.25 s) cap.
The CMake build generates `shaders/_generated/engine_config.glsl` from this header
so cross-language constants *can* be single-sourced - though the forward shaders
still hand-define their copies today (see
[system/lighting.md](system/lighting.md#limits-and-the-generated-constants-contract)).
Per-system tunables (cull distance, camera sensitivity) live in a nested
`Settings` struct on the owning system, not here.

## Directory layout

Engine code, single include root `src/engine/`:

| Path                       | Contents                                                                 |
|----------------------------|--------------------------------------------------------------------------|
| `core/`                    | `Engine`, `System`, `FrameContext`, `SystemStage`, `Clock`, `engine_config`, `reflect` |
| `core/math/`               | math helpers (rotation, axes, random, easing)                            |
| `core/memory/`             | `TypeId`, `SparseSet`, `SlotAllocator`, `StorageIndex`                   |
| `ecs/`                     | `Scene`, `EntityId`, `Environment`                                        |
| `ecs/component/`           | `Transform`, `WorldTransform`, `Camera`, `Mesh`, `Light`, `Animation`, `Hierarchy`, `Name`, `Collider`, `RigidBody`, `PhysicsWorld`, `ReflectionProbe` |
| `system/animation/`        | `AnimationSystem`, `AnimationTrack`, `Keyframe`                          |
| `system/async/`            | `AsyncLoaderSystem`                                                      |
| `system/camera/`           | `CameraControllerSystem`                                                       |
| `core/event/`              | `EventBus` (typed pub/sub; engine-owned infrastructure)                  |
| `system/hierarchy/`        | `HierarchySystem`, `HierarchyOperations` (free functions)               |
| `system/io/`               | `FileWatcherSystem` (polling hot-reload)                                       |
| `system/physics/`          | `PhysicsSystem`, `collision/`                                            |
| `system/render/`           | `RenderSystem`, `RenderBackend`, `RenderView`, `RenderSettings`, `data/` |
| `system/script/`           | `BehaviorSystem`, `Behavior`, `ReflectedBehavior`, `BehaviorRegistry`, `ScriptComponent`, `ScriptModule` (see [system/scripting.md](system/scripting.md)) |
| `system/visibility/`       | `VisibilitySystem`, `Visibility`, `VisibilityContext`, `BoundsUtils`    |
| `system/visibility/culling/` | `FrustumCuller`, `DistanceCulling`, `ScreenSizeCulling`                |
| `resource/`                | `ResourceManager`, `Resource`, `Handle`, `texture_format`               |
| `resource/asset/`          | `MeshAsset`, `MaterialAsset`, `TextureAsset`, `FontAsset`               |
| `io/`                      | `json_vec`, `project_paths` (shared I/O helpers)                          |
| `io/asset/`                | `AssetLibrary`, `AssetFactory`, `AssetCooker`, `CookedLoader`, `AssetSerializer` |
| `io/scene/`                | `SceneSerializer`, `ComponentSerializer`                                  |
| `platform/window/`         | `WindowManager`, `InputHandle` (raw keyboard/mouse state), `FrameLimiter` |
| `platform/input/`          | `InputMap`, `InputBinding`, `InputSource`, `default_bindings` (the named actions gameplay reads) |
| `platform/threading/`      | `ThreadPool` + `parallelFor` (shared-deque pool, see [threading.md](threading.md)) |
| `platform/library/`        | `DynamicLibrary` (cross-platform `.dll`/`.so` loader for gameplay hot-reload) |
| `debug/`                   | `build_info`, `engine_error_log`, `profiler` (Tracy facade)              |

OpenGL backend, `src/backend/opengl/` (flat `gl_`-prefixed includes):

| Path          | Contents                                                                  |
|---------------|---------------------------------------------------------------------------|
| (top level)   | `GLBackend`, `GLView`, `GLTarget`, `GLPass`, `GLFrameContext`             |
| `convention/` | `gl_bindings` (UBO/sampler contract), `gl_format_conversion`              |
| `data/`       | `GLMesh`, `GLMaterial`, `GLTexture`, `GLLights`, `GLCamera`, `GLShadowAtlas`/`Data`, `GLIBL`, `GLBloom`, probe + preview helpers |
| `pass/`       | the passes: shadow, depth-prepass, resolve (depth + colour scopes), hi-z, occlusion-cull, gtao, skybox, cluster-cull, fog (compute + apply), forward, particle, decal, dof, bloom, grid, composite, ui |

Editor (`src/editor/`): `EditorSystem` at the root; `framework/`, `panels/`,
`overlays/`, `gizmo/`, `input/`, `ui/`. Tools (`src/tools/`): `generator/` plus
the runtime-safe cooked loaders and `asset_registration.cpp` (the `cooked`/
`inline` runtime factories) build into `vkm_tools`; the heavy importers
(`loader/model_loaders`, `texture_loaders`, `material_loaders`) and the asset
cooker (`cook/`) build into the editor-only `vkm_cook`, so the runtime links
neither Assimp nor the heavy image decode.

Application and gameplay layers sit **outside** the `src/engine/` include root:

| Path                | Contents                                                                  |
|---------------------|---------------------------------------------------------------------------|
| `app/engine_app.h`  | `setupEngineApp`: the shared, header-only bootstrap that registers the default systems and installs the GL backend. It seeds no scene - that is the project's answer, given by `bootProjectScene` after it returns. Both mains include it directly (there is no `EngineApp` library) |
| `app/editor/`       | `vkm_editor` entry point; opens a project and loads its module for hot-reload |
| `app/runtime/`      | `vkm_runtime` entry point; opens a project and plays it                 |
| `app/cooker/`       | `vkm_cook` entry point; headless asset cook (no window, no GL, no `Engine`) |
| `examples/`         | complete worked projects (Potion Runner, Stress Arena). Gameplay lives in a project, never in the engine |

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

- `Vkm::Engine::` for all engine code (ECS, systems, components, resources, editor).
- `Vkm::GL::` for low-level OpenGL wrappers from `vkmGL` (`Shader`, `Context`, ...).
- `Vkm::Log::` for `vkmLog` (`Logger`, `LogLevel`). The `LOG_*` and `VKM_ASSERT`
  macros qualify it themselves, so call sites never name it.

## Key design patterns

| Pattern                   | Where                                | Purpose                                                  |
|---------------------------|--------------------------------------|----------------------------------------------------------|
| Generational handles      | `StorageIndex`                       | Prevent use-after-free for entities, resources, slots    |
| Sparse-dense dual array   | `SparseSet<T>`                       | O(1) add/remove/lookup, packed iteration                 |
| Type erasure + TypeId     | `ISparseSet`, `typeId<T>()`          | Open component registry without modifying Scene          |
| Fold expressions          | `forEach<A, B, ...>`                 | Compile-time multi-component query                       |
| `if constexpr` dispatch   | `ResourceManager`                    | Type-safe routing to the correct storage                 |
| Compile-time reflection   | `core/reflect.h` `Field` + `Traits`  | Field iteration driving (de)serialization and inspectors |
| Frame-local snapshot      | `RenderView`                         | Capture scene state for the backend, no shared mutation  |
| Version-based GPU sync    | `Resource::version` + `GLView`       | Skip redundant GPU uploads                               |
| Instanced rendering       | sorted drawables + instance batches  | One draw per (material, mesh) batch                      |
| Shared-deque thread pool  | `ThreadPool` + free `parallelFor`    | Data-parallel loops; main thread participates            |
| Command pattern           | `Command`, `CommandStack`            | Editor undo/redo with drag-coalesce                      |
| Staging-and-swap          | `SceneSerializer::load`              | Transactional scene load; failed loads leave live scene intact |
