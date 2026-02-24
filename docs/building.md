# Building

## Prerequisites

- C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+)
- CMake 3.25+
- Ninja build system
- OpenGL 4.3 capable GPU and drivers

## Setup

Clone and initialize submodules:

```bash
git clone <repo-url>
cd vkmEngine
git submodule update --init --recursive
```

## Build Commands

```bash
# Configure
cmake -B build -G Ninja

# Build
cmake --build build

# Run
./build/engine
```

The executable is placed at `build/bin/engine`.

## CMake Targets

| Target | Type | Description |
|--------|------|-------------|
| `EngineCore` | Static lib | Core engine: ECS, resources, systems (animation/visibility/event), platform, debug |
| `EngineRendering` | Static lib | Render system, backend abstraction, pipeline, passes, view |
| `BackendOpenGL` | Static lib | OpenGL backend implementation (GLBackend, GLView, passes, GPU resources) |
| `EngineTools` | Static lib | Asset loaders and procedural generators |
| `EngineEditor` | Static lib | Editor UI, camera controller, panels, gizmo |
| `engine` | Executable | Main application linking all libraries |
| `BuildInfo` | Interface lib | Compile-time build metadata (version, branch, commit hash) |

### Dependency Graph

```
engine (executable)
  |-- EngineCore
  |     |-- vkmLog, vkmGL (glm, glfw, glew)
  |-- EngineRendering
  |     |-- EngineCore
  |-- BackendOpenGL
  |     |-- vkmGL, EngineRendering, EngineTools
  |-- EngineTools
  |     |-- EngineCore
  |-- EngineEditor
  |     |-- EngineCore, EngineRendering, imgui
  |-- BuildInfo
```

## External Modules

All external dependencies are git submodules under `modules/`:

| Module | Path | Provides |
|--------|------|----------|
| **vkmGL** | `modules/vkmGL` | OpenGL utilities, bundles GLFW, GLM, GLEW, stb_image |
| **vkmLog** | `modules/vkmLog` | Logging library (LOG_TRACE..LOG_FATAL), VKM_ASSERT |
| **imgui** | `modules/imgui` | Dear ImGui for editor UI |

## Shaders

GLSL shaders live in `shaders/`, each in its own folder:

```
shaders/
  pbr/           # PBR forward rendering
  aabb_debug/    # Wireframe AABB debug overlay
  grid/          # Infinite procedural grid
  gizmo/         # Navigation gizmo
```

Each folder contains `vertexShader.shader` and `fragmentShader.shader`. Loaded by path prefix:

```cpp
Core::Shader pbr("shaders/pbr");
```

## Compiler Flags

All targets are built with:
- `-Wall -Wextra` (warnings enabled)
- `-Wno-unused-value` (VKM_ASSERT expands to `true` in release)
- `-Wno-unused-parameter` (interface stubs)
- `-Wno-unused-variable` (system refs kept for readability)
- `-Wno-stringop-truncation` (Name component strncpy is intentional)

## Build Definitions

| Define | Scope | Purpose |
|--------|-------|---------|
| `ENABLE_STATISTICS_TRACKING=1` | EngineCore (public) | Enables STATS_RECORD_* macros |
| `GLM_ENABLE_EXPERIMENTAL` | EngineCore (public) | GLM experimental features |
| `GLM_FORCE_INTRINSICS` | EngineCore (public) | GLM SIMD intrinsics |
| `APP_VERSION` | Executable | Project version string |
| `APP_ROOT_DIR` | Executable | Absolute path to project root |
| `APP_BRANCH`, `APP_COMMIT_HASH`, `APP_BUILD_DATE` | BuildInfo | Git metadata |
