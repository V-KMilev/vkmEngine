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

# Run (two executables build by default)
./build/bin/engine_editor          # engine + in-process editor (Linux / macOS)
./build/bin/engine_runtime         # bare engine, no editor
./build/bin/engine_runtime scenes/level1.json   # runtime can boot a saved scene
build\bin\engine_editor.exe        # Windows (MSYS2 + Clang)
```

Both executables (and the game module DLL) land in `build/bin/` - one directory
so an exe finds its DLLs (set by `CMAKE_RUNTIME_OUTPUT_DIRECTORY` in the
top-level CMakeLists). `-DVKM_WITH_EDITOR=OFF` builds only `engine_runtime` and
skips `src/editor` entirely, proving the engine carries no hidden dependency on
editor code.

## CMake Targets

| Target | Type | Description |
|--------|------|-------------|
| `EngineCore` | Static lib | Core engine: ECS, resources, IO, the non-render systems (animation/visibility/event/physics/script/hierarchy/...), platform, debug |
| `EngineRendering` | Static lib | Render system, backend abstraction, render view |
| `BackendOpenGL` | Static lib | OpenGL backend implementation (GLBackend, GLView, passes, GPU resources) |
| `EngineTools` | Static lib | Procedural generators + the runtime-safe cooked-asset loaders/factories. No Assimp, no heavy image decode |
| `EngineCooker` | Static lib | Editor-only: the heavy importers (Assimp model import, stb image decode) + the asset cooker that bakes recipes into the cooked cache (only when `VKM_WITH_EDITOR=ON`) |
| `EngineEditor` | Static lib | Editor UI, panels, overlays, gizmo, scene I/O (only when `VKM_WITH_EDITOR=ON`) |
| `game` | Static lib | Concrete gameplay behaviors, static-linked into the runtime (no hot-reload) |
| `game_module` | Shared lib | The same gameplay sources built as `game.dll`/`libgame.so` for the editor to hot-reload |
| `engine_runtime` | Executable | Bare engine: the engine libs + static `game`, no editor. Includes `app/engine_app.h` for the shared bootstrap; links no Assimp |
| `engine_editor` | Executable | Engine libs + `EngineEditor` + `EngineCooker`, loads `game_module` for hot-reload (only when `VKM_WITH_EDITOR=ON`) |
| `EngineHeaders` | Interface lib | Include-only view of EngineCore's public API; the hot-reload module compiles against it without linking EngineCore's objects |
| `BuildInfo` | Interface lib | Compile-time build metadata (version, branch, commit hash) |
| `vkm_warnings` | Interface lib | Shared GCC/Clang warning flags; first-party targets opt in, submodules don't |

### Dependency Graph

```
engine_editor (executable)              engine_runtime (executable)
  |-- EngineCore                          |-- EngineCore (glm, glfw, vkmLog, nlohmann_json; glew private)
  |-- EngineRendering -- EngineCore       |-- EngineRendering -- EngineCore
  |-- EngineTools -- EngineCore           |-- EngineTools -- EngineCore (generators + cooked loaders; no Assimp)
  |-- BackendOpenGL -- vkmGL, ...         |-- BackendOpenGL -- vkmGL, EngineRendering, EngineTools
  |-- EngineCooker -- EngineCore,         |-- game -- EngineCore
  |     EngineTools (+ assimp private)    |-- BuildInfo
  |-- EngineEditor -- EngineCore,
  |     EngineTools, EngineCooker, imgui, vkmGL
  |-- BuildInfo

Both executables #include app/engine_app.h for setupEngineApp (no EngineApp lib).
Only engine_editor links EngineCooker, so engine_runtime pulls in no Assimp.

game_module (shared, editor only) -- EngineHeaders (include-only); resolves engine
  symbols from the engine_editor exe at load (Windows import lib / Linux -rdynamic).
```

## External Modules

All external dependencies are git submodules under `modules/` (see `.gitmodules`):

| Module | Path | Provides |
|--------|------|----------|
| **vkmGL** | `modules/vkmGL` | OpenGL utilities, bundles GLFW, GLM, GLEW, stb_image |
| **vkmLog** | `modules/vkmLog` | Logging library (LOG_TRACE..LOG_FATAL), VKM_ASSERT |
| **imgui** | `modules/imgui` | Dear ImGui for editor UI |
| **json** | `modules/json` | nlohmann/json (`nlohmann_json`); serialization + asset `source` descriptors |
| **assimp** | `modules/assimp` | Model import (glTF/OBJ/FBX/DAE/STL/PLY/3DS), trimmed to the importers used; linked privately by EngineTools |
| **tracy** | `modules/tracy` | Tracy profiler client (`TracyClient`), linked by EngineCore only when `VKM_PROFILER` is on |

## Shaders

GLSL shaders live in `shaders/`, one folder per program, grouped by pipeline
stage (each backend pass owns its program):

```
shaders/
  forward/      # forward shading: pbr/ (the ubershader), phong/, prepass/
  shadow/       # shadow_2d/, shadow_cube/
  ibl/          # equirect/, irradiance/, prefilter/, brdf/  (IBL bake)
  gtao/  ssr/  motion_blur/  bloom/  skybox/  composite/  grid/
  _generated/   # engine_config.glsl, generated from engine_config.h at configure time
```

Each folder contains the program's source files, named after the GL stage they
target. The loader (vkmGL) hard-codes these names:

| Stage      | Filename             | Required?                          |
|------------|----------------------|------------------------------------|
| Vertex     | `vertex.shader`      | Required for graphics programs     |
| Fragment   | `fragment.shader`    | Required for graphics programs     |
| Geometry   | `geometry.shader`    | Optional; loaded if present        |
| Compute    | `computeShader.shader` | A compute-only program (`Core::ComputeShader`) |

A program is loaded by path prefix:

```cpp
Core::Shader pbr("shaders/forward/pbr");
```

The engine's own shader preprocessor resolves `#include` directives between
`.shader`/`.glsl` files (cycle-safe). `shaders/_generated/engine_config.glsl` is
derived from `engine_config.h` so cross-language constants have one C++ source -
though the forward shaders currently still hand-define their copies (see
[lighting.md](system/lighting.md#limits-and-the-must-match-shader-contract)).

## Compiler Flags

First-party targets opt into a shared warning set by linking the `vkm_warnings`
interface lib (GCC/Clang dialect only; MSVC is left untouched). Submodules do
**not** link it, so third-party code keeps its own warning level:
- `-Wall -Wextra` (warnings enabled)
- `-Wno-unused-value` (VKM_ASSERT expands to `true` in release)
- `-Wno-unused-parameter` (interface stubs)
- `-Wno-unused-variable` (system refs kept for readability)
- `-Wno-stringop-truncation` (Name component strncpy is intentional)

First-party code also builds as strict C++17 (`CMAKE_CXX_EXTENSIONS OFF`).

## Build Definitions

| Define | Scope | Purpose |
|--------|-------|---------|
| `VKM_PROFILER=1` | EngineCore (public) | Enables Tracy CPU+GPU zones via debug/profiler.h. Default ON unless `CMAKE_BUILD_TYPE` is exactly `Release`. Pass `-DVKM_PROFILER=OFF` to force off. |
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
