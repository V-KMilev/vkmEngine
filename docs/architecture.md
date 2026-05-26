# Architecture

## Overview

vkmEngine is built around an open type-erased ECS and a stage-based
system pipeline. The `Engine` class owns the `Scene`, `ResourceManager`,
`WindowManager`, and a per-stage list of systems. Each frame, stages
run in declaration order; within a stage, systems run either sequentially
in registration order, or in parallel layers when opted in.

There is no `Engine::get()` singleton. Engine is stack-constructible,
and tests or headless tools can spin up their own instance. Process-global
statistics live behind `Engine::getStatistics()` so the `STATS_RECORD_*`
macros still work without an Engine handle.

```
Engine
  Scene              (ECS registry, open type-erased)
  ResourceManager    (meshes, materials, textures, shaders)
  WindowManager      (GLFW window, input handle, frame limiter)
  m_systemsByStage   (one vector per SystemStage)
  m_schedule         (per-stage execution plan: layers + parallel flag)
  m_fixedUpdaters    (opt-in subset of systems with a real fixedUpdate)
```

## SystemStage

Each system is registered at exactly one stage:

```cpp
enum class SystemStage : uint8_t {
    Input        = 0,   // Poll devices, capture input
    Simulation   = 1,   // Gameplay, events, animation, physics
    Transform    = 2,   // Local to world transform resolution
    Visibility   = 3,   // Culling
    Render       = 4,   // Build RenderView, execute RenderGraph
    UI           = 5,   // ImGui, editor
    Count
};
```

Default wiring (`main.cpp`):

| Stage      | Systems                                                  |
|------------|----------------------------------------------------------|
| Input      | `CameraController`, `FileWatcher`                        |
| Simulation | `EventSystem`, `AnimationSystem`                         |
| Transform  | `HierarchySystem`                                        |
| Visibility | `VisibilitySystem`                                       |
| Render     | `RenderSystem`                                           |
| UI         | `EditorSystem`                                           |

Future systems slot in by responsibility:

- Physics goes in Simulation and uses `fixedUpdate`.
- Scripting goes in Simulation (or splits into Input and UI for input/UI scripts).

## Per-stage layer scheduler

`Engine::buildSchedule()` runs once at init. For each stage it groups
systems into **layers** using a greedy assignment based on
`System::declareAccess()`:

- Each system declares which component `TypeId`s it reads and writes.
- A system goes into the earliest layer where its reads and writes
  don't overlap any other system's writes, and its writes don't
  overlap any other system's reads.
- Layers run in order; within a layer, systems run concurrently when
  parallel dispatch is enabled for the stage, sequentially otherwise.

`Engine::setParallelDispatch(stage, true)` flips a layer-level parallel
fan-out via `ThreadPool` for that stage. The API exists but is not wired
yet. Today's stages have at most two systems each, so layer parallelism
buys nothing. The per-system data-parallel `parallelFor` inside
`HierarchySystem`, `AnimationSystem`, and `VisibilitySystem` is the
actual scaling lever. Revisit when 3+ peers share a stage (Physics
plus AI plus Particles plus Audio under Simulation is the motivating
case).

A default-constructed `SystemAccess` is treated as **conservative**:
the system conflicts with everything in its stage and gets its own
layer. Systems that genuinely touch no shared state return
`SystemAccess::none()`, which is parallel-safe with anyone.

## fixedUpdate

`System::fixedUpdate(FrameContext&)` is called on an accumulator clocked
at `Config::FixedTimeStep` (1/60 s). The accumulator is clamped at
`Config::MaxFrameAccumulator` (0.25 s) to prevent the "spiral of death"
after a frame hitch. The clamp warning is throttled to once per second.

No system currently overrides `fixedUpdate()`. It is reserved for the
forthcoming Physics system. `System::hasFixedUpdate()` opts a system
into the per-init filtered list (`m_fixedUpdaters`) so empty-virtual
dispatch doesn't happen for every system every tick.

## FrameContext

The per-frame data bundle passed to every system:

```cpp
struct FrameContext {
    Scene&            scene;
    ResourceManager&  resources;
    WindowManager&    window;
    FrameTracker&     frameTracker;
    float             deltaTime;        // real seconds since last frame
    float             fixedDeltaTime;   // Config::FixedTimeStep (60 Hz)

    // Render rect within the GLFW window. The editor calls
    // WindowManager::setSceneViewport to report this; headless engines
    // default to the full window.
    uint32_t          viewportX, viewportY;
    uint32_t          viewportWidth, viewportHeight;

    // Per-frame visibility snapshot, populated by VisibilitySystem
    // in its stage. null before that stage runs each frame; first
    // frame is null everywhere.
    const Visibility* visibility = nullptr;
};
```

