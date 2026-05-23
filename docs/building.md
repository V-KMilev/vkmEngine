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
./build/bin/engine        # Linux / macOS
build\bin\engine.exe      # Windows (MSYS2 + Clang)
```

The executable is placed at `build/bin/engine[.exe]` (set by
`CMAKE_RUNTIME_OUTPUT_DIRECTORY` in the top-level CMakeLists).

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
| `VKM_PROFILER=1` | EngineCore (public) | Enables Tracy CPU+GPU zones via debug/profiler.h. Default ON in Debug, OFF otherwise. Pass `-DVKM_PROFILER=OFF` to disable in Debug. |
| `GLM_ENABLE_EXPERIMENTAL` | EngineCore (public) | GLM experimental features |
| `GLM_FORCE_INTRINSICS` | EngineCore (public) | GLM SIMD intrinsics |
| `APP_VERSION` | Executable | Project version string |
| `APP_ROOT_DIR` | Executable | Absolute path to project root |
| `APP_BRANCH`, `APP_COMMIT_HASH`, `APP_BUILD_DATE` | BuildInfo | Git metadata |

### Profiling with Tracy

When `VKM_PROFILER=1` (the default in Debug), the engine emits per-frame
`FrameMark`, per-stage CPU zones, and per-pass CPU+GPU zones over TCP.
Attach the Tracy profiler GUI (built separately from `modules/tracy/profiler`)
to inspect a live capture. Macros are in `src/engine/debug/profiler.h` -
engine code never includes Tracy headers directly.
