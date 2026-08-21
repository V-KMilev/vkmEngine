<div align="center">

<img src="assets/logo/vkm_engine_logo.png" alt="vkmEngine" width="440">

### Build a world. Write the gameplay in C++. Ship the game.

![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![OpenGL 4.3](https://img.shields.io/badge/OpenGL-4.3-5586A4?style=flat-square&logo=opengl&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-Ninja-064F8C?style=flat-square&logo=cmake&logoColor=white)
![Windows | Linux](https://img.shields.io/badge/Windows%20%7C%20Linux-informational?style=flat-square)

</div>

<img src="docs/images/stress_arena.png" alt="The stress arena">

## The engine

Three binaries share one project directory. `vkm_editor` authors it, `vkm_runtime` plays it,
`vkm_cook` bakes its assets without a window. Gameplay is C++ compiled into the project's own
module and hot-reloaded while the editor stays open.

**Rendering.** A 19-pass forward pipeline: Forward+ clustered lighting, cascaded, spot and cube
shadows, LTC area lights, IBL from an HDR or a procedural sky, reflection probes, baked
irradiance volumes, GTAO, volumetric fog, decals, particles, depth of field, bloom, MSAA and GPU
Hi-Z occlusion culling.

**Characters.** Skeletal import, GPU skinning through the depth, forward and shadow passes, clip
blending, bone sockets, capsule colliders and a character controller.

**Core.** An open type-erased ECS over sparse sets, generational handles, a staged system
pipeline, compile-time field reflection, a job system and a typed event bus.

**Authoring.** Scene editing with a transform gizmo and full undo/redo, prefabs with per-instance
overrides, material and render-settings panels, an asset browser, and a cooking pipeline that
turns source art into engine formats.

<img src="docs/images/in_engine_editor.png" alt="The editor">

<img src="docs/images/potion_runner.png" alt="Potion Runner">

## Quick Start

```bash
git submodule update --init --recursive
cmake -B build -G Ninja
cmake --build build
./build/bin/vkm_editor examples/potion_runner    # edit a project
./build/bin/vkm_runtime examples/potion_runner   # play it
./build/bin/vkm_cook examples/potion_runner      # bake its assets, no window
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
```

See [Architecture](docs/reference/architecture.md) for the full per-directory breakdown.
