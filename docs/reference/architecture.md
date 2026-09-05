# Architecture

The one page to read first, and the one to come back to. It starts at what the
engine is and ends at the patterns it is built from; each subsystem has a deeper
doc under [system/](system/).

## Build and run

```bash
git submodule update --init --recursive
cmake -B build -G Ninja
cmake --build build
./build/bin/vkm_editor examples/potion_runner    # edit a project
./build/bin/vkm_runtime examples/potion_runner   # play it
```

CMake 3.25+, Ninja, C++17, OpenGL 4.3 core. The build produces four executables
- `vkm_editor`, `vkm_runtime`, and the headless `vkm_cook` and `vkm_server` -
over a shared header-only bootstrap (`setupEngineApp` in `app/engine_app.h`).
See [building.md](building.md) for targets, modules, and flags.

## The engine runs projects

The engine holds no game of its own. A game is a **project**: a directory with a
`project.json`, its own scenes, assets, and gameplay code built into its own
`bin/`. Every executable finds one by the same rule - *the project beside the
executable, unless an argument names a different one* - so a shipped game ships
its exe next to its `project.json` and the player passes nothing.

Three roots keep the halves apart: `engineRoot()` for what ships with the engine
(shaders, default font, icons; read-only to a game), `projectRoot()` for what the
game owns, and `userRoot()` for how one person likes their tools - the editor's
recent-projects list and window layout, which follow the user rather than either
of the other two. `examples/potion_runner` and `examples/stress_arena` are
complete worked examples. See [system/io.md](system/io.md#projects-and-the-three-roots).

## Overview

vkmEngine is built around an open, type-erased ECS and a stage-based system
pipeline. The `Engine` class owns the `Scene`, `ResourceManager`, `WindowManager`,
the `Clock`, the `EventBus`, the `InputMap`, the `NetSession`, and a per-stage
list of systems. Each
frame, stages run in declaration order; **within a stage, systems run sequentially
in registration order.** There is no parallel layer scheduler - per-system data parallelism (via
`ThreadPool`) is the scaling lever, not framework-level system parallelism.

There is no `Engine::get()` singleton. Engine is stack-constructible, so tests and
headless tools spin up their own instance. Singletons are limited to
process-wide registries reached via a static `get()`. There are five, and the
code is the list rather than this sentence -
`grep -rn "static .*& get()" src/` names them: `ThreadPool`, `AsyncLoadQueue`,
`BehaviorRegistry`, `AssetLibrary` and `NetSchema`. (Asset construction goes through the
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
  NetSession        (the wire; brackets the frame, offline until told otherwise)
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
| Input      | `CameraControllerSystem` (**editor binary only** - the fly controls are an authoring tool, so the authoring host is what registers them) |
| Simulation | (EventBus flush), `SplashSystem` (the startup logo; frame-clock presentation, publishes `ctx.splash`), `AsyncLoaderSystem`, `BehaviorSystem`, `AnimationSystem`, `SkeletalAnimationSystem`, `ParticleSystem`, `PhysicsSystem`, `CharacterControllerSystem`, `SkySystem` |
| Transform  | `BoneSocketSystem`, `HierarchySystem`, `UISystem` (the game UI; runs in **both** binaries), `AudioSystem` (after the world resolve it reads poses from) |
| Visibility | `VisibilitySystem`                                                            |
| Render     | `RenderSystem`                                                                |
| UI         | `EditorSystem` (editor binary only)                                           |

Place a new system by responsibility and let stage order schedule it - see
[../guides/engine.md](../guides/engine.md#absolutes).

## fixedUpdate

`System::fixedUpdate(FrameContext&)` runs on an accumulator clocked at the
project's tick rate (`project.json`'s `tickRate`, 64 by default), clamped at a
max accumulator (0.25 s) to prevent the spiral-of-death after a frame hitch.
The cap is a duration rather than a tick count, so a project that raises its
rate buys more ticks per hitch rather than a longer stall. It is the
deterministic-simulation hook (physics, networking tick); take the step length
from `ctx.clock.getFixedStep()`, never from the frame delta.
`System::hasFixedUpdate()` declares that a system has a real fixedUpdate body,
and the loop calls `fixedUpdate()` only on the systems that answer true.

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
    NetSession&      net;
    HostChrome&      chrome;   // what an authoring host says about the frame

    const Visibility* visibility = nullptr;  // VisibilitySystem's culling result
    const PoseBuffer* poses      = nullptr;  // SkeletalAnimationSystem's rig poses
    const UIDrawData* ui         = nullptr;  // UISystem's draw list
    const SplashFrame* splash    = nullptr;  // SplashSystem's logo + fade
};
```

Time is read off the clock, not the context: `ctx.clock.getDeltaTime()` is real
elapsed seconds (input, camera, UI, file watching), `getSimDelta()` is that delta
scaled by play state (0 while paused, exactly one step while single-stepping), and
`getFixedStep()` is the project's tick length, to use in `fixedUpdate()`.
Simulation systems read the sim delta so pause, time-scale, and single-step
apply uniformly; anything that must advance regardless of play state reads the
real delta. A system reads the timeline its responsibility lives on, so
`AudioSystem` runs every frame whether or not the simulation advanced - pausing
a game must not cut its music
(see [Audio](system/audio.md#time-pause-and-the-editor)).

Gameplay gets the same split rather than a choice of system: a behavior's
`onUpdate` is simulation time and does not run at all while paused, and its
`onRealtimeUpdate` is real time and runs every frame regardless - so a pause menu
can animate, and unpause, over a frozen world (see
[Scripting](system/scripting.md#time-and-pause)).

The context is rebuilt from scratch each frame and the fixed-step loop runs before
any producer stage, so `visibility`, `poses` and `ui` are always null inside
`fixedUpdate()` - read products from `update()` only.

## Engine config constants

Cross-cutting compile-time limits live in `core/engine_config.h` (treat it as the
source of truth for exact names/values): `MAX_LIGHTS = 256`;
`MAX_SHADOW_CASTERS_2D = 6` 2D atlas tiles (4 reserved for the first directional
light's CSM cascades via `NUM_CASCADES`) + `MAX_SHADOW_CASTERS_CUBE = 2` cube
slots; `DEFAULT_TICK_RATE` (64) with `MIN_TICK_RATE` / `MAX_TICK_RATE` bounding
what a project may ask for, and the `MAX_FRAME_ACCUMULATOR` (0.25 s) cap.
The GL backend writes these into every shader's prelude from the C++ constants
themselves, so a shader uses `MAX_LIGHTS` without declaring it (see
[system/lighting.md](system/lighting.md#limits-and-the-shader-prelude)).
Per-system tunables (cull distance, camera sensitivity) live in a nested
`Settings` struct on the owning system, not here.

## Directory layout

Engine code, single include root `src/engine/`:

| Path                       | Contents                                                                 |
|----------------------------|--------------------------------------------------------------------------|
| `core/`                    | `Engine`, `System`, `FrameContext`, `SystemStage`, `Clock`, `engine_config`, `reflect` |
| `core/math/`               | math helpers (rotation, axes, bounds, frustum, projection, random, easing) |
| `core/memory/`             | `TypeId`, `SparseSet`, `SlotAllocator`, `StorageIndex`                   |
| `ecs/`                     | `Scene`, `EntityId`, `Environment`                                        |
| `ecs/component/core/`      | `Transform`, `WorldTransform`, `Hierarchy`, `Name`                       |
| `ecs/component/render/`    | `Mesh`, `Light`, `Camera`, `Decal`, `LOD`, `ParticleEmitter`, `ReflectionProbe`, `IrradianceVolume` |
| `ecs/component/animation/` | `Animation`, `Animator`, `BoneSocket`                                    |
| `ecs/component/audio/`     | `AudioSource`, `AudioListener`                                           |
| `ecs/component/physics/`   | `Rigidbody`, `Collider`, `CharacterController`                           |
| `ecs/component/ui/`        | `UICanvas`, `UIElement`, `UIImage`, `UIText`, `UIButton`                 |
| `ecs/component/prefab/`    | `PrefabEntity`, `PrefabInstance`                                         |
| `system/animation/`        | `AnimationSystem`, `AnimationTrack`, `SkeletalAnimationSystem`, `PoseBuffer`, `composePose`, `BoneSocketSystem` |
| `system/async/`            | `AsyncLoaderSystem`                                                      |
| `system/audio/`            | `AudioSystem`, `AudioDevice` (the only engine file that includes the audio backend) |
| `system/camera/`           | `CameraControllerSystem`                                                       |
| `core/event/`              | `EventBus` (typed pub/sub; engine-owned infrastructure)                  |
| `net/`                     | `NetSession` and the wire it speaks - schema, snapshots, commands, prediction, interpolation (see [system/networking.md](system/networking.md)). Engine-owned infrastructure like `EventBus`, not a `System` |
| `system/hierarchy/`        | `HierarchySystem`, `HierarchyOperations` (free functions)               |
| `system/physics/`          | `PhysicsSystem`, `CharacterControllerSystem`, `collision/`               |
| `system/render/`           | `RenderSystem`, `RenderBackend`, `RenderView`, `RenderSettings`, `data/` |
| `system/script/`           | `BehaviorSystem`, `Behavior`, `ReflectedBehavior`, `BehaviorRegistry`, `ScriptComponent`, `ScriptModule` (see [system/scripting.md](system/scripting.md)) |
| `system/visibility/`       | `VisibilitySystem`, `Visibility`, `VisibilityContext` (AABB helpers are `core/math/bounds.h`) |
| `system/visibility/culling/` | `FrustumCuller`, `DistanceCuller`, `ScreenSizeCuller`                  |
| `resource/`                | `ResourceManager`, `Resource`, `Handle`, `texture_format`               |
| `resource/asset/`          | `MeshAsset`, `MaterialAsset`, `TextureAsset`, `FontAsset`, `SkeletonAsset`, `AnimationClipAsset`, `AudioClipAsset` |
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
`overlays/`, `gizmo/`, `input/`, `ui/`. Tools (`src/tools/`):
the runtime-safe cooked loaders and `asset_registration.cpp` (the `cooked`/
`inline` runtime factories) build into `vkm_tools`; the heavy importers
(`loader/model_loaders`, `texture_loaders`, `material_loaders`) and the asset
cooker (`cook/`) build into the editor-only `vkm_cook`, so the runtime links
neither Assimp nor the heavy image decode.

Application and gameplay layers sit **outside** the `src/engine/` include root:

| Path                | Contents                                                                  |
|---------------------|---------------------------------------------------------------------------|
| `app/engine_app.h`  | `setupEngineApp`: the shared, header-only bootstrap that registers the default systems and installs the GL backend. It seeds no scene - that is the project's answer, given by `bootProjectScene` after it returns. Both mains include it directly (there is no `EngineApp` library) |
| `app/editor/`       | `vkm_editor` entry point; opens a project and loads its module for hot-reload. Opens anyway when the module or the entry scene is broken - repairing those is what it is for |
| `app/runtime/`      | `vkm_runtime` entry point; opens a project and plays it, or exits non-zero when it cannot ([which conditions](system/io.md#what-each-host-does-when-a-project-will-not-open)) |
| `app/cooker/`       | `vkm_cook` entry point; headless asset cook (no window, no GL, no `Engine`) |
| `examples/`         | complete worked projects (Potion Runner, Stress Arena). Gameplay lives in a project, never in the engine |

## Include conventions

Engine includes use module-qualified paths from `src/engine/`:

```cpp
#include "core/engine.h"
#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
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

## Rendering at a glance

The engine builds a backend-agnostic `RenderView` snapshot each frame and hands
it to a `RenderBackend` through one seam (`init` / `render`). The OpenGL backend
runs a fixed pass list, in the order `GLBackend`'s constructor registers them -
that list is the record, and it is twenty passes ending in the splash. There is
no render-graph abstraction and no shader variant cache.

Real features: five light types including LTC area lights, Forward+ clustered
lighting, CSM + spot + point-cube shadows, IBL (HDR or procedural sky), GTAO with
bent normals, froxel volumetric fog, baked SH irradiance volumes, reflection
probes, projected decals, CPU billboard particles, MSAA, DoF, bloom, and a
screen-space in-game UI (SDF text, buttons). Not present: TAA, SSR, FXAA, motion
blur, lens flare, auto-exposure, contact shadows.

Engine code never includes a `gl_*` header; `MaterialAsset` is the renderer
contract. See [system/rendering.md](system/rendering.md) and
[system/ui.md](system/ui.md).

## Resources

`ResourceManager` owns all assets (`MeshAsset`, `TextureAsset`, `MaterialAsset`,
`FontAsset`, `SkeletonAsset`, `AnimationClipAsset`, `AudioClipAsset`) behind
typed generational `Handle<T>`s. Assets are identified by a unique non-empty
`name`; scene files reference them by name and resolve via `findByName`.
`commit()` bumps a per-resource version so the backend skips unchanged uploads.
See [resources.md](resources.md).

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
