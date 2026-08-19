# Project Overview

vkmEngine is a C++17 3D rendering engine with an OpenGL backend, built around an
open type-erased ECS and a stage-based system pipeline. This page is the quick
orientation; each subsystem has a deeper doc linked below.

## Build and run

```bash
git submodule update --init --recursive
cmake -B build -G Ninja
cmake --build build
./build/bin/vkm_editor examples/potion_runner    # edit a project
./build/bin/vkm_runtime examples/potion_runner   # play it
```

CMake 3.25+, Ninja, C++17, OpenGL 4.3 core. The build produces three executables
- `vkm_editor`, `vkm_runtime`, and the headless `vkm_cook` - over a
shared header-only bootstrap (`setupEngineApp` in `app/engine_app.h`). See
[building.md](building.md) for targets, modules, and flags.

## The engine runs projects

The engine holds no game of its own. A game is a **project**: a directory with a
`project.json`, its own scenes, assets, and gameplay code built into its own
`bin/`. Every executable finds one by the same rule - *the project beside the
executable, unless an argument names a different one* - so a shipped game ships
its exe next to its `project.json` and the player passes nothing.

Two roots keep the halves apart: `engineRoot()` for what ships with the engine
(shaders, default font, icons; read-only to a game) and `projectRoot()` for what
the game owns. `examples/potion_runner` and `examples/stress_arena` are complete
worked examples. See [system/io.md](system/io.md#projects-and-the-two-roots).

## Core model

- **`Scene`** - an open, type-erased ECS registry. Any plain struct is a component
  with no registration; each type is stored in its own `SparseSet<T>`, created on
  first use. Entities are generational handles (`EntityId` = `StorageIndex`) from a
  `SlotAllocator`, so stale handles are detected, not crashed.
- **`System`** - per-frame unit with `init` / `update` / `fixedUpdate` / `shutdown`.
  Each registers at exactly one `SystemStage`.
- **`Engine`** - stack-constructible owner of the `Scene`, `ResourceManager`,
  `WindowManager`, `Clock`, `EventBus`, `InputMap`, and the per-stage system list. **No
  `Engine::get()` singleton.** Singletons are limited to a few process-wide
  registries accessed via a static `get()`: `ThreadPool`, `AsyncLoadQueue`, and
  `BehaviorRegistry`. Asset construction uses the `AssetFactory` function-pointer
  seam; recoverable errors use the `reportError()` sink (captured by the
  editor-owned `EngineErrorLog`).
- **`FrameContext`** - the per-frame bundle passed to every system. Services come
  as references (scene, resources, clock, events, window, input), per-frame
  products as pointers (`visibility`, `ui`). Time comes off the clock:
  `getDeltaTime()` (real), `getSimDelta()` (clock-scaled, pause/step-aware),
  `getFixedStep()` (1/60). Simulation systems read the sim delta.

## System execution order

Stages run in declaration order; within a stage, systems run **sequentially** in
registration order (there is no parallel layer scheduler - per-system `parallelFor`
is the scaling lever). The default wiring lives in `setupEngineApp`
(`app/engine_app.h`, a header both executables include); `vkm_editor` adds
`EditorSystem` on top.

| Stage | Default systems |
|-------|-----------------|
| Input | CameraControllerSystem |
| Simulation | (EventBus flush), AsyncLoaderSystem, BehaviorSystem, AnimationSystem, ParticleSystem, PhysicsSystem, SkySystem |
| Transform | HierarchySystem (resolves `WorldTransform` from local `Transform` + hierarchy); UISystem (resolves UI layout + builds the screen-space overlay draw list) |
| Visibility | VisibilitySystem (frustum / distance / screen-size culling -> `Visibility`) |
| Render | RenderSystem (builds `RenderView`, hands it to the backend) |
| UI | EditorSystem (editor binary only) |

(`FileWatcherSystem` is an Input-stage `System` the engine provides but the default app
does not register; see [io.md](system/io.md).)

## Rendering at a glance

The engine builds a backend-agnostic `RenderView` snapshot each frame and hands it
to a `RenderBackend` through one seam (`init` / `render`). The OpenGL
backend runs a **fixed 19-pass forward pipeline**: Shadow -> DepthPrepass ->
ResolveDepth -> HiZ -> OcclusionCull -> GTAO -> Skybox -> ClusterCull ->
FogCompute -> Forward (PBR ubershader) -> Particles -> ResolveColor -> Decals ->
FogApply -> DoF -> Bloom -> Grid -> Composite -> UI (the screen-space overlay).
There is no render-graph abstraction and no shader variant cache. Real features:
five light types incl. LTC area lights, Forward+ clustered lighting, CSM + spot +
point-cube shadows, IBL (HDR or procedural sky), GTAO with bent normals,
froxel volumetric fog, baked SH irradiance volumes, reflection probes,
projected decals, CPU billboard particles, MSAA, DoF, bloom, and a
screen-space in-game UI (SDF text, buttons). Not present: TAA, SSR, FXAA,
motion blur, lens flare, auto-exposure, contact shadows. Engine code never includes a `gl_*` header;
`MaterialAsset` is the renderer contract. See [rendering.md](system/rendering.md)
and [ui.md](system/ui.md).

## Resources

`ResourceManager` owns all assets (`MeshAsset`, `TextureAsset`, `MaterialAsset`,
`ShaderAsset`) behind typed generational `Handle<T>`s. Assets are identified by a
unique non-empty `name`; scene files reference them by name and resolve via
`findByName`. `commit()` bumps a per-resource version so the backend skips
unchanged uploads. See [resources.md](resources.md).

## Layout and conventions

- `src/engine/` - engine code, module-qualified includes (`#include "ecs/scene.h"`).
- `src/backend/opengl/` - OpenGL backend, flat `gl_`-prefixed includes.
- `src/editor/` - ImGui editor (panels, gizmo, undo/redo).
- `src/tools/` - asset loaders + generators.
- Namespaces: `Engine::` for engine code, `Core::` for vkmGL wrappers.

Full tree and design patterns: [architecture.md](architecture.md). House style:
[../guides/code-style.md](../guides/code-style.md).

## Where to go next

- **Before writing code:** [../README.md](../README.md) (the pre-flight order) ->
  [../guides/development.md](../guides/development.md) ->
  [../guides/implementation.md](../guides/implementation.md) ->
  [../guides/code-style.md](../guides/code-style.md).
- **Subsystem detail:** [architecture.md](architecture.md), [ecs.md](ecs.md),
  [resources.md](resources.md), [threading.md](threading.md),
  [editor.md](editor.md), and `system/` (rendering, lighting, visibility,
  hierarchy, animation, events, io, scripting, physics).
