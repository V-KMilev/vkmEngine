# vkmEngine Documentation

A C++17 3D rendering engine with an OpenGL backend. Open type-erased ECS, a fixed 19-pass PBR forward renderer (Forward+ clustered lighting, CSM + spot + cube shadows, LTC area lights, IBL from an HDR or procedural sky, reflection probes, GTAO, froxel volumetric fog, decals, particles, DoF, bloom, tonemap), animation, hierarchy, physics, scripting, an event system, screen-space in-game UI (SDF text), an ImGui editor with undo/redo, and a transactional scene serializer.

## Quick Start

```bash
git submodule update --init --recursive
cmake -B build -G Ninja
cmake --build build
./build/bin/engine_editor examples/potion_runner    # edit a project
./build/bin/engine_runtime examples/potion_runner   # play it
./build/bin/engine_cook examples/potion_runner      # bake its assets, no window
```

The engine runs **projects**: a directory with a `project.json`, its own scenes and
assets, and its gameplay code built into its own `bin/`. All three executables find
one the same way - the project beside the executable, unless an argument names a
different one. `examples/` holds two complete ones.

See [Building](docs/reference/building.md) for prerequisites and CMake targets.

## Documentation Index

Reference docs and contributor guides live under [docs/](docs/).

### Core

- [Architecture](docs/reference/architecture.md) - High-level design, per-stage scheduler, FrameContext, directory layout, conventions
- [Building](docs/reference/building.md) - Prerequisites, build commands, CMake targets, external modules
- [ECS](docs/reference/ecs.md) - Scene, entities, components, queries, hierarchy
- [Resources](docs/reference/resources.md) - Resource management, asset types, handles, versioning, internal/external assets

### Systems

- [Rendering](docs/reference/system/rendering.md) - Backend seam, the fixed 19-pass forward pipeline, RenderView contract, material preview
- [Lighting](docs/reference/system/lighting.md) - Light types, area lights (Rect/Disk via LTC), shadow atlas + cube shadows, IBL
- [Visibility](docs/reference/system/visibility.md) - Culling pipeline (frustum / distance / screen-size), parallel dispatch
- [Hierarchy](docs/reference/system/hierarchy.md) - HierarchySystem, HierarchyOperations, parallel world-transform resolve
- [Animation](docs/reference/system/animation.md) - Animation tracks, keyframes, easing, two-phase update
- [Events](docs/reference/system/events.md) - Typed pub/sub, sync emit vs deferred enqueue, listener safety
- [IO & Serialization](docs/reference/system/io.md) - Scene / asset / component serializers, transactional load, FileWatcher
- [Scripting](docs/reference/system/scripting.md) - Behavior lifecycle, ScriptComponent, DLL hot-reload
- [Physics](docs/reference/system/physics.md) - Fixed-step rigid bodies, box colliders, contact solver
- [UI](docs/reference/system/ui.md) - Screen-space in-game UI (canvas/element/image/text/button)

### Platform

- [Threading](docs/reference/threading.md) - Shared-deque thread pool, parallelFor, main-thread participation
- [Editor](docs/reference/editor.md) - Panels, gizmos, undo/redo, viewport RTT, material preview, keybinds

### Contributor guides

- [Code style](docs/guides/code-style.md) - File layout, naming, formatting, comment styles, anti-patterns
- [Development](docs/guides/development.md) - How to fit a change to the engine's structure and goals
- [Implementation](docs/guides/implementation.md) - What makes an implementation good: simple, clean, not speculative

## Source Layout

```
src/
  engine/          Engine code (single include root: src/engine/)
  backend/opengl/  OpenGL backend (flat gl_-prefixed includes)
  editor/          ImGui editor (panels, gizmo, framework, overlays, ui, input)
  tools/           Asset loaders and procedural generators (src/tools/)
app/               The three executables + the bootstrap (setupEngineApp) two of them share
examples/          Complete projects (Potion Runner, Stress Arena) - gameplay lives here
modules/
  vkmGL            Submodule: GL object wrappers + shader loading (vendors GLEW)
  vkmLog           Submodule: logging + VKM_ASSERT
  glm / glfw / stb Submodules: math, windowing, image + font decode
  imgui            Submodule: Dear ImGui
  assimp / freetype / json / tracy
                   Submodules: model import, font rasterization, JSON, profiling
shaders/           GLSL source (one folder per program)
assets/            Textures, materials, meshes referenced by scenes
example/           Standalone scene generators used at boot
```

See [Architecture](docs/reference/architecture.md) for the full per-directory breakdown.
