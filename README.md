# vkmEngine Documentation

A C++17 3D rendering engine with an OpenGL backend. Open type-erased ECS, a fixed 18-pass PBR forward renderer (Forward+ clustered lighting, CSM + spot + cube shadows, LTC area lights, IBL from an HDR or procedural sky, reflection probes, GTAO, contact shadows, froxel volumetric fog, decals, particles, DoF, bloom, tonemap), animation, hierarchy, physics, scripting, an event system, screen-space in-game UI (SDF text), an ImGui editor with undo/redo, and a transactional scene serializer.

## Quick Start

```bash
git submodule update --init --recursive
cmake -B build -G Ninja
cmake --build build
./build/bin/engine_editor     # engine + editor (or engine_runtime for the bare engine)
```

See [Building](docs/reference/building.md) for prerequisites and CMake targets.

## Documentation Index

Reference docs and contributor guides live under [docs/](docs/).

### Core

- [Architecture](docs/reference/architecture.md) - High-level design, per-stage scheduler, FrameContext, directory layout, conventions
- [Building](docs/reference/building.md) - Prerequisites, build commands, CMake targets, external modules
- [ECS](docs/reference/ecs.md) - Scene, entities, components, queries, hierarchy
- [Resources](docs/reference/resources.md) - Resource management, asset types, handles, versioning, internal/external assets

### Systems

- [Rendering](docs/reference/system/rendering.md) - Backend seam, the fixed 18-pass forward pipeline, RenderView contract, material preview
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
app/               Header-only bootstrap (setupEngineApp) shared by both executables
game/              Hot-reloadable gameplay modules (Behavior DLLs)
modules/
  vkmGL            Submodule: GL utilities + GLFW + GLM + GLEW + stb_image
  vkmLog           Submodule: logging + VKM_ASSERT
  imgui            Submodule: Dear ImGui
  assimp / freetype / json / stb / tracy
                   Submodules: model import, font rasterization, JSON, image IO, profiling
shaders/           GLSL source (one folder per program)
scenes/            Sample scenes (.json) for editor open
assets/            Textures, materials, meshes referenced by scenes
example/           Standalone scene generators used at boot
```

See [Architecture](docs/reference/architecture.md) for the full per-directory breakdown.
