# vkmEngine Documentation

A C++17 3D rendering engine with an OpenGL backend. Open type-erased ECS, a fixed 10-pass PBR forward renderer (CSM + spot + cube shadows, LTC area lights, IBL, reflection probes, GTAO, SSR, motion blur, bloom, tonemap), animation, hierarchy, physics, scripting, an event system, an ImGui editor with undo/redo, and a transactional scene serializer.

## Quick Start

```bash
git submodule update --init --recursive
cmake -B build -G Ninja
cmake --build build
./build/bin/engine
```

See [Building](claude_helper/reference/building.md) for prerequisites and CMake targets.

## Documentation Index

Reference docs and contributor guides live under [claude_helper/](claude_helper/).

### Core

- [Architecture](claude_helper/reference/architecture.md) - High-level design, per-stage scheduler, FrameContext, directory layout, conventions
- [Building](claude_helper/reference/building.md) - Prerequisites, build commands, CMake targets, external modules
- [ECS](claude_helper/reference/ecs.md) - Scene, entities, components, queries, hierarchy
- [Resources](claude_helper/reference/resources.md) - Resource management, asset types, handles, versioning, internal/external assets

### Systems

- [Rendering](claude_helper/reference/system/rendering.md) - Backend seam, the fixed 10-pass forward pipeline, RenderView contract, material preview
- [Lighting](claude_helper/reference/system/lighting.md) - Light types, area lights (Rect/Disk via LTC), shadow atlas + cube shadows, IBL
- [Visibility](claude_helper/reference/system/visibility.md) - Culling pipeline (frustum / distance / screen-size), parallel dispatch
- [Hierarchy](claude_helper/reference/system/hierarchy.md) - HierarchySystem, HierarchyOperations, parallel world-transform resolve
- [Animation](claude_helper/reference/system/animation.md) - Animation tracks, keyframes, easing, two-phase update
- [Events](claude_helper/reference/system/events.md) - Typed pub/sub, sync emit vs deferred enqueue, listener safety
- [IO & Serialization](claude_helper/reference/system/io.md) - Scene / asset / component serializers, transactional load, FileWatcher

### Platform

- [Threading](claude_helper/reference/threading.md) - Shared-deque thread pool, parallelFor, main-thread participation
- [Editor](claude_helper/reference/editor.md) - Panels, gizmos, undo/redo, viewport RTT, material preview, keybinds

### Contributor guides

- [Code style](claude_helper/guides/code-style.md) - File layout, naming, formatting, comment styles, anti-patterns
- [Development](claude_helper/guides/development.md) - How to fit a change to the engine's structure and goals
- [Implementation](claude_helper/guides/implementation.md) - What makes an implementation good: simple, clean, not speculative

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

See [Architecture](claude_helper/reference/architecture.md) for the full per-directory breakdown.
