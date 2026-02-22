# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure (from project root)
cmake -B build -G Ninja

# Build
cmake --build build

# Run the engine
./build/engine
```

The project uses CMake 3.25+ with Ninja generator. C++17 standard is required.

## Architecture Overview

This is a 3D game/rendering engine with an OpenGL backend, structured around these core systems:

### ECS (Entity-Component-System)
- **Scene** (`src/engine/ecs/scene.h`) - Open type-erased registry; any type can be a component without modifying Scene
- Components stored in `SparseSet<T>` (dense-packed, sparse-indexed) via `ISparseSet` type erasure, created on first use
- Entity lifetime managed by `SlotAllocator` (generation-safe handles with recycling)
- `TypeId` / `typeId<T>()` for compile-time type-to-integer mapping (`src/engine/core/memory/types.h`)
- Multi-component `forEach<A, B>(fn)` for querying entities with multiple component types
- `components.h` convenience header bundles all component includes
- `Storage<T>` used only by ResourceManager (generational arena with versioning)
- Entities are `EntityId` (alias for `StorageIndex`), components accessed via `scene.get<T>(entity)`

### Resource Management
- **ResourceManager** (`src/engine/resources/resource_manager.h`) - Manages assets with typed handles
- Three resource types: `MeshAsset`, `TextureAsset`, `MaterialAsset` with corresponding handles (`MeshHandle`, etc.)
- Versioned storage enables GPU sync detection - `commit()` bumps version after edits

### Systems Layer
- **System** (`src/engine/core/system.h`) - Abstract base class; all systems implement `update(FrameContext&)`
- **FrameContext** - Per-frame bundle: `Scene&`, `ResourceManager&`, `deltaTime`, viewport size, `Visibility`
- **Engine** (`src/engine/core/engine.h`) - Singleton owning Scene, ResourceManager, system pipeline, and main loop
- Systems registered via `Engine::addSystem()` and executed sequentially per frame

### Rendering Pipeline
- **RenderSystem** (`src/engine/rendering/`) - Orchestrates rendering, owns backend and pass pipeline
- **RenderBackend** - Abstract interface (OpenGL, Optix, CPU planned)
- **RenderPass** - Abstract pass interface; passes execute sequentially
- **GLBackend** (`src/backend/opengl/`) - OpenGL implementation with `GLView` managing GPU resources
- Current passes: Forward (PBR), AABB Debug, Grid, Navigation Gizmo

### Visibility System
- **VisibilitySystem** (`src/engine/visibility/visibility_system.h`) - Inherits `System`, runs culling per frame
- Populates `FrameContext.visibility` before RenderSystem and AnimationSystem consume it
- Produces `Visibility` struct with visible entity IDs and pre-computed model matrices
- Culling stages: frustum, distance, screen-size

### Threading
- **ThreadPool** (`src/engine/platform/threading/thread_pool.h`) - Singleton work-stealing pool
- `parallelFor(begin, end, grain, func)` for parallel iteration
- `submit()` returns future, `enqueue()` for fire-and-forget

### Events
- **EventSystem** (`src/engine/events/event_system.h`) - Inherits `System`, runs queued events per frame
- Push/pop queue for prioritized events + named listener pub/sub
- Thread-safe, async dispatch via ThreadPool

### Platform/Window
- GLFW-based windowing via **WindowManager** (Engine-owned, not a singleton)
- Input handling through `KeyboardInputHandle`, `MouseInputHandle`
- Frame rate limiting in `FrameLimiter`

## Key Directories

```
src/
├── engine/                     ← single include root (src/engine/)
│   ├── core/                   # Engine singleton, System base
│   │   └── memory/             # TypeId, Storage, SparseSet, SlotAllocator
│   ├── ecs/                    # Scene, Entity
│   │   └── component/          # Transform, Camera, Mesh, Animation, Light
│   ├── render/                 # RenderSystem, RenderBackend, RenderView, RenderPipeline, RenderPass
│   ├── resource/               # ResourceManager, Resource, ResourceHandle, MeshAsset, MaterialAsset, TextureAsset
│   ├── visibility/             # Visibility, VisibilityContext, BoundsUtils
│   │   └── culling/            # FrustumCuller, DistanceCulling, ScreenSizeCulling, OcclusionCuller
│   ├── animation/              # AnimationSystem, AnimationTrack, Keyframe, Easing
│   ├── event/                  # EventSystem, Event, EventListener
│   ├── platform/
│   │   ├── window/             # WindowManager, Window, InputHandle, FrameLimiter
│   │   └── threading/          # ThreadPool
│   ├── debug/                  # Statistics, FrameTracker, CallTracker, FrameInfo, PrintHelper
│   └── editor/                 # CameraController
├── backend/opengl/             # OpenGL backend (flat gl_-prefixed includes)
│   ├── core/                   # GLBackend, GLView, GLInstanceBatcher
│   ├── resource/               # GLMesh, GLMaterial, GLTexture, GLLights, GLInstanceBuffer
│   ├── config/                 # GLConfig, GLTextureMapping
│   └── pass/                   # GLForwardPass, GLAABBDebugPass, GLGridPass, GLNavigationGizmoPass
└── tools/                      ← single include root (src/tools/)
    ├── loader/                 # TextureLoaders, MaterialLoaders
    └── generator/              # MeshGenerators, TextureGenerators, MaterialGenerators, LightGenerators
```

## Include Convention

All engine includes use **module-qualified paths** from the single include root (`src/engine/`):
```cpp
#include "core/engine.h"              // not "engine.h"
#include "ecs/scene.h"                // not "scene.h"
#include "ecs/component/transform.h"  // not "transform.h"
#include "render/render_pass.h"       // not "render_pass.h"
#include "resource/mesh_asset.h"      // not "mesh_asset.h"
#include "debug/statistics.h"         // not "statistics.h"
```

Backend-internal includes stay flat (`#include "gl_backend.h"`) since all files are gl_-prefixed.
Tools includes use their root (`#include "loader/texture_loaders.h"`, `#include "generator/mesh_generators.h"`).

## External Modules

- **vkmGL** (`modules/vkmGL`) - OpenGL utilities, bundles GLFW, GLM, GLEW, stb_image
- **vkmLog** (`modules/vkmLog`) - Logging library

Initialize submodules: `git submodule update --init --recursive`

## Shaders

Shaders are in `shaders/` directory, each in its own folder with `vertexShader.shader` and `fragmentShader.shader`. Loaded by path prefix (e.g., `Core::Shader pbr("shaders/pbr")`).

## Coding Patterns

- Singletons use `::get()` pattern (Engine, ThreadPool only). WindowManager and StatisticTracker are Engine-owned. EventSystem is a standalone System.
- Classes are non-copyable/non-movable where resource ownership is involved
- Template metaprogramming with `if constexpr` for type dispatch in ResourceManager
- VKM_ASSERT macro for debug assertions (from vkmLog)
- Namespace `Engine::` for engine code, `Core::` for low-level OpenGL wrappers
