# Project Overview

vkmEngine is a C++17 3D rendering engine with an OpenGL backend, built around an
open type-erased ECS and a stage-based system pipeline. This page is the quick
orientation; each subsystem has a deeper doc linked below.

## Build and run

```bash
git submodule update --init --recursive
cmake -B build -G Ninja
cmake --build build
./build/bin/engine_editor     # engine + editor (or engine_runtime for the bare engine)
```

CMake 3.25+, Ninja, C++17, OpenGL 4.3 core. The build produces two executables -
`engine_editor` and `engine_runtime` - over a shared header-only bootstrap
(`setupEngineApp` in `app/engine_app.h`). See [building.md](building.md) for
targets, modules, and flags.

## Core model

- **`Scene`** - an open, type-erased ECS registry. Any plain struct is a component
  with no registration; each type is stored in its own `SparseSet<T>`, created on
  first use. Entities are generational handles (`EntityId` = `StorageIndex`) from a
  `SlotAllocator`, so stale handles are detected, not crashed.
- **`System`** - per-frame unit with `init` / `update` / `fixedUpdate` / `shutdown`.
  Each registers at exactly one `SystemStage`.
- **`Engine`** - stack-constructible owner of the `Scene`, `ResourceManager`,
  `WindowManager`, `SimulationClock`, and the per-stage system list. **No
  `Engine::get()` singleton.** Singletons are limited to a handful of
  process-wide registries and log sinks accessed via a static `get()`:
  `ThreadPool`, `AsyncLoadQueue`, `BehaviorRegistry`, `AssetFactories`, and
  `BehaviorErrorLog`.
- **`FrameContext`** - the per-frame bundle passed to every system. Carries three
  deltas: `deltaTime` (real), `simDeltaTime` (clock-scaled, pause/step-aware),
  `fixedDeltaTime` (1/60). Simulation systems read `simDeltaTime`.

## System execution order

Stages run in declaration order; within a stage, systems run **sequentially** in
registration order (there is no parallel layer scheduler - per-system `parallelFor`
is the scaling lever). The default wiring lives in `setupEngineApp`
(`app/engine_app.h`, a header both executables include); `engine_editor` adds
`EditorSystem` on top.

| Stage | Default systems |
|-------|-----------------|
| Input | CameraControllerSystem |
| Simulation | EventSystem, AsyncLoaderSystem, BehaviorSystem, AnimationSystem, PhysicsSystem |
| Transform | HierarchySystem (resolves `WorldTransform` from local `Transform` + hierarchy) |
| Visibility | VisibilitySystem (frustum / distance / screen-size culling -> `Visibility`) |
| Render | RenderSystem (builds `RenderView`, hands it to the backend) |
| UI | EditorSystem (editor binary only) |

(`FileWatcherSystem` is an Input-stage `System` the engine provides but the default app
does not register; see [io.md](system/io.md).)

## Rendering at a glance

The engine builds a backend-agnostic `RenderView` snapshot each frame and hands it
to a `RenderBackend` through one seam (`init` / `resize` / `render`). The OpenGL
backend runs a **fixed 10-pass forward pipeline**: Shadow -> DepthPrepass -> GTAO ->
Skybox -> Forward (PBR ubershader) -> SSR -> MotionBlur -> Bloom -> Grid ->
Composite. There is no render-graph abstraction and no shader variant cache.
Real features: five light types incl. LTC area lights, CSM + spot + point-cube
shadows, IBL, GTAO, SSR, bloom, motion blur, reflection probes. Not present: TAA,
DoF, auto-exposure. Engine code never includes a `gl_*` header; `MaterialAsset` is
the renderer contract. See [rendering.md](system/rendering.md).

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
