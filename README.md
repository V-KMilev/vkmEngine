# vkmEngine Documentation

A C++17 3D rendering engine with an OpenGL backend. ECS core, PBR forward rendering with a render graph, area lights (LTC), shadow atlas + cube shadows, IBL, GTAO, SSR, TAA, DoF, motion blur, bloom, auto-exposure, AgX/PBR-Neutral/ACES tone mapping, animation, hierarchy, event system, an ImGui editor with undo/redo, and a transactional scene serializer.

## Quick Start

```bash
git submodule update --init --recursive
cmake -B build -G Ninja
cmake --build build
./build/bin/engine
```

See [Building](docs/building.md) for prerequisites and CMake targets.

## Documentation Index

### Core

- [Architecture](docs/architecture.md) - High-level design, per-stage scheduler, FrameContext, directory layout, conventions
- [Building](docs/building.md) - Prerequisites, build commands, CMake targets, external modules
- [ECS](docs/ecs.md) - Scene, entities, components, queries, hierarchy
- [Resources](docs/resources.md) - Resource management, asset types, handles, versioning, internal/external assets

### Systems

- [Rendering](docs/system/rendering.md) - Render graph, backend abstraction, passes, shader variant cache, material preview
- [Lighting](docs/system/lighting.md) - Light types, area lights (Rect/Disk via LTC), shadow atlas + cube shadows, IBL
- [Visibility](docs/system/visibility.md) - Culling pipeline (frustum / distance / screen-size), parallel dispatch
- [Hierarchy](docs/system/hierarchy.md) - HierarchySystem, HierarchyOperations, parallel world-transform resolve
- [Animation](docs/system/animation.md) - Animation tracks, keyframes, easing, two-phase update
- [Events](docs/system/events.md) - Typed pub/sub, sync emit vs deferred enqueue, listener safety
- [IO & Serialization](docs/system/io.md) - Scene / asset / component serializers, transactional load, FileWatcher

### Platform

- [Threading](docs/threading.md) - Work-stealing pool, parallelFor, per-stage layer scheduler
- [Editor](docs/editor.md) - Panels, gizmos, undo/redo, viewport RTT, material preview, keybinds

### Other

- [Code style guide](docs/misc/code_style_guide.md) - File layout, naming, formatting, comment styles, anti-patterns

## Source Layout

```
src/
  engine/          Engine code (single include root: src/engine/)
  backend/opengl/  OpenGL backend (flat gl_-prefixed includes)
  editor/          ImGui editor (panels, gizmo, framework, overlays, ui, input)
  tools/           Asset loaders and procedural generators (src/tools/)
modules/
  vkmGL            Submodule: GL utilities + GLFW + GLM + GLEW + stb_image
  vkmLog           Submodule: logging + VKM_ASSERT
  imgui            Submodule: Dear ImGui
shaders/           GLSL source (one folder per program)
scenes/            Sample scenes (.json) for editor open
assets/            Textures, materials, meshes referenced by scenes
examples/          Standalone scene generators used at boot
```

See [Architecture](docs/architecture.md) for the full per-directory breakdown.
