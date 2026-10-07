# Architecture

The one page to read first, and the one to come back to. It starts at what the
engine is and ends at the patterns it is built from; each subsystem has a deeper
page beside this one, listed in [the manual's index](../README.md).

## Build and run

```bash
git submodule update --init --recursive
cmake -B build -G Ninja
cmake --build build
./build/bin/vkm_editor examples/potion_runner    # edit a project
./build/bin/vkm_runtime examples/potion_runner   # play it
```

CMake 3.25+, Ninja, C++17, OpenGL 4.3 core. The build produces four executables
- `vkm_editor`, `vkm_runtime`, and the headless `vkm_cook` and `vkm_server`. The
three that build an `Engine` share a header-only bootstrap (`setupEngineApp` in
`app/engine_app.h`); `vkm_cook` builds none.
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
of the other two. `examples/potion_runner`, `examples/physics_lab` and
`examples/stress_arena` are complete worked examples - the first and the last
build their worlds in code, and the middle one is the only one that authors and
saves a scene, which is also what makes it the multiplayer sample. See [io.md](io.md#projects-and-the-three-roots).

## Overview

vkmEngine is built around an open, type-erased ECS and a stage-based system
pipeline. The `Engine` class owns the services every system is handed - the
ones `FrameContext` carries by reference, below - and a per-stage list of
systems. Each
frame, stages run in declaration order; **within a stage, systems run sequentially
in registration order.** There is no parallel layer scheduler - per-system data parallelism (via
`ThreadPool`) is the scaling lever, not framework-level system parallelism.

Engine is not a singleton: nothing reaches it globally. It is
stack-constructible, so tests and headless tools spin up their own instance.
Singletons are limited to
process-wide registries reached via a static `get()`. There are five, and the
code is the list rather than this sentence -
`grep -rn "static .*& get()" src/` names them: `ThreadPool`, `AsyncLoadQueue`,
`BehaviorRegistry`, `AssetLibrary` and `NetSchema`. (A recipe import goes through the
`AssetFactory` function-pointer seam in `io/asset/asset_factory.h`, not a singleton;
recoverable errors go through the `reportError()` sink, captured by an
editor-owned `EngineErrorLog`.) Profiling goes through `debug/profiler.h` (a
Tracy facade), not an in-engine
statistics registry.

```
Engine
  Scene             (ECS registry, open type-erased)
  ResourceManager   (meshes, materials, textures, fonts)
  Clock             (real + sim time, play / pause / step / time-scale)
  EventBus          (typed pub/sub, flushed at the top of Simulation)
  InputMap          (named actions, resolved once per frame)
  WindowManager     (GLFW window, input handle, frame limiter)
  NetSession        (the wire; brackets the frame, offline until told otherwise)
  HostChrome        (what an authoring host says about the frame)
  RenderSettings    (the one copy of the quality the frame is drawn at)
  m_systemsByStage  (one vector per SystemStage)
```

## SystemStage

Each system is registered at exactly one stage (`core/system.h`):

```cpp
enum class SystemStage : uint8_t {
    Input        = 0,
    Simulation   = 1,
    Transform    = 2,
    Visibility   = 3,
    Render       = 4,
    Editor       = 5,

    Count  ///< Sentinel: number of stages. Keep last.
};
```

In order: acting on the input sampled before any stage; events, async loading,
scripting, animation and physics; local-to-world resolution; culling; building
the `RenderView` and submitting it; the editor.

Default wiring lives in `setupEngineApp` (`app/engine_app.h`, a header-only
inline bootstrap the three hosts that build an Engine include and call -
`vkm_editor`, `vkm_runtime` and `vkm_server`; `vkm_cook` builds none). The
editor adds `EditorSystem` from its own `main()` after that returns; the other
two never do:

| Stage      | Systems                                                                         |
|------------|---------------------------------------------------------------------------------|
| Input      | `CameraControllerSystem` (**editor binary only** - the editor's own view and its fly controls, published as `ctx.hostView`; an authoring tool, so the authoring host is what registers it) |
| Simulation | (EventBus flush), `SplashSystem` (the startup logo; frame-clock presentation, publishes `ctx.splash`), `AsyncLoaderSystem`, `BehaviorSystem`, `AnimationSystem`, `SkeletalAnimationSystem`, `RagdollSystem` (after the pose it reads, before the bodies it writes), `PhysicsSystem`, `CharacterControllerSystem`, `SkySystem` |
| Transform  | `BoneSocketSystem`, `HierarchySystem`, `UISystem` (the game UI; runs in **every** host), `AudioSystem` and `ParticleSystem` (both after the world resolve they read poses from) |
| Visibility | `VisibilitySystem`                                                            |
| Render     | `RenderSystem`                                                                |
| Editor     | `EditorSystem` (editor binary only)                                           |

Place a new system by responsibility and let stage order schedule it - see
[../guides/engine.md](../guides/engine.md#absolutes).

## fixedUpdate

`System::fixedUpdate(FrameContext&)` runs on an accumulator clocked at the
project's tick rate (`project.json`'s `tickRate`, 64 by default), clamped at a
max accumulator (0.25 s) to prevent the spiral-of-death after a frame hitch.
The cap is a duration rather than a tick count, so a project that raises its
rate buys more ticks per hitch rather than a longer stall. It is the
deterministic-simulation hook (physics, networking tick); take the step length
from `ctx.clock.getFixedStep()`, never from the frame delta. A networked client
fills the accumulator up to 5% fast or slow (`Clock::setPacing`, see
[networking.md](networking.md#pacing)); the step itself never
changes. The loop calls
`fixedUpdate()` on every system in stage order; the default body is empty, so a
system that does not override it costs one virtual call per tick.

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
    RenderSettings&  render;   // the one copy of the quality the frame is drawn at

    const HostView*   hostView   = nullptr;  // an authoring host's view to render through, or the scene's camera
    const Visibility* visibility = nullptr;  // VisibilitySystem's culling result
    const PoseBuffer* poses      = nullptr;  // SkeletalAnimationSystem's rig poses
    const UIDrawData* ui         = nullptr;  // UISystem's draw list
    const SplashFrame* splash    = nullptr;  // SplashSystem's logo + fade
    const LiveParticles* particles = nullptr; // ParticleSystem's live particles
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
(see [Audio](audio.md#time-pause-and-the-editor)).

Gameplay gets the same split rather than a choice of system: a behavior's
`onUpdate` is simulation time and does not run at all while paused, and its
`onRealtimeUpdate` is real time and runs every frame regardless - so a pause menu
can animate, and unpause, over a frozen world (see
[Scripting](scripting.md#time-and-pause)).

The context is rebuilt from scratch each frame and the fixed-step loop runs before
any producer stage, so `visibility` and `ui` are always null inside
`fixedUpdate()` - read those from `update()` only. `poses` is the exception,
because a pose is simulation: `SkeletalAnimationSystem` fills it in its own
`fixedUpdate`, so a system registered after it in the Simulation stage reads
this tick's pose.

## Engine config constants

Cross-cutting compile-time limits live in `core/engine_config.h` (treat it as the
source of truth for exact names/values): `MAX_LIGHTS = 256`;
`MAX_SHADOW_CASTERS_2D = 6` 2D atlas tiles (4 of them the first shadow-casting
directional light's CSM cascades, via `NUM_CASCADES`, when there is one) +
`MAX_SHADOW_CASTERS_CUBE = 2` cube slots; `DEFAULT_TICK_RATE` (64) with
`MIN_TICK_RATE` / `MAX_TICK_RATE` bounding what a project may ask for, and the
`MAX_FRAME_ACCUMULATOR` (0.25 s) cap.
The GL backend writes these into every shader's prelude from the C++ constants
themselves, so a shader uses `MAX_LIGHTS` without declaring it (see
[lighting.md](lighting.md#limits-and-the-shader-prelude)).
Per-system tunables (cull distance, camera sensitivity) live in a nested
`Settings` struct on the owning system, not here.

## Directory layout

Engine code, single include root `src/engine/`:

| Path                       | Contents                                                                 |
|----------------------------|--------------------------------------------------------------------------|
| `core/`                    | `Engine`, `System`, `FrameContext`, `SystemStage`, `Clock`, `HostChrome`, `engine_config`, `reflect`, `fnv1a` |
| `core/math/`               | math helpers (rotation, axes, bounds, frustum, random, easing)           |
| `core/memory/`             | `TypeId`, `SparseSet`, `SlotAllocator`, `StorageIndex`, `TypeRegistry`   |
| `core/event/`              | `EventBus` (typed pub/sub; engine-owned infrastructure) and the `EventSender` view behaviors get |
| `ecs/`                     | `Scene`, `EntityId`, `Environment`, `PhysicsSettings`, `EntityResolver` and `EntityNamer` (in `entity_mapping.h`), `HierarchyOperations` (free functions) |
| `ecs/component/core/`      | `Transform`, `WorldTransform`, `Hierarchy`, `Name`                       |
| `ecs/component/render/`    | `Mesh`, `Light`, `Camera`, `Decal`, `LOD`, `ParticleEmitter`, `ReflectionProbe`, `IrradianceVolume` |
| `ecs/component/animation/` | `Animation`, `Animator`, `BoneSocket`, `AnimationTrack`                  |
| `ecs/component/audio/`     | `AudioSource`, `AudioListener`                                           |
| `ecs/component/physics/`   | `Rigidbody`, `Collider`, `CharacterController`, `Joint`, `Ragdoll`       |
| `ecs/component/ui/`        | `UICanvas`, `UIElement`, `UIImage`, `UIText`, `UIButton`, `UIScroll`, `UIShape` |
| `ecs/component/prefab/`    | `PrefabEntity`, `PrefabInstance`, `NetSpawn`                             |
| `system/animation/`        | `AnimationSystem`, `SkeletalAnimationSystem`, `PoseBuffer`, `composePose`, `BoneSocketSystem` |
| `system/async/`            | `AsyncLoaderSystem`                                                      |
| `system/audio/`            | `AudioSystem`, `AudioDevice` (the only engine file that includes the audio backend) |
| `system/hierarchy/`        | `HierarchySystem`                                                        |
| `system/particle/`         | `ParticleSystem` (CPU simulation; `GLParticlePass` draws what it emits)  |
| `system/physics/`          | `PhysicsSystem`, `CharacterControllerSystem`, and `authoring/`, `character/`, `collision/`, `query/`, `solver/` (see [physics.md](physics.md) for the folder map) |
| `system/render/`           | `RenderSystem`, `RenderBackend`, `RenderView`, `RenderSettings`, `data/` |
| `system/script/`           | `BehaviorSystem`, `Behavior`, `ReflectedBehavior`, `BehaviorRegistry`, `ScriptComponent`, `ScriptModule` (see [scripting.md](scripting.md)) |
| `system/sky/`              | `SkySystem` (points the key light at wherever `Environment` says the sun is) |
| `system/splash/`           | `SplashSystem` (the startup image, and the frame it hands over on)       |
| `system/ui/`               | `UISystem`, `UIDrawData` (Transform stage, after Hierarchy; see [ui.md](ui.md)) |
| `system/visibility/`       | `VisibilitySystem`, `Visibility`, `VisibilityContext` and `Culling` (AABB helpers are `core/math/bounds.h`) |
| `resource/`                | `ResourceManager`, `Resource`, `Handle`, `texture_format`               |
| `resource/asset/`          | `MeshAsset`, `MaterialAsset`, `TextureAsset`, `FontAsset`, `SkeletonAsset`, `AnimationClipAsset`, `AudioClipAsset` |
| `resource/generate/`       | `generateCube` and its siblings - meshes, textures, materials and the default scene, built in code so a project needs no art to run |
| `io/`                      | `json_vec`, `project_paths` (shared I/O helpers)                          |
| `io/asset/`                | `AssetLibrary`, `AssetFactory`, `AssetCook` (the cooked binary format), `AssetSerializer`, and `cooked_loader.h`'s free `loadCooked*` functions. The cooker that writes it is a tool, in `src/tools/cook/` |
| `io/scene/`                | `SceneSerializer`, `ComponentSerializer`                                  |
| `net/`                     | `NetSession` and the wire it speaks - schema, snapshots, commands, prediction, interpolation (see [networking.md](networking.md)). Engine-owned infrastructure like `EventBus`, not a `System` |
| `platform/`                | `windows_api` - the one door Windows headers come through, so `NOGDI` and the `far`/`near`/`ERROR` macros are dealt with once |
| `platform/window/`         | `WindowManager`, `FrameLimiter`                                          |
| `platform/input/`          | `InputHandle` (raw keyboard/mouse state), `InputMap`, `InputBinding`, `InputSource`, `InputCommand`, `InputActions`. The map answers the same four queries for a frame or for a tick's command, and carries the pointer and the wheel - neither is an action a binding could name |
| `platform/net/`            | `UdpSocket`, `winsock_init` (the sockets `net/` speaks over)             |
| `platform/threading/`      | `ThreadPool` + `parallelFor` (shared-deque pool, see [threading.md](threading.md)) |
| `platform/library/`        | `DynamicLibrary` (cross-platform `.dll`/`.so` loader for gameplay hot-reload) |
| `platform/process/`        | `ChildProcess` (a program run in the background, its output collected; how the editor runs vkm) |
| `debug/`                   | `build_info`, `engine_error_log`, `fault_latch`, `profiler` (Tracy facade), `screenshot` |

OpenGL backend, `src/backend/opengl/` (every file `gl_`-prefixed; includes are
module-qualified like the engine's, and flat only for the root files above and
for vkmGL's own headers):

| Path          | Contents                                                                  |
|---------------|---------------------------------------------------------------------------|
| (top level)   | `GLBackend`, `GLView`, `GLTarget`, `GLPass`, `GLFrameContext`, and what every pass shares: the fullscreen triangle and the mip-level arithmetic |
| `convention/` | `gl_bindings` (UBO/sampler contract), `gl_format_conversion`              |
| `asset/`      | the GPU copy of one engine asset, kept current by `GLView`: `GLMesh` (a range in the `GLMeshPool` every mesh shares), `GLMaterial`, `GLTexture` |
| `frame/`      | what one frame uploads from the `RenderView`: `GLCamera`, `GLLights`, the object buffer, the skin palette, the instance batcher and the `GLDrawList` it and the shadow pass submit through, `GLShadowData`, the stream upload |
| `storage/`    | GPU-only state that outlives a frame, filled by a pass or a bake and sampled by others: `GLShadowAtlas`, `GLIBL`, `GLAtmosphere`, `GLIrradianceVolume`, the probe array and manager, `GLBloom`, the cluster grid, the fog volume |
| `offline/`    | GPU work that runs outside the pass list: the shared rig (`GLSceneCapture`, `GLCubeConvolver`) and everything it is lent to - the IBL, irradiance and probe bakers, and the editor's material preview |
| `pass/`       | the passes: shadow, depth-prepass, resolve (depth + colour scopes), gtao, cluster-cull, fog (compute), atmosphere, skybox, forward, particle, decal, reflection, dof, bloom, grid, composite, ui, splash |

Editor (`src/editor/`): at the root, the parts every panel sees - `EditorSystem`,
`EditorState`, `EditorContext`, the settings that persist the first and the verbs
(`editor_actions.h`) that act on it. Beneath it, one directory per
responsibility: `command/` (the undoable `Command` and everything that pushes
one), `session/` (what the editor is doing right now - the open project, scene
I/O, the play snapshot, material previews), `chrome/` (the frame around the
viewport: menu bar, status bar, the dockspace's default layout), `panels/`, `overlays/` (what is
drawn over the viewport, the transform gizmo included), `input/` (keybinds,
the shortcut dispatcher and the editor's view) and `ui/` (widgets, style, theme,
dialogs, the asset picker). Tools (`src/tools/`):
`loader/` (the HDR environment and plain image decoders and the SDF font baker) and
`project_boot` (the prologue every host runs) build into `vkm_tools`, which
every host links; the heavy importers
(`import/model_loaders`, `texture_loaders`, `material_loaders`) and the asset
cooker (`cook/`, with the LOD generation and mesh processing it bakes with, and
the texture bake that mips and block-compresses textures) build into
`vkm_cook`, which the editor and `vkm_cook_app` link and the runtime does not,
so the runtime links neither the model importers, meshoptimizer, the heavy
image decode nor the encoder.

Application and gameplay layers sit **outside** the `src/engine/` include root:

| Path                | Contents                                                                  |
|---------------------|---------------------------------------------------------------------------|
| `app/engine_app.h`  | `setupEngineApp`: the shared, header-only bootstrap that registers the default systems. It installs no backend - each host that draws sets its own - and seeds no scene: that is the project's answer, given by `bootProjectWorld` (`src/tools/project_boot.h`) after it returns, with the tick rate and the world's fingerprint. The three hosts that build an Engine include it directly (there is no `EngineApp` library) |
| `app/editor/`       | `vkm_editor` entry point; opens a project and loads its module for hot-reload. Opens anyway when the module or the entry scene is broken - repairing those is what it is for |
| `app/runtime/`      | `vkm_runtime` entry point; opens a project and plays it, or exits non-zero when it cannot ([which conditions](io.md#what-each-host-does-when-a-project-will-not-open)) |
| `app/cook/`         | `vkm_cook` entry point; headless asset cook (no window, no GL, no `Engine`) |
| `app/server/`       | `vkm_server` entry point; hosts a game and referees it - no window, no render backend, the same system stack a client runs |
| `examples/`         | complete worked projects (Potion Runner, Physics Lab, Stress Arena). Gameplay lives in a project, never in the engine |

## Include conventions

Engine includes use module-qualified paths from `src/engine/`:

```cpp
#include "core/engine.h"
#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
#include "system/render/render_view.h"
#include "resource/asset/mesh_asset.h"
```

The backend's includes are module-qualified from `src/backend/opengl/` too, flat
only for its own root files; engine code never reaches into it, seeing only
`RenderBackend` through engine headers. Tools use their own root
(`#include "import/texture_loaders.h"`). Full rules:
[../guides/code-style.md](../guides/code-style.md#1-include-roots-and-include-order).

## Namespaces

- `Vkm::Engine::` for all engine code (ECS, systems, components, resources, editor).
- `Vkm::GL::` for low-level OpenGL wrappers from `vkmGL` (`Shader`, `Context`, ...).
- `Vkm::Log::` for `vkmLog` (`Logger`, `LogLevel`). The `LOG_*` and `VKM_ASSERT`
  macros qualify it themselves, so call sites never name it.

## Rendering at a glance

The engine builds a backend-agnostic `RenderView` snapshot each frame and hands
it to a `RenderBackend` through one seam (`init` / `render`). The OpenGL backend
runs a fixed pass list, in the order `GLBackend::init` registers them -
that list is the record, ending in the splash. There is
no render-graph abstraction and no shader variant cache.

Real features: five light types including LTC area lights, Forward+ clustered
lighting, CSM + spot + point-cube shadows, IBL (an HDR or a physically based
procedural sky, with aerial perspective), GTAO with bent normals, froxel
volumetric fog, a baked SH irradiance volume, reflection probes, screen-space
reflections (a pass on this frame's lit colour), projected decals, CPU billboard
particles, MSAA, DoF, bloom, and a screen-space in-game UI (SDF text, wrapping,
clipping, scrolling, buttons). Not present: TAA, FXAA, motion blur, lens flare,
auto-exposure, contact shadows.

Engine code never includes a `gl_*` header; `MaterialAsset` is the renderer
contract. See [rendering.md](rendering.md) and
[ui.md](ui.md).

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
| `if constexpr` dispatch   | `Scene::forEach`, `AnimationTrack`   | Compile-time routing by type, no virtual call            |
| Compile-time reflection   | `core/reflect.h` `Field` + `Traits`  | Field iteration driving (de)serialization and inspectors |
| Frame-local snapshot      | `RenderView`                         | Capture scene state for the backend, no shared mutation  |
| Version-based GPU sync    | `Resource::version` + `GLView`       | Skip redundant GPU uploads                               |
| Multi-draw indirect       | sorted drawables + `GLDrawList`      | One multi-draw over consecutive runs sharing program, material and vertex layout |
| Shared-deque thread pool  | `ThreadPool` + free `parallelFor`    | Data-parallel loops; main thread participates            |
| Command pattern           | `Command`, `CommandStack`            | Editor undo/redo with drag-coalesce                      |
| Staging-and-swap          | `SceneSerializer::load`              | Transactional scene load; failed loads leave live scene intact |