## Engine config constants

Cross-cutting compile-time limits live in
[core/engine_config.h](../src/engine/core/engine_config.h)
under `namespace Engine::Config`:

| Constant                | Default     | Notes                                                                  |
|-------------------------|-------------|------------------------------------------------------------------------|
| `MaxLights`             | 32          | Must match `MAX_LIGHTS` in `shaders/pbr/fragment.shader`               |
| `MaxShadowCasters2D`    | 6           | Must match `SHADOW_MAX_CASTERS_2D`                                     |
| `MaxShadowCastersCube`  | 2           | Must match `SHADOW_MAX_CASTERS_CUBE`                                   |
| `NumCascades`           | 4           | Directional CSM cascades. Must match `NUM_CASCADES`                    |
| `ShadowCubeNear`        | 0.1f        | Emitted into `shaders/_generated/engine_config.glsl` at build time     |
| `FixedTimeStep`         | 1/60 s      | fixedUpdate cadence                                                    |
| `MaxFrameAccumulator`   | 0.25 s      | Spiral-of-death cap                                                    |

Per-system tunables (cull distance, camera sensitivity, ...) live as a
nested `Settings` struct on the owning system, **not** here.

## Directory layout

The single include root for engine code is `src/engine/`:

| Path                              | Contents                                                                                  |
|-----------------------------------|-------------------------------------------------------------------------------------------|
| `core/`                           | `Engine`, `System`, `FrameContext`, `SystemStage`, `engine_config`                        |
| `core/math/`                      | `MathUtils`                                                                               |
| `core/memory/`                    | `TypeId`, `SparseSet`, `SlotAllocator`, `Storage`                                         |
| `ecs/`                            | `Scene`, `Entity`                                                                         |
| `ecs/component/`                  | `Transform`, `WorldTransform`, `Camera`, `Mesh`, `Animation`, `Light`, `Hierarchy`, `Name`|
| `system/animation/`               | `AnimationSystem`, `AnimationTrack`, `Keyframe`, `Easing`                                 |
| `system/camera/`                  | `CameraController`                                                                        |
| `system/event/`                   | `EventSystem` (typed pub/sub: emit / enqueue / subscribe)                                 |
| `system/hierarchy/`               | `HierarchySystem`, `HierarchyOperations` (free functions)                                 |
| `system/io/`                      | `FileWatcher` (polling shader hot-reload)                                                 |
| `system/render/`                  | `RenderSystem`, `RenderBackend`, `RenderView`, `RenderGraph` + builder + context + `RGResource`, `RenderPass`, `RenderTarget`, `Environment` |
| `system/visibility/`              | `VisibilitySystem`, `Visibility`, `VisibilityContext`, `BoundsUtils`                      |
| `system/visibility/culling/`      | `FrustumCuller`, `DistanceCulling`, `ScreenSizeCulling`, `OcclusionCuller` (stub)         |
| `resource/`                       | `ResourceManager`, `Resource`, `Handle`, `MeshAsset`, `MaterialAsset`, `TextureAsset`, `ShaderAsset` |
| `io/`                             | `SceneSerializer`, `AssetSerializer` (+ `AssetFactories`), `ComponentSerializer`          |
| `platform/window/`                | `WindowManager`, `Window`, `InputHandle`, `KeyboardInputHandle`, `MouseInputHandle`, `FrameLimiter` |
| `platform/threading/`             | `ThreadPool`, `StealThread`, `Task`                                                       |
| `debug/`                          | `StatisticTracker`, `FrameTracker`, `CallTracker`, `FrameInfo`, `PrintHelper`             |

The OpenGL backend lives under `src/backend/opengl/`:

| Path        | Contents                                                                                                    |
|-------------|-------------------------------------------------------------------------------------------------------------|
| `core/`     | `GLBackend`, `GLView`, `GLInstanceBatcher`, `GLDefaultRenderTarget`, `GLSceneTarget`, `GLFrameResources`    |
| `resource/` | `GLMesh`, `GLMaterial`, `GLTexture`, `GLLights`, `GLShaderProgram`, `GLShadowAtlas`, `GLShadowData`, `GLIBL`, `GLBloom`, `GLAutoExposure`, `GLGBuffer`, `GLTAA`, `GLPostScratch`, `GLCamera` |
| `config/`   | `GLConfig`, `GLTextureMapping`, `GLFormatConversion`                                                        |
| `pass/`     | `GL*Pass`: forward, shadow, prepass, gtao, skybox, ibl_bake, aabb_debug, grid, ssr, lens_flare, taa, dof, motion_blur, bloom, exposure, composite |

