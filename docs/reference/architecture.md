# Architecture

vkmEngine is a C++17 game engine, drawn through OpenGL, that runs *projects*: a game is a
directory of its own, and the engine's executables open it. Inside, one
`Engine` owns a scene of entities and components and runs a fixed sequence of
systems over it every frame. This page is the map - what the pieces are, the
order they run in, where their code lives and how new code is shaped; each
subsystem has its own page, listed in [the manual's index](../README.md), and
building and running it is [building.md](building.md).

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

**Which executable was run is what the process is; its arguments are what it
does.** Serving is a host the way baking is, so hosting a game is `vkm_server`, not
a flag on the runtime. The address a client joins is an argument, because it changes
between runs while what the process is does not. There is no `--host`: someone who
wants to host and play serves the project and joins it.

## The Engine and its frame

`Engine` (`core/engine.h`) owns the services every system is handed - the scene and
its assets, the clock, the event bus, the input map, vkmPlatform's window, the net
session, the host's chrome, the render settings, the active camera and the rest, each
documented on its member - and a list of systems per stage.

`Engine` is not a singleton: each host builds its own, as the tests' harness does
(`tests/harness/world.h`). The only singletons are process-wide services: the thread
pool, the async load queue, the asset library and the registries (`BehaviorRegistry`,
`NetSchema`), each reached through a static `get()`, and the asset factory table
(`assetFactory()`). Beside them, a few files keep process-wide state of their own: the prefabs
parsed so far (`io/scene/prefab.cpp`), the sink `reportError` fills
(`debug/error/engine_error_log.cpp`), the project root `Roots::setProjectRoot` sets, the host's
claim on its log (`io/project/host_boot.cpp`), and the cooker's files being baked and its cook
keys (`src/pipeline/cook/asset_cooker.cpp`).

### Stages

Each system is registered at exactly one `SystemStage` (`core/system.h`). Each
frame the stages run in order, and **within a stage, systems run one after
another in registration order** - there is no parallel scheduler
([threading.md](threading.md) says how a system uses more cores). The default wiring is
`setupEngineApp` (`app/engine_app.h`), shared by every host that builds an `Engine`:

| Stage      | Systems                                                                         |
|------------|---------------------------------------------------------------------------------|
| Input      | `CameraControllerSystem` (**editor binary only** - the editor's view, published as `ctx.hostView`) |
| Simulation | (EventBus flush), `SplashSystem`, `LoadingSystem` (not in a headless host), `AsyncLoaderSystem`, `WaterSystem`, `BehaviorSystem`, `AnimationSystem`, `SkeletalAnimationSystem`, `RagdollSystem` (after the pose it reads, before the bodies it writes), `PhysicsSystem`, `CharacterControllerSystem`, `SkySystem`; `BenchSystem` last (**`vkm_runtime --bench` only**, so its camera wins over the game's) |
| Transform  | `BoneSocketSystem`, `HierarchySystem`, `TerrainSystem`, `UISystem` (runs in **every** host), `AudioSystem` and `ParticleSystem` (both after the world resolve they read poses from) |
| Visibility | `VisibilitySystem`                                                            |
| Render     | `DebugViewSystem` (the scene's components, into the debug categories shown), `RenderSystem` |
| Editor     | `EditorSystem` (editor binary only)                                           |

A system runs at exactly one stage, placed by responsibility, and relies on stage
order rather than manual sequencing.

### The fixed step

`System::fixedUpdate(FrameContext&)` runs on an accumulator clocked at the
project's `tickRate`, capped so a hitch cannot spiral. It is the deterministic
hook - physics, the network tick - and takes its step from
`ctx.clock.getFixedStep()`, never from the frame delta. A networked client
paces how fast the accumulator fills, never the step
([networking.md](networking.md#pacing)).

A frame falls between ticks, so the frame's stages see every moving body carried
from its last tick to the frame by its velocities, and the tick's pose comes back
after them, but for what the frame wrote over it (`DrawnAhead`,
[physics.md](physics.md#drawn-between-ticks)); a rig a clip plays is posed for the
frame's moment the same way.

No tick runs while a mesh a collider or a water of the world needs decodes, as the world
arrives or at any time after (`TickHold`, which says when it gives up): a floor or a water
surface made of one is not there yet, and a body ticked meanwhile falls through it. Held, what
is made of it takes part from the tick after it is made, however fast the disk; a mesh only
drawn holds nothing. A client's ticks wait the same, so its prediction meets the colliders
the server's ticks meet. Nor does a world of its own tick while the renderer's startup
programs build (`RenderReadiness::compiling`): it would be played under the loading screen,
its time spent and its keys taken; a client's goes on, as the server's world does. Both hold
the clock (`Clock::setHeld`): a step asked while the world is held runs on the frame that
lets it go, with `onUpdate` and its contacts.

### Determinism

The simulation is deterministic: a client re-runs the ticks the server disagreed
with and must arrive where the server did, so a change that lets machine state or
iteration order leak into a tick breaks networking far from where it was made.

- **Simulated state advances in `fixedUpdate`, by `ctx.clock.getFixedStep()`**, never by
  the frame delta and never by the wall clock: `PhysicsSystem`,
  `CharacterControllerSystem`, `RagdollSystem`, `WaterSystem`'s floating bodies,
  `AnimationSystem`, `SkeletalAnimationSystem` and a behavior's `onFixedUpdate`.
- **Order is by slot.** `PhysicsSystem` settles its contact and joint order on slot, and
  `BehaviorSystem` and `CharacterControllerSystem` run behaviors and characters in slot
  order. Anything that iterates entities into simulation state does the same; never let a
  hash map's iteration order, a pointer's value or a thread's finishing order decide it.
- **Randomness that feeds re-simulated state is owned and seeded.**
  `Math::Random::rng()` is per-thread and clock-seeded: right for a sound's pitch or a
  choice only the server makes, wrong for anything a client replays. A system that
  needs repeatable spread owns a `Math::Rng` with a fixed seed, as `ParticleSystem`
  does. **One draw a statement**: a call's arguments run in an order each compiler
  picks, and GCC's is not Clang's, so `glm::vec3(rng.nextFloat(), rng.nextFloat(), ...)`
  makes another world in another build. The docs suite holds it, in `src/`, `app/`,
  `examples/`, `templates/` and `tests/`.

A parallel loop is deterministic when each index writes only its own output
([threading.md](threading.md#the-rules)) and any merge runs in index order after the
join.

### FrameContext

Every system receives a `FrameContext` (`core/frame_context.h`). Its **references are
the Engine's services**, valid all session; its **pointers are per-frame
products** - the culled set, the rig poses, the UI draw list and the like - null
until the stage that produces them has run - except `debug`, a service a host
may not keep, null for all of a session on one that does not.
The fixed step runs before any producer stage, so inside `fixedUpdate()` they
are null, except `poses`, which `SkeletalAnimationSystem` fills in its own fixed
step, and not in a replayed tick, which runs no `SkeletalAnimationSystem`.

**A system that produces a product publishes it on every exit**, including the ones
that produced nothing: clear the buffer at the top of `update`, then work
(`system/visibility/visibility_system.cpp`). `Engine` keeps one `FrameContext` for the
session and clears its products at the top of every frame, all at once
(`FrameContext::clearProducts`, an assignment to its `FrameProducts` base), so a missed
publish reads null, not last frame's answer. `ctx.cover`
is the deliberate exception: `UISystem` sets it only while the loading screen shows,
and null says none does.

Time is read off the clock: `getDeltaTime()` is real seconds (input, camera,
UI), `getSimDelta()` is that scaled by play state (0 while paused, one step
while single-stepping), `getFixedStep()` is the tick. A frame stepped while paused
lasts its steps, not the wall time, so a host stepping frames for a capture sees what
moves by real time move with the world, however long each frame takes to draw. The
systems on the fixed step
([Determinism](#determinism)) meet pause and time-scale as fewer ticks, because the
same scaled time fills the accumulator. What runs once a frame and is never replayed
reads the sim delta: a behavior's `onUpdate` and `ParticleSystem`. Whatever must run
while paused reads the real one. Gameplay gets the same split: a behavior's `onUpdate`
stops while paused and `onRealtimeUpdate` does not
([scripting.md](scripting.md#time-and-pause)).

Engine-wide compile-time limits live with their subject, under `Config::`: the light and
shadow counts every backend is sized for in `system/render/render_limits.h`, which shaders reach
through the prelude ([lighting.md](lighting.md#limits-and-the-shader-prelude)); the tick rate's
range in `io/project/tick_rate.h`; the seat cap in `io/project/project.h`; the accumulator cap in
`core/clock.h`. A system's own constants are `static constexpr` members of it
(`VisibilitySystem::FIXED_CHECK_STRIDE`).

## What everything else stands on

A mistake in one of these is never local:

- **`Scene` and the ECS** - open and type-erased: any struct is stored with no
  registration; saving or replicating one is a step of its own ([ecs.md](ecs.md)).
- **`FrameContext`** ([above](#framecontext)).
- **The host's two products** - all an authoring host writes into the frame.
  `HostView` (`ctx.hostView`, `core/frame/host_view.h`) is a view to render
  through instead of the scene's active camera. `VisibilitySystem` is its only reader,
  so nothing past the `Visibility` product knows whose view it was. `HostChrome`
  (`ctx.chrome`, `core/host_chrome.h`) is what the host's own UI holds this frame and
  what it has the frame do. A system asks it what it must not take or keep, never which
  host it runs under. An authoring need extends one of these rather than teaching a
  system that the editor exists. `DebugDraw`'s settings - the categories shown, the
  focus - are that service's own, not a host's product: the editor sets them as a game
  would, and `DebugViewSystem` and `RenderSystem` read them off `DebugDraw` alone.
- **`NetSession`** - one object, four states. `simulates(scene, entity)` decides which
  end moves what; offline it answers yes to everything, so single-player is unchanged
  by its existence. `Disconnected` is the opposite of `Offline`: an offline end decides
  everything, a dropped client nothing of the world it was shown. Owned by `Engine` by
  value, it brackets the frame rather than taking part in it
  ([networking.md](networking.md)).
- **`Handle<T>` and `ResourceManager`** - assets owned once, referenced by handle,
  serialized by name.
- **The scene, prefab, project and cooked asset formats** - no back-compatibility read
  path ([decisions.md](decisions.md#what-must-stay-true)), and each format's own
  version gate ([io.md](io.md#what-each-formats-version-gate-does)).
- **`RenderBackend`** - the frame-path seam between engine and GPU. The second seam is
  deliberate: `EditorRenderHooks` (`system/render/editor_render_hooks.h`) is the GPU work
  only authoring asks for - previews, thumbnails, shaders and grading reloaded as they are
  saved, the editor's own ImGui - offered by the
  backend (`RenderBackend::editorHooks()`). It lives in `system/render` because the
  backend implements it and cannot see editor code. An authoring-side GPU need extends
  this seam rather than inventing a third.
- **`WindowSurface`** (vkmPlatform's `window/window_surface.h`) - what a backend asks of the
  window: the hints before it exists, attaching to it, presenting, the swap interval. A
  host hands one to `setupEngineApp` beside its backend (the backend's own, from
  `app/backend_choice.h`); a host that draws nothing hands none.
- **Stage order** ([Stages](#stages)).
- **Determinism of the simulation** ([Determinism](#determinism)).

`EditorSystem` is the one system that holds other systems, taken by constructor
(`editor/editor_system.h`). It may because it is not in the frame's data flow: it is
the authoring tool driving the engine, at the Editor stage, after every producer has
run. Any other system that wants a reference to another has a `FrameContext` product
it has not defined yet.

## Directory layout

Engine code, single include root `src/engine/`:

| Path                       | Contents                                                                 |
|----------------------------|--------------------------------------------------------------------------|
| `core/`                    | `Engine`, `System`, `FrameContext`, `SystemStage`, `Clock`, `HostChrome`, `Graphics`, `InputMap`, `RenderMode`, `project_boot` (opening a project's module and world): the Engine and its frame |
| `core/frame/`              | what one frame's systems hand each other, below every system: the products the frame contract names - `Visibility` (with `CameraData` and `RenderObjects`), `HostView`, `PoseBuffer`, `UIDrawData`, `SplashFrame`, `LoadingFrame` and where the bar is drawn (`loading_bar`), `LiveParticles`, `TerrainFrame`, `RenderReadiness` - and `TickHold`, `ArrivalHold`, `BodyPose`, `secondsAhead`, `ScreenView` and the events the physics, the UI and the loading screen post |
| `base/`                    | the foundation, which includes nothing in the engine above it: one folder per job below |
| `base/math/`               | math helpers (rotation, axes, aabb, ray, frustum, grid, random, easing, damp, cycle, curve, float_range) |
| `base/container/`          | `TypeId`, `SparseSet`, `SlotAllocator`, `StorageIndex`, `TypeRegistry`   |
| `base/event/`              | `EventBus` (typed pub/sub) and the `EventSender` view behaviors get |
| `base/hash/`               | `fnv1a`                                                                  |
| `base/reflect/`            | `reflect` (field reflection), `enum_names`, `FieldInfo` (a field, type-erased) |
| `base/encoding/`           | `utf8`: text as bytes                                                    |
| `base/profile/`            | `profiler`: the Tracy facade every layer includes                        |
| `ecs/`                     | `Scene`, `EntityId`, `ISceneObserver`, `HierarchyOperations` (free functions), the prefab instance queries (`prefab_instances`) |
| `ecs/settings/`            | what the Scene holds and saves: `Environment` and its five settings (`SkySettings`, `NightSkySettings`, `FogSettings`, `AcousticsSettings`, `WaveSettings`), `PhysicsSettings` |
| `ecs/water/`               | what the Scene holds and computes: `WaterData` (the scene's water and its waves, as `WaterSystem` builds them) and the wave model it is moved by - `WaveFrame`, `WaveSurface`, `Ripples`, `Swell`, `WaveSpectrum` |
| `ecs/component/`           | the built-in components, one folder per domain ([ecs.md](ecs.md#built-in-components) lists them) |
| `system/animation/`        | `AnimationSystem`, `SkeletalAnimationSystem`, `BoneSocketSystem`, `playback` (the playback head); posing a rig in `pose/` (`composePose`, a ragdoll's pose) |
| `system/async_loader/`     | `AsyncLoaderSystem` |
| `system/audio/`            | `AudioSystem`: the scene's sources and listener, mixed by vkmAudio's `Audio::Device`, and the acoustics' rays cast over the physics queries |
| `system/debug_view/`       | `DebugViewSystem`: the scene's colliders, lights, probes and the rest, as debug drawings |
| `system/hierarchy/`        | `HierarchySystem`                                                        |
| `system/water/`            | `WaterSystem`: every body of water laid out, the waves that move it built, and what floats on it lifted (see [water.md](water.md)) |
| `system/particle/`         | `ParticleSystem` (CPU simulation; `GLParticlePass` draws what it emits)  |
| `system/physics/`          | `PhysicsSystem`, `CharacterControllerSystem`, `RagdollSystem`, `DrawnAhead`, the queries (`query`), and the colliders' and ragdolls' authoring in `authoring/` (see [physics.md](physics.md)) |
| `system/render/`           | `RenderSystem`, `RenderBackend`, `EditorRenderHooks`, `RenderView`, `render_limits`, `exposure` (auto-exposure), `probe_fill` (repairing the irradiance volume's probes in walls), `screenshot`, `data/` |
| `system/script/`           | `BehaviorSystem`, `Behavior`, `BehaviorBase`, `BehaviorRegistry`, `addBehavior` and `removeBehavior` (`script_component.h`), `behavior_api` (what a game includes); `ScriptModule` and the module's entry points in `module/` (see [scripting.md](scripting.md)) |
| `system/sky/`              | `SkySystem`, and `Atmosphere`: the air the sky and the sun's light are computed through |
| `system/terrain/`          | `TerrainSystem`, `TerrainChunks`, `TerrainPlants`, `TerrainBrush` (see [terrain.md](terrain.md)) |
| `system/splash/`           | `SplashSystem`                                                           |
| `system/loading/`          | `LoadingSystem` |
| `system/ui/`               | `UISystem` and its text layout (see [ui.md](ui.md)) |
| `system/visibility/`       | `VisibilitySystem`, `VisibilityContext` and `Culling` (AABB helpers are `base/math/aabb.h`) |
| `resource/`                | `ResourceManager`, `Resource`, `Handle`, `AssetRef`, `AssetType`, `AsyncLoadQueue` |
| `resource/asset/`          | `MeshAsset`, `MaterialAsset`, `TextureAsset` and its `TextureParams`, `FontAsset`, `SkeletonAsset` and its `BonePose`, `AnimationClipAsset`, `AudioClipAsset`, `TerrainAsset` |
| `io/`                      | `whole_file_write`: a file written whole or not at all                   |
| `io/json/`                 | `json_file`, `own_file` (a project's own lenient files), `json_number`, `json_vec`, `reflect_json`: JSON in and out |
| `io/project/`              | `project` (`project.json`) and what it holds (`render_settings`, `loading_screen`, `actions_json`, `tick_rate`), `host_boot` (a host's prologue: its project and its log), `roots` (the engine's, project's and user's roots) |
| `io/asset/`                | `AssetLibrary`, the `.terrain` format (`terrain_file`), and the byte writer and bounded reader the binary formats share (`byte_stream`). The cooker is the pipeline's, in `src/pipeline/cook/` |
| `io/asset/cooked/`         | `AssetCook` (the cooked binary format) and `cooked_loader` (the runtime's load path) |
| `io/asset/generate/`       | `generateCube` and its siblings - meshes, textures and materials built in code |
| `io/asset/recipe/`         | `asset_source` (the recipe's keys), `AssetFactory` (the recipe-import seam), the material's and the texture's recipes (`material_recipe`, `texture_recipe`) |
| `io/scene/`                | `SceneSerializer`, `ComponentSerializer`, `scene_components` (the components the format holds), `SceneAssets` (a document's assets block), `asset_refs` (which assets entities name), `prefab` and `prefab_ref`, `default_scene`, `scene_fingerprint`, `EntityResolver` and `EntityNamer` (in `entity_mapping.h`) |
| `net/`                     | `NetSession` and the wire it speaks, over vkmNetwork's transport (see [networking.md](networking.md)). Engine-owned infrastructure like `EventBus`, not a `System` |
| `platform/input/`          | `InputBinding` (a key or a button) and `bindingName` (their names), `InputCommand`: the engine's input vocabulary over vkmPlatform's devices (see [input.md](input.md)) |
| `debug/`                   | debug drawing and fault reporting, one folder each below                 |
| `debug/draw/`              | `DebugDraw`, its record (`DebugLine`), `DebugCategory` and `debug_palette`: drawing in the world for a frame or a while |
| `debug/error/`             | `engine_error_log` (the recoverable-error sink) and `fault_latch`         |

OpenGL backend, `src/backend/opengl/` (every file `gl_`-prefixed; includes are
module-qualified like the engine's, and flat only for the root files above):

| Path               | Contents                                                             |
|--------------------|----------------------------------------------------------------------|
| (top level)        | `GLBackend`, its window surface, the shader prelude (`gl_shader_constants`), and what every part shares: the GPU zones (`gl_profiler`), the fullscreen triangle, the forward programs and a picture's upload (`gl_image_upload`) |
| `convention/`      | rules stated once: `gl_bindings` (UBO/sampler contract), `gl_depth` (reversed depth), `gl_mip_levels` |
| `asset/`           | the GPU copy of each engine asset, kept current by `GLAssetMirror` (`gl_asset_mirror`): `GLMesh` (a range in the `GLMeshPool` every mesh shares), `GLMaterial`, `GLAssetTexture`, and `gl_texture_conversion` |
| `frame/`           | the per-frame blocks uploaded from the `RenderView`: `GLCamera`, the object buffer, the skin palette |
| `frame/draw/`      | submitting the frame's objects: the `GLDrawList` the instance batcher and the shadow pass submit through, `GLOcclusionCull` |
| `frame/light/`     | the frame's lights and shadow plan: `GLLights`, `GLShadowPlan`, the shadow block's layout (`gl_shadow_block`) and a light's shadow shape (`gl_shadow_shape`) |
| `frame/water/`     | the frame's water: `GLWaves`, `GLWaterBodies` |
| `storage/`         | GPU state that outlives a frame, filled by a pass or a bake and sampled by others or read back: `GLTarget`, `GLBloom`, `GLExposure` |
| `storage/light/`   | what lighting writes and samples: `GLShadowAtlas`, `GLIBL`, `GLAtmosphere`, `GLIrradianceVolume`, `GLLightmaps`, the probe array, the cluster grid, the fog volume, `LightSlots` |
| `storage/water/`   | what water reads besides its block: `GLWaterShore`, `GLWaveFields` |
| `offline/`         | GPU work that runs outside the pass list: the IBL, air, irradiance and probe bakers and the probe manager, the lightmap baker (`GLLightmapBaker`), the editor's previews, and the bakes' bookkeeping |
| `offline/capture/` | the rig the bakers and previews borrow: `GLSceneCapture`, its sun map `GLCaptureShadow`, `GLCubeConvolver`, `GLCasterDepth` |
| `pass/`            | `GLPass` and `GLFrameContext`: what a pass is and what it is handed; the passes by stage below, which [rendering.md](rendering.md) lists in order |
| `pass/scene/`      | drawn into the scene target: the prepass, shadows, forward, water, skybox, outline, particles, decals, and the resolve |
| `pass/light/`      | lighting the drawn scene: the cluster, GTAO, fog, reflection and atmosphere passes |
| `pass/post/`       | from the lit scene to the picture: bloom, composite, upscale |
| `pass/overlay/`    | drawn over the picture: the grid, debug lines, UI, splash and loading |
| `editor/`          | `GLEditorBackend`: `GLBackend` with ImGui's OpenGL renderer, its own target (`vkm_backend_gl_editor`), linked by the editor's host alone |

### The layer order

A folder includes only its own layer and the ones below it. The engine, bottom to top:

```
base < platform < resource < ecs < debug < io < net < core (the frame contract) < core/frame < system < core (the Engine)
```

- `core/` is two layers. Its contract (`system.h`, `frame_context.h`, `clock`, `host_chrome`,
  `graphics`, `input_map`, `render_mode.h`) is what every system includes; the Engine
  (`engine`, `project_boot`) holds every system and opens a project.
- `core/frame/` holds what one frame's systems hand each other, so a system reads another's
  products without including it. Inside `system/` the order is `physics < audio, particle < script`:
  audio and particles cast rays through the physics queries, and the behavior API offers games
  those queries and the play-sound request. They are services, not frame data. Every other folder
  of `system/` is a peer that includes none of the others.
- The docs suite fails on an engine include against these two orders.
- One seam crosses the order by link, not by include: `io/scene` declares the Script row's save,
  load and asset names, and `system/script/scene_script.cpp` defines them, as only the script
  system can read a behavior.

The backend: `convention < asset < storage < frame < {offline, pass} < (top level)`; the top
level's four shared files (`gl_profiler`, `gl_screen_triangle`, `gl_forward_programs`,
`gl_image_upload`) are what every part may include from above. The editor has no order of its
own, but no folder of it includes one that includes it back; its root is both the hub every part
reads (`EditorState`, `EditorContext`) and the top (`EditorSystem`). Above the engine:
`src/loader` < `src/pipeline` and `src/backend` < `src/editor` < `app/`.

Editor (`src/editor/`): one directory per responsibility, mapped in
[editor.md](editor.md#where-things-are). The runtime's decoders (`src/loader/`) build into
`vkm_loader`, which every host that draws links, and the asset pipeline (`src/pipeline/`) into
`vkm_pipeline`, which the runtime does not ([resources.md](resources.md#importing-and-cooking)).

Application and gameplay layers sit **outside** the `src/engine/` include root:

| Path                | Contents                                                                  |
|---------------------|---------------------------------------------------------------------------|
| `app/engine_app.h`  | `setupEngineApp`: the shared bootstrap that registers the default systems. It installs no backend, bakes no font and seeds no scene - each host that draws sets its backend and calls `ensureDefaultUIFont` (`src/loader/font_baker.h`), and `bootProjectWorld` (`src/engine/core/project_boot.h`) opens the world |
| `app/backend_choice.h` | the one file of `app/` that names a backend: `Backend` (the runtime's) and `EditorBackend` (the editor's) |
| `app/editor/`       | `vkm_editor` entry point; opens a project and hot-reloads its module |
| `app/runtime/`      | `vkm_runtime` entry point; opens a project and plays it ([what it does when it cannot](io.md#what-each-host-does-when-a-project-will-not-open)) |
| `app/cook/`         | `vkm_cook` entry point; headless asset cook (no window, no GL, no `Engine`) |
| `app/server/`       | `vkm_server` entry point; hosts a game with the client's system stack and no window or renderer |
| `examples/`         | complete worked projects. Gameplay lives in a project, never in the engine |

## Adding to the engine

Before writing anything new, find the closest sibling and match it.

- A **system** subclasses `System`, overrides `update` and/or `fixedUpdate`, and comes
  alive at one `addSystem<T>(stage)` line in `setupEngineApp` (`app/engine_app.h`), its
  stage argued beside it. It reads and writes through `FrameContext`, never globals. It
  writes out the Rule of 5, though `core/system.h` already deletes copy and move, and
  says in its `@brief` which stage it runs at and why that one, against a named sibling
  (`system/sky/sky_system.h`).
- A **component** is a plain data struct: bare members, *static* helpers, no mutating
  instance methods. `Transform` is the model, and logic lives in a system. Surviving a
  save is a step of its own
  ([io.md](io.md#adding-a-component-to-the-round-trip)).
- A **render pass** looks like the other passes and lives in the backend
  ([rendering.md](rendering.md#writing-a-pass)).
- An **asset kind** is a `Resource` subclass stored in `ResourceManager`, reached through
  a typed `Handle<T>`, and registered with the asset factories so serialization can
  cold-load it.

A new thing that cannot take its siblings' shape usually means the pattern was misread;
a genuinely new kind of problem is rare.

If most of what you need exists, extend it. The reusable machinery:
`HierarchyOperations`, the `io/asset/generate/` helpers, `SparseSet<T>`, `SlotAllocator`,
`TypeRegistry<Base>` (one slot per type), `parallelFor()`
(`vkm_platform/threading/thread_pool.h`), the field reflection in `base/reflect/reflect.h`, and the
event bus on `ctx.events`.

### Where it belongs

The directory tree encodes responsibility. Let it place your code:

| If the code is...                             | It belongs in...                     |
|-----------------------------------------------|--------------------------------------|
| Per-frame behavior over the scene             | `src/engine/system/<name>/`          |
| Pure data attached to an entity               | `src/engine/ecs/component/<subject>/` |
| An operation on the entity graph many systems share (the hierarchy) | `src/engine/ecs/` |
| A new asset *kind* - the `Resource` subclass  | `src/engine/resource/asset/`         |
| Low-level container / handle / type machinery | `src/engine/base/container/`         |
| An action, a binding or its name, the tick's command | `src/engine/platform/input/` |
| The profiling facade                          | `src/engine/base/profile/`           |
| Error reporting and debug drawing             | `src/engine/debug/`                  |
| Scene / asset / component (de)serialization   | `src/engine/io/`                     |
| What goes on the wire, or who decides what    | `src/engine/net/` - not a system; see [above](#what-everything-else-stands-on) |
| GPU-specific work                             | `src/backend/opengl/`                |
| A log line or an assert                       | `modules/vkmLog/` - every layer logs through it (`vkm_log/logger.h`) |
| The window, a key's or a button's state, the thread pool, a process or a loaded library, where the OS keeps a user's files | `modules/vkmPlatform/` - the frame and `InputMap` stand on it; see [threading.md](threading.md#key-files) and [input.md](input.md#key-files) |
| The mix, a voice, a decoder, the acoustics' estimates or the room's reverb | `modules/vkmAudio/` - `system/audio/` hands it the scene's sources and casts the rays it reads; see [audio.md](audio.md#key-files) |
| A socket, the connection, reliable messages, the join token, the bit stream or a coder | `modules/vkmNetwork/` - `net/` speaks the engine's messages over it; see [networking.md](networking.md#key-files) |
| A step of the physics: collision, the solver, joints, sleep | `modules/vkmPhysics/` - `system/physics/` hands it the scene's bodies; see [physics.md](physics.md#key-files) |
| An OpenGL object, the state it keeps or a call on it | `modules/vkmOpenGL/` - the backend only uses it; see [rendering.md](rendering.md#writing-a-pass) |
| A shader                                      | `shaders/<api>/<stage>/<program>/`, today `shaders/opengl/` |
| Editor-only UI or interaction                 | `src/editor/` - read [editor.md](editor.md#rules-that-fail-quietly) first |
| Importing an asset from its source art (cook-only) | `src/pipeline/import/`              |
| Loading what the runtime reads, or generating an asset | `src/loader/` or `src/engine/io/asset/generate/` |
| Baking an asset into the form the runtime reads | `src/pipeline/cook/`                  |
| Registering a system                          | `app/engine_app.h`                   |
| The prologue every host runs - root and log; then module and world | `src/engine/io/project/host_boot.h`; `src/engine/core/project_boot.h` |
| Gameplay                                      | `examples/<project>/src/` - never in the engine |
| A test of engine code                         | `tests/<area>/<topic>.cpp`, the area named for the source folder it tests; `tests/render/<api>/` is the GPU binary through a backend |

`NetSession` is owned by `Engine` and brackets the frame: a change about *when* the wire
is read or written belongs in `Engine::run`, and a system asking who decides an entity
calls `ctx.net.simulates(ctx.scene, entity)`. If code fits nowhere obvious, the design is
off.

**How a folder grows.** A folder's root holds its entry point and what every part of it
shares; a feature gets a subfolder named for it (`system/physics/`, `net/`) only when the
folder needs one - more than a screenful of files, or two features someone would look for
by name. Many members of one kind, named for that kind (`pass/gl_*_pass`, `panels/*_panel`),
stay flat: the name pattern is the index. Nothing is named `common`, `misc`, `utils` or
`helpers`: shared code sits at the root of the narrowest folder that shares it. Tests follow
the source.

### Tracing the change

A change is rarely one file. Follow it outward:

- Does it need a `FrameContext` product, and at which stage is it written and read?
- Does a serializer in `io/` need to know?
- Does the editor need to author it?
- Does the cooker need a recipe?
- Does a page of this manual now describe something that is no longer true?

## When something fails

| Context                          | Mechanism                          |
|----------------------------------|------------------------------------|
| Preconditions (programmer error) | `VKM_ASSERT(condition, "message")` |
| Initialization failures          | `throw std::runtime_error(...)`    |
| Runtime lookup not found         | return `nullptr` or `false`        |
| Invalid handle                   | null sentinel (index 0)            |

- `VKM_ASSERT` compiles to nothing where `NDEBUG` is defined, so its condition has no side
  effects. `VKM_ASSERTS` (on, but in a shipping build) undefines `NDEBUG` whatever the build
  type; off, the build type decides, and all but `Debug` define it. Most asserts sit in the
  foundation types - `Scene`, `SparseSet`, `SlotAllocator`, `ResourceManager`, the hierarchy
  operations - where a broken precondition corrupts shared state; a few elsewhere catch what
  only a programmer can get wrong, such as `GLForwardPrograms::programFor` before `build`. A
  destructive operation **asserts and guards** the same condition: the assert is for the
  programmer who broke it, the guard for the player who ships it. Against what a scene or a
  player can cause, the backend, the editor and the hosts guard and degrade.
- **Exceptions are for startup, loading and explicit recovery boundaries**, never a hot path.
- **An enum that indexes a hand-written table gets a `Count` sentinel and a `static_assert` on
  the table's length** (`STAGE_NAMES` in `core/engine.cpp`). Better still, generate the table
  from the same list as the enum - `KIND_DIRS` expands from `VKM_ASSET_KINDS` - and there is
  nothing left to compare.
- **Anything read from outside the process is bounded before it is trusted** - a count divided
  before it is multiplied, a length checked before it is allocated. Each reader refuses what it
  cannot represent rather than clamping it into something wrong.

Three channels carry a failure; pick by **who caused it and who can act on it**:

- **`LOG_ERROR` and friends** reach the developer's console. Every failure logs.
- **`reportError(category, source, message)`** (`debug/error/engine_error_log.h`) reaches the editor
  Console's Errors and the cooker's summary: a recoverable failure the **project author** caused
  - a throwing script hook, an asset that will not resolve, a module that will not load. It logs
  too, so it never wants a `LOG_ERROR` beside it.
- **`state.toasts.push(...)`** reaches the author beside the gesture, for an **editor action** that
  succeeded or failed. An editor operation that fails takes the log and the toast, not
  `reportError` - the author caused nothing. Do not add a second log line restating what the layer
  below already printed.

## Conventions

**Axes.** Forward is -Z and screen-right is +X, right-handed with +Y up - glm's own convention.
Screen-right is `cross(forward, up)`. The convention lives in `Math::computeForward` and its
siblings: use them, rather than working out an axis by hand.

**Include roots.** Code lives under seven, and a file includes by **module path** (`"ecs/scene.h"`),
never the bare filename (`"scene.h"`):

| Root                  | Include style                    | Example                                  |
|-----------------------|----------------------------------|------------------------------------------|
| `src/engine/`         | module-qualified                 | `#include "system/render/render_view.h"` |
| `src/backend/opengl/` | module-qualified; its own root files are flat | `#include "asset/gl_mesh.h"`, `#include "gl_backend.h"` |
| `src/pipeline/`       | module-qualified                 | `#include "import/texture_import.h"`     |
| `src/loader/`         | flat: one folder                 | `#include "font_baker.h"`                |
| `src/editor/`         | module-qualified, from **two** roots | `#include "panels/inspector/cards.h"` and `#include "ecs/scene.h"` |
| `app/`                | repo-root-qualified              | `#include "app/engine_app.h"`            |
| `tests/`              | area-qualified, the harness too  | `#include "physics/bodies.h"`, `#include "harness/check.h"` |

A library under `modules/` - each a repository of its own, a submodule here
([building.md](building.md#external-modules)) - is included by its own folder:
`#include "vkm_log/logger.h"`, `#include "vkm_platform/window/window_manager.h"`,
`#include "vkm_audio/device.h"`, `#include "vkm_network/connection.h"`,
`#include "vkm_physics/solver/solver.h"`, `#include "vkm_opengl/texture/texture.h"`. vkmVulkan, the Vulkan layer to
come, holds no code yet, so nothing includes it and the build adds nothing of it.

The editor's two roots are its own and the engine's; its own root files are included bare
(`#include "editor_state.h"`).

**Engine code never reaches into `backend/`**: it sees the GPU through the seams
[above](#what-everything-else-stands-on), and which files may name a graphics API - a backend's
places and the hosts' one choice, `app/backend_choice.h` - is
[decisions.md](decisions.md#what-must-stay-true)'s.

**Namespaces.** `Vkm::Engine::` for all engine code, the OpenGL backend included; `Vkm::Log::` for
`vkmLog` (the `LOG_*` and `VKM_ASSERT` macros qualify it themselves); `Vkm::Platform::` for
`vkmPlatform`, which engine code spells `Platform::`; `Vkm::Audio::` for `vkmAudio`, spelled
`Audio::`; `Vkm::Network::` for `vkmNetwork`, spelled `Network::`; `Vkm::Physics::` for `vkmPhysics`,
spelled `Physics::`; `Vkm::OpenGL::` for the `vkmOpenGL` OpenGL wrappers, so every `Vkm::OpenGL`
block in the backend forward-declares a vkmOpenGL type. A file outside `Vkm` - a game's, a test's -
says `using namespace Vkm::Platform;` beside `Vkm::Engine` where it names one of the platform's.
Helper namespaces nest under their umbrella (`Vkm::Engine::Math`); a file whose content lives in one
opens it in the one-line form, `namespace Vkm::Engine::Math {`, closed by one `}`.

**Do not redefine what glm or the standard library already names.** `glm::pi<float>()`,
`glm::epsilon<float>()` and the rest of `glm/gtc/constants.hpp` are the engine's vocabulary; a local
`PI` or `EPSILON` is a second name that drifts. Declare your own constant only when the value is the
engine's - a slope limit, a sleep threshold, a cascade count.

**A reflected type closes the namespace first.** `VKM_REFLECT_BEGIN` opens
`namespace Vkm::Engine::Reflect` itself, so it goes at global scope, after the namespace close,
naming the type fully qualified:

```cpp
} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Transform)
    VKM_F(position)
    VKM_F(rotation)
    VKM_F(scale)
VKM_REFLECT_END()
```

**Logging is categorized**: `#define VKM_LOG_CATEGORY "RENDER"` above the own header, the one thing
allowed before it because `vkm_log/logger.h` defaults the category when nothing set it, then `LOG_TRACE` /
`LOG_INFO` / `LOG_WARNING` / `LOG_ERROR` with printf-style formatting. A category names a subsystem,
not a file: the whole backend logs as `"BACKEND::GL"`; reuse your neighbors'. A `.cpp` that logs
declares its category, since a file that logs with none sends its lines where no filter reaches them,
and one that does not log declares none.

### Known exceptions

Deliberate deviations. Do not add one without the owner.

- **`ScriptComponent` is a move-only component.** It holds
  `std::vector<std::unique_ptr<BehaviorBase>>`, so it cannot be copied; deep copy goes
  through `BehaviorBase::clone()`. Do not generalize from it.
- **A gameplay `Behavior` writes no Rule of 5.** `clone()` copies it, and the authored fields are its
  whole interface.
- **Reflected behavior fields are bare publics.** Authored fields on a `Behavior` subclass are bare
  public members: the field name is the serialized key and the inspector's label, so `m_` would leak
  into both. Runtime state on the same class takes `m_`. The model is `Spinner::degreesPerSecond`
  (`templates/default/src/game.h`).
