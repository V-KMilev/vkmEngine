# Architecture

vkmEngine is a C++17 / OpenGL 4.5 game engine that runs *projects*: a game is a
directory of its own, and the engine's executables open it. Inside, one
`Engine` owns a scene of entities and components and runs a fixed sequence of
systems over it every frame. This page is the map - what the pieces are, the
order they run in, and where their code lives; each subsystem has its own page,
listed in [the manual's index](../README.md).

## Build and run

```bash
git submodule update --init --recursive
cmake -B build -G Ninja
cmake --build build
./build/bin/vkm_editor examples/potion_runner    # edit a project
./build/bin/vkm_runtime examples/potion_runner   # play it
```

The build produces `vkm_editor`, `vkm_runtime`, and the headless `vkm_cook`
and `vkm_server`. [building.md](building.md) has targets, modules and flags.

## The engine runs projects

The engine holds no game of its own. A **project** is a directory with a
`project.json`, its scenes and assets, and its gameplay code built into its own
`bin/` as a module the host loads. Every executable opens *the project beside
it, unless an argument names another*, so a shipped game is its exe next to its
`project.json`.

Paths are split by owner: `engineRoot()` for what ships with the engine,
`projectRoot()` for what the game owns, `userRoot()` for one person's tool
settings ([io.md](io.md#projects-and-their-roots)). `examples/` holds
complete projects to learn from.

## The Engine and its frame

`Engine` (`core/engine.h`) owns the services every system is handed and a list
of systems per stage:

```
Engine
  Scene             (ECS registry, open type-erased)
  ResourceManager   (every loaded asset)
  Clock             (real + sim time, play / pause / step / time-scale)
  EventBus          (typed pub/sub, flushed at the top of Simulation)
  InputMap          (named actions, resolved once per frame)
  WindowManager     (GLFW window, its WindowSurface, input handle, frame limiter)
  NetSession        (the wire; brackets the frame, offline until told otherwise)
  HostChrome        (what an authoring host says about the frame)
  RenderSettings    (the one copy of the quality the frame is drawn at)
  m_systemsByStage  (one vector per SystemStage)
```

`Engine` is not a singleton - tests and headless tools construct their own. The
only singletons are process-wide services reached through a static `get()` - the
thread pool, the async load queue, and the registries.

### Stages

Each system is registered at exactly one `SystemStage` (`core/system.h`). Each
frame the stages run in order, and **within a stage, systems run one after
another in registration order** - there is no parallel scheduler; a system
that needs more speed parallelises its own loop on the `ThreadPool`
([threading.md](threading.md)). The default wiring is `setupEngineApp`
(`app/engine_app.h`), shared by every host that builds an `Engine`:

| Stage      | Systems                                                                         |
|------------|---------------------------------------------------------------------------------|
| Input      | `CameraControllerSystem` (**editor binary only** - the editor's view, published as `ctx.hostView`) |
| Simulation | (EventBus flush), `SplashSystem`, `AsyncLoaderSystem`, `OceanSystem`, `BehaviorSystem`, `AnimationSystem`, `SkeletalAnimationSystem`, `RagdollSystem` (after the pose it reads, before the bodies it writes), `PhysicsSystem`, `CharacterControllerSystem`, `SkySystem` |
| Transform  | `BoneSocketSystem`, `HierarchySystem`, `UISystem` (runs in **every** host), `AudioSystem` and `ParticleSystem` (both after the world resolve they read poses from) |
| Visibility | `VisibilitySystem`                                                            |
| Render     | `RenderSystem`                                                                |
| Editor     | `EditorSystem` (editor binary only)                                           |

A new system goes at the stage its responsibility belongs to, and stage order
schedules it ([engine.md](../guides/engine.md#absolutes)).

### The fixed step

`System::fixedUpdate(FrameContext&)` runs on an accumulator clocked at the
project's `tickRate`, capped so a hitch cannot spiral. It is the deterministic
hook - physics, the network tick - and takes its step from
`ctx.clock.getFixedStep()`, never from the frame delta. A networked client
paces how fast the accumulator fills, never the step
([networking.md](networking.md#pacing)).

### FrameContext

Every system receives a `FrameContext` (`core/system.h`). Its **references are
the Engine's services**, valid all session; its **pointers are per-frame
products** - the culled set, the rig poses, the UI draw list and the like - null
until the stage that produces them has run. The fixed step runs before any
producer stage, so inside `fixedUpdate()` they are null, except `poses` and
`ocean`, which `SkeletalAnimationSystem` and `OceanSystem` fill in their own
fixed step.

Time is read off the clock: `getDeltaTime()` is real seconds (input, camera,
UI), `getSimDelta()` is that scaled by play state (0 while paused, one step
while single-stepping), `getFixedStep()` is the tick. Simulation reads the sim
delta so pause and time-scale apply everywhere; whatever must run while paused
reads the real one. Gameplay gets the same split: a behavior's `onUpdate` stops
while paused and `onRealtimeUpdate` does not
([scripting.md](scripting.md#time-and-pause)).

Engine-wide compile-time limits - light counts, tick-rate bounds, the
accumulator cap - are in `core/engine_config.h`; the ones shaders read reach them
through the prelude ([lighting.md](lighting.md#limits-and-the-shader-prelude)). A
system's own constants are `static constexpr` members of it
(`VisibilitySystem::FIXED_CHECK_STRIDE`).

## The big pieces

- **Scene** - entities and components ([ecs.md](ecs.md)); parenting and the
  world-transform resolve ([hierarchy.md](hierarchy.md)).
- **Resources** - `ResourceManager` owns every asset behind typed generational
  `Handle<T>`s; scene files name assets by their unique `name`
  ([resources.md](resources.md)); on disk they are recipes and a cooked cache
  ([io.md](io.md)).
- **Rendering** - each frame the engine builds a backend-agnostic `RenderView`
  and hands it to a `RenderBackend` through one seam (`init` / `render`). The
  OpenGL backend runs a fixed pass list; engine code never includes a `gl_*`
  header, and `MaterialAsset` is the renderer contract
  ([rendering.md](rendering.md)).
- **Gameplay** - behaviors in a project's module ([scripting.md](scripting.md)),
  talking through the `EventBus` ([events.md](events.md)).
- **The editor** - an extra system and a host around the same Engine
  ([editor.md](editor.md)).

## Directory layout

Engine code, single include root `src/engine/`:

| Path                       | Contents                                                                 |
|----------------------------|--------------------------------------------------------------------------|
| `core/`                    | `Engine`, `System`, `FrameContext`, `SystemStage`, `Clock`, `HostChrome`, `engine_config`, `reflect`, `fnv1a` |
| `core/math/`               | math helpers (rotation, axes, bounds, frustum, random, easing)           |
| `core/memory/`             | `TypeId`, `SparseSet`, `SlotAllocator`, `StorageIndex`, `TypeRegistry`   |
| `core/event/`              | `EventBus` (typed pub/sub) and the `EventSender` view behaviors get |
| `ecs/`                     | `Scene`, `EntityId`, `Environment`, `PhysicsSettings`, `EntityResolver` and `EntityNamer` (in `entity_mapping.h`), `HierarchyOperations` (free functions) |
| `ecs/component/core/`      | `Transform`, `WorldTransform`, `Hierarchy`, `Name`                       |
| `ecs/component/render/`    | `Mesh`, `Light`, `Camera`, `Decal`, `LOD`, `ParticleEmitter`, `ReflectionProbe`, `IrradianceVolume`, `Ocean` |
| `ecs/component/animation/` | `Animation`, `Animator`, `BoneSocket`, `AnimationTrack`                  |
| `ecs/component/audio/`     | `AudioSource`, `AudioListener`                                           |
| `ecs/component/physics/`   | `Rigidbody`, `Collider`, `CharacterController`, `Joint`, `Ragdoll`       |
| `ecs/component/ui/`        | `UICanvas`, `UIElement`, `UIImage`, `UIText`, `UIButton`, `UIScroll`, `UISlider`, `UIToggle`, `UIChoice`, `UIShape` |
| `ecs/component/prefab/`    | `PrefabEntity`, `PrefabInstance`, `NetSpawn`                             |
| `system/animation/`        | `AnimationSystem`, `SkeletalAnimationSystem`, `PoseBuffer`, `composePose`, `BoneSocketSystem` |
| `system/async/`            | `AsyncLoaderSystem`                                                      |
| `system/audio/`            | `AudioSystem`, `AudioDevice` (the only engine file that includes the audio backend) |
| `system/hierarchy/`        | `HierarchySystem`                                                        |
| `system/ocean/`            | `OceanSystem`, `OceanData`, `OceanSurface` |
| `system/particle/`         | `ParticleSystem` (CPU simulation; `GLParticlePass` draws what it emits)  |
| `system/physics/`          | `PhysicsSystem`, `CharacterControllerSystem`, and subfolders (see [physics.md](physics.md)) |
| `system/render/`           | `RenderSystem`, `RenderBackend`, `RenderView`, `RenderSettings`, `data/` |
| `system/script/`           | `BehaviorSystem`, `Behavior`, `ReflectedBehavior`, `BehaviorRegistry`, `ScriptComponent`, `ScriptModule` (see [scripting.md](scripting.md)) |
| `system/sky/`              | `SkySystem` |
| `system/splash/`           | `SplashSystem`                                                           |
| `system/ui/`               | `UISystem`, `UIDrawData` (see [ui.md](ui.md))                            |
| `system/visibility/`       | `VisibilitySystem`, `Visibility`, `HostView`, `VisibilityContext` and `Culling` (AABB helpers are `core/math/bounds.h`) |
| `resource/`                | `ResourceManager`, `Resource`, `Handle`, `texture_format`               |
| `resource/asset/`          | `MeshAsset`, `MaterialAsset`, `TextureAsset`, `FontAsset`, `SkeletonAsset`, `AnimationClipAsset`, `AudioClipAsset` |
| `resource/generate/`       | `generateCube` and its siblings - meshes, textures and materials built in code |
| `io/`                      | `json_vec`, `project_paths` (shared I/O helpers)                          |
| `io/asset/`                | `AssetLibrary`, `AssetFactory`, `AssetCook` (the cooked binary format), `AssetSerializer`, `cooked_loader`. The cooker is a tool, in `src/tools/cook/` |
| `io/scene/`                | `SceneSerializer`, `ComponentSerializer`                                  |
| `net/`                     | `NetSession` and the wire it speaks (see [networking.md](networking.md)). Engine-owned infrastructure like `EventBus`, not a `System` |
| `platform/`                | `windows_api` - the one door Windows headers come through |
| `platform/window/`         | `WindowManager`, `FrameLimiter`, `WindowSurface` (the backend's hold on the window) |
| `platform/input/`          | `InputHandle` (raw keyboard/mouse state), `InputMap`, `InputBinding`, `InputSource`, `InputCommand`, `InputActions` (see [input.md](input.md)) |
| `platform/net/`            | `UdpSocket`, `winsock_init` (the sockets `net/` speaks over)             |
| `platform/threading/`      | `ThreadPool` + `parallelFor` (see [threading.md](threading.md))         |
| `platform/library/`        | `DynamicLibrary` (the gameplay module's `.dll`/`.so` loader)            |
| `platform/process/`        | `ChildProcess` (how the editor runs vkm)                                 |
| `debug/`                   | `build_info`, `engine_error_log`, `fault_latch`, `profiler` (Tracy facade), `screenshot` |

OpenGL backend, `src/backend/opengl/` (every file `gl_`-prefixed; includes are
module-qualified like the engine's, and flat only for the root files above and
for vkmGL's own headers):

| Path          | Contents                                                                  |
|---------------|---------------------------------------------------------------------------|
| (top level)   | `GLBackend`, `GLView`, `GLTarget`, `GLPass`, `GLFrameContext`, and what every pass shares: the fullscreen triangle and the mip-level arithmetic |
| `convention/` | `gl_bindings` (UBO/sampler contract), `gl_depth` (reversed depth), `gl_format_conversion` |
| `asset/`      | the GPU copy of one engine asset, kept current by `GLView`: `GLMesh` (a range in the `GLMeshPool` every mesh shares), `GLMaterial`, `GLTexture` |
| `frame/`      | what one frame uploads from the `RenderView`: `GLCamera`, `GLLights`, the object buffer, the skin palette, the instance batcher and the `GLDrawList` it and the shadow pass submit through, `GLShadowData`, `GLOcean`, `GLOcclusionCull` |
| `storage/`    | GPU state that outlives a frame, filled by a pass or a bake and sampled by others or read back: `GLShadowAtlas`, `GLIBL`, `GLAtmosphere`, `GLIrradianceVolume`, the probe array and manager, `GLBloom`, the cluster grid, the fog volume, `GLExposure` |
| `offline/`    | GPU work that runs outside the pass list: the shared rig (`GLSceneCapture`, its sun map `GLCaptureShadow`, `GLCubeConvolver`) and everything it is lent to - the IBL, irradiance and probe bakers, and the editor's material preview |
| `pass/`       | the passes; [rendering.md](rendering.md) lists them in order |

Editor (`src/editor/`): at the root, the parts every panel sees - `EditorSystem`,
`EditorState`, `EditorContext` and the verbs (`editor_actions.h`) that act on
it. Beneath it, one directory per responsibility: `command/` (undo), `session/`
(the open project, scene I/O, the play snapshot), `chrome/` (the frame around
the viewport), `panels/`, `overlays/` (what is drawn over the viewport),
`input/` and `ui/` (widgets, style, dialogs). Tools (`src/tools/`) build into
`vkm_tools`, which every host links, and `vkm_cook`, which the runtime does not
([resources.md](resources.md#importing-and-cooking)).

Application and gameplay layers sit **outside** the `src/engine/` include root:

| Path                | Contents                                                                  |
|---------------------|---------------------------------------------------------------------------|
| `app/engine_app.h`  | `setupEngineApp`: the shared bootstrap that registers the default systems. It installs no backend and seeds no scene - each host that draws sets its backend, and `bootProjectWorld` (`src/tools/project_boot.h`) opens the world |
| `app/editor/`       | `vkm_editor` entry point; opens a project and hot-reloads its module |
| `app/runtime/`      | `vkm_runtime` entry point; opens a project and plays it ([what it does when it cannot](io.md#what-each-host-does-when-a-project-will-not-open)) |
| `app/cook/`         | `vkm_cook` entry point; headless asset cook (no window, no GL, no `Engine`) |
| `app/server/`       | `vkm_server` entry point; hosts a game with the client's system stack and no window or renderer |
| `examples/`         | complete worked projects. Gameplay lives in a project, never in the engine |

## Conventions

Engine includes are module-qualified from `src/engine/` (`#include
"ecs/scene.h"`), the backend's from `src/backend/opengl/`, and engine code
never includes the backend - it sees only `RenderBackend`
([code-style.md](../guides/code-style.md#1-include-roots-and-include-order)).

Namespaces: `Vkm::Engine::` for all engine code, `Vkm::GL::` for the `vkmGL`
OpenGL wrappers, `Vkm::Log::` for `vkmLog` (the `LOG_*` and `VKM_ASSERT` macros
qualify it themselves).

## Key design patterns

| Pattern                   | Where                                | Purpose                                                  |
|---------------------------|--------------------------------------|----------------------------------------------------------|
| Generational handles      | `StorageIndex`                       | Detect a stale reference to an entity, resource or slot  |
| Sparse-dense dual array   | `SparseSet<T>`                       | O(1) add/remove/lookup, packed iteration                 |
| Type erasure + TypeId     | `ISparseSet`, `typeId<T>()`          | Open component registry without modifying Scene          |
| Compile-time reflection   | `core/reflect.h` `Field` + `Traits`  | Field iteration driving (de)serialization and inspectors |
| Frame-local snapshot      | `RenderView`                         | Capture scene state for the backend, no shared mutation  |
| Version-based GPU sync    | `Resource::version` + `GLView`       | Skip redundant GPU uploads                               |
| Multi-draw indirect       | sorted drawables + `GLDrawList`      | One multi-draw over consecutive runs sharing program, material and vertex layout |
| Shared-deque thread pool  | `ThreadPool` + free `parallelFor`    | Data-parallel loops; main thread participates            |
| Command pattern           | `Command`, `CommandStack`            | Editor undo/redo with drag-coalesce                      |
| Staging-and-swap          | `SceneSerializer::load`              | Transactional scene load; failed loads leave live scene intact |