The editor lives under `src/editor/`:

| Path         | Contents                                                                                                 |
|--------------|----------------------------------------------------------------------------------------------------------|
| (root)       | `EditorSystem` (UI stage)                                                                                |
| `framework/` | `EditorState`, `EditorContext`, `Command`, `CommandStack`, `editor_commands`, `scene_io_controller`, `screenshot`, menu/status bar, panel resize, shortcuts, `asset_picker` |
| `panels/`    | `HierarchyPanel`, `InspectorPanel`, `BottomPanel`, `AssetBrowser`, `MaterialEditor`, `EnvironmentInspector`, `PreferencesPanel` |
| `overlays/`  | `GizmoOverlay`, `ViewportOverlay`, `ViewportToolbar`, `PlaybackBar`                                      |
| `gizmo/`     | `TransformGizmo` (split into draw, drag, hit translation units)                                          |
| `input/`     | `editor_actions`, `editor_keybinds`                                                                      |
| `ui/`        | `editor_widgets`, `editor_icons`, `editor_style`, `editor_theme`                                         |

Tools live under `src/tools/`:

| Path                      | Contents                                                                            |
|---------------------------|-------------------------------------------------------------------------------------|
| `loader/`                 | `TextureLoaders`, `MaterialLoaders`, `ModelLoader`, `ShaderLoaders`                 |
| `generator/`              | `MeshGenerators`, `TextureGenerators`, `MaterialGenerators`, `LightGenerators`      |
| `asset_registration.cpp`  | Registers all tools/ factories into `AssetFactories` at startup                     |

## Include conventions

Engine includes use module-qualified paths from `src/engine/`:

```cpp
#include "core/engine.h"
#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "system/render/render_pass.h"
#include "system/visibility/visibility.h"
#include "system/event/event_system.h"
#include "resource/mesh_asset.h"
```

The backend uses flat `gl_`-prefixed includes (`#include "gl_backend.h"`)
intentionally; see the code style guide section 13.2. Engine code never
reaches into the backend; it only sees `RenderBackend` and friends
through engine headers.

Tools use their own include root (`#include "loader/texture_loaders.h"`).

## Namespaces

- `Engine::` for all engine code (ECS, systems, components, resources, editor).
- `Core::` for low-level OpenGL wrappers from `vkmGL` (`Shader`, `Context`, `VertexArray`, ...).

## Key design patterns

| Pattern                  | Where                                          | Purpose                                                                                       |
|--------------------------|------------------------------------------------|-----------------------------------------------------------------------------------------------|
| Generational handles     | `StorageIndex`                                 | Prevent use-after-free for entities, resources, light slots                                   |
| Sparse-dense dual array  | `SparseSet<T>`, `Storage<T>`                   | O(1) add/remove/lookup with O(n) packed iteration                                             |
| Type erasure + TypeId    | `ISparseSet`, `typeId<T>()`                    | Open component registry without modifying Scene                                               |
| Lazy component creation  | `Scene::getStorage<T>()`                       | SparseSet created on first `add<T>()`                                                         |
| Fold expressions         | `forEach<A, B, ...>`                           | Compile-time multi-component query expansion                                                  |
| `if constexpr` dispatch  | `ResourceManager`                              | Type-safe routing to the correct Storage                                                      |
| Trait-based serialization| `SerializerTraits<T>` (`scene_serializer.cpp`) | Compile-time per-component serialize fold across `SerializedComponents` tuple                 |
| Frame-local snapshots    | `RenderView`                                   | Capture scene state, avoid concurrent mutation                                                |
| Render graph             | `RenderGraph` + `RGResource`                   | Ordered pass execution with declared transient reads/writes and auto MSAA resolve insertion   |
| Version-based GPU sync   | `Resource::version` + `GLView` tables          | Skip redundant GPU uploads                                                                    |
| Shader variant cache     | `GLView::resolveShaderVariant`                 | One compiled program per (shader, material-feature-bit) combination                           |
| Instanced rendering      | `GLInstanceBatcher`                            | Group sorted drawables by (material, mesh), one draw per batch                                |
| Work-stealing pool       | `ThreadPool`                                   | Local LIFO, global FIFO, steal from back                                                      |
| Layer scheduler          | `Engine::buildSchedule`                        | Greedy assignment of systems to parallel layers based on declared component access            |
| Command pattern          | `Command`, `CommandStack`                      | Editor undo/redo with merge-on-coalesce for drag interactions                                 |
| Staging-and-swap         | `SceneSerializer::load`                        | Transactional scene swap; failed loads leave the live scene untouched                         |
