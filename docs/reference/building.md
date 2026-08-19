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

An in-source build is refused rather than warned about, and a configure that
names no build type gets `RelWithDebInfo` - an unset one silently means neither
optimisation nor debug info.

```bash
# Configure
cmake -B build -G Ninja

# Build
cmake --build build

# Run (three executables build by default)
./build/bin/vkm_editor examples/potion_runner    # edit a project
./build/bin/vkm_runtime examples/potion_runner   # play it
./build/bin/vkm_cook examples/potion_runner      # bake its assets, no window
build\bin\vkm_editor.exe        # Windows (MSYS2 + Clang)
```

All three take **a project directory**, and all three apply the same rule: the
project is the one beside the executable, unless an argument names a different
one. So a shipped game ships its exe next to its `project.json` and the player
passes nothing. See [system/io.md](system/io.md#projects-and-the-two-roots) for
what a project is and how the two roots divide engine data from project data.

The executables land in `build/bin/` - one directory so an exe finds its DLLs
(set by `CMAKE_RUNTIME_OUTPUT_DIRECTORY` in the top-level CMakeLists). Each
project's gameplay module builds into that project's own `bin/` instead, because
it belongs to the project rather than to this build tree.

## CMake Targets

| Target | Type | Description |
|--------|------|-------------|
| `vkm_core` | **Shared lib** | Core engine: ECS, resources, IO, the non-render systems (animation/visibility/event/physics/script/hierarchy/...), platform, debug |
| `vkm_render` | **Shared lib** | Render system, backend abstraction, render view |
| `vkm_backend_gl` | Static lib | OpenGL backend implementation (GLBackend, GLView, passes, GPU resources) |
| `vkm_tools` | Static lib | Procedural generators + the runtime-safe cooked-asset loaders/factories. No Assimp, no heavy image decode |
| `vkm_cook` | Static lib | The heavy importers (Assimp model import, stb image decode) + the asset cooker that bakes recipes into the cooked cache. Linked by `vkm_editor_app` and `vkm_cook_app` only |
| `vkm_editor` | Static lib | Editor UI, panels, overlays, gizmo, scene I/O |
| `vkm_headers` | Interface lib | Include-only view of vkm_core's public API; the hot-reload module compiles against it without linking vkm_core's objects |
| `vkm_build_info` | Interface lib | Compile-time build metadata (version, branch, commit hash) |
| `vkm_warnings` | Interface lib | Shared GCC/Clang warning flags; first-party targets opt in, submodules don't |
| `<project>_module` | Shared lib | One per project (`potion_runner_module`, `stress_arena_module`): that project's gameplay sources built as `game.dll`/`libgame.so` into the project's own `bin/`. The engine ships no gameplay of its own |
| `vkm_runtime_app` | Executable | Bare engine, no editor. Includes `app/engine_app.h` for the shared bootstrap; links no Assimp and no ImGui. Runs as `vkm_runtime` |
| `vkm_editor_app` | Executable | Engine libs + `vkm_editor` + `vkm_cook`; loads the open project's module for hot-reload. Runs as `vkm_editor` |
| `vkm_cook_app` | Executable | Headless asset cook: `vkm_cook` with no window, no GL context and no `Engine`, so it runs over SSH and on CI. Runs as `vkm_cook` |

Only the executable targets carry a suffix, and all three carry it so it reads
as "this is the application" rather than "this one had a clash". A CMake target
name must be unique across the project and two of the three are already library
names; the files never collide - `vkm_editor` sits beside `libvkm_editor.a`, and
`vkm_editor.exe` beside `vkm_editor.dll` - so `OUTPUT_NAME` drops the suffix and
nobody types it outside these build files.

`vkm_core` and `vkm_render` are shared on purpose. A gameplay module has
to reach engine symbols without carrying a second copy - two copies mean two
typeId registries and two sets of singletons - and a static engine can only
manage that by having the module resolve symbols from the host executable. That
works on Linux and binds a module to one specific exe on Windows, so a module
built for the editor could not be loaded by the runtime. One shared library both
link against removes the question.

## Installing an SDK

Building the engine and shipping it are different things. `cmake --install`
produces an **SDK**, not a game:

```bash
cmake --install build --prefix /path/to/sdk
```

```
<prefix>/bin/       the three hosts, the shared engine, and the vkm command
<prefix>/include/   the engine's public headers plus the third-party headers
                    they reach into
<prefix>/lib/cmake/vkmEngine/   what find_package(vkmEngine) loads
<prefix>/shaders/   engine shaders
<prefix>/templates/ what `vkm new` copies
```

A downloadable archive comes from CPack, and carries the compiler in its name
because the engine is not ABI-stable across compilers:

```bash
cmake --build build --target package
# -> vkmEngine-1.4.0-Linux-x86_64-GNU-12.3.0.tar.xz
```

Building a game *with* that SDK is [getting-started.md](../guides/getting-started.md).
Nothing there involves writing CMake.

### Dependency Graph

```
vkm_editor_app (executable)             vkm_runtime_app (executable)
  |-- vkm_core                            |-- vkm_core (glm, glfw, vkm_log, nlohmann_json; glew private)
  |-- vkm_render -- vkm_core              |-- vkm_render -- vkm_core
  |-- vkm_tools -- vkm_core               |-- vkm_tools -- vkm_core (generators + cooked loaders; no Assimp)
  |-- vkm_backend_gl -- vkm_gl, ...       |-- vkm_backend_gl -- vkm_gl, vkm_render, vkm_tools
  |-- vkm_cook -- vkm_core,               |-- vkm_build_info
  |     vkm_tools (+ assimp private)
  |-- vkm_editor -- vkm_core,           vkm_cook_app (executable)
  |     vkm_tools, vkm_cook,              |-- vkm_cook -- vkm_core, vkm_tools
  |     imgui, vkm_gl                     |-- vkm_build_info
  |-- vkm_build_info                      (no window, no GL, no Engine)

vkm_editor_app and vkm_runtime_app #include app/engine_app.h for setupEngineApp
(no EngineApp lib). vkm_cook_app does not: it constructs a Scene and a
ResourceManager directly and never builds an Engine, which is what lets it run
headless. vkm_runtime_app links neither vkm_cook nor vkm_editor, so it pulls in
no Assimp and no ImGui - the link lists enforce that, not a build flag.

<project>_module (shared, per project) -- vkm_headers (include-only); it must
  not link vkm_core: a second copy would duplicate the typeId registry and the
  singletons, and components registered on one copy are invisible to the other.
  It resolves engine symbols from the host that loaded it. On Linux that is
  whichever host did, since ENABLE_EXPORTS is set on vkm_editor_app and
  vkm_runtime_app alike. On Windows a shared library must name an import library
  at link time, so a module binds to vkm_editor_app specifically and the runtime
  cannot load it there.
```

## External Modules

All external dependencies are git submodules under `modules/` (see `.gitmodules`):

| Module | Path | Provides |
|--------|------|----------|
| **vkmGL** | `modules/vkmGL` | OpenGL object wrappers (`Vkm::GL::`), shader loading/preprocessing. Vendors GLEW privately; everything else it needs is a target the engine supplies |
| **vkmLog** | `modules/vkmLog` | Logging library (`Vkm::Log::`): LOG_TRACE..LOG_FATAL, VKM_ASSERT |
| **glm** | `modules/glm` | Vector/matrix math, used engine-wide and by vkmGL |
| **glfw** | `modules/glfw` | Window + input platform layer |
| **stb** | `modules/stb` | `stb_image` (texture decode), `stb_truetype` (SDF font bake) |
| **imgui** | `modules/imgui` | Dear ImGui for editor UI |
| **freetype** | `modules/freetype` | Font rasterizer, trimmed to the core; vendored so the build does not depend on a system libfreetype |
| **json** | `modules/json` | nlohmann/json (`nlohmann_json`); serialization + asset `source` descriptors |
| **assimp** | `modules/assimp` | Model import (glTF/OBJ/FBX/DAE/STL/PLY/3DS), trimmed to the importers used; linked privately by vkm_tools |
| **tracy** | `modules/tracy` | Tracy profiler client (`TracyClient`), linked by vkm_core only when `VKM_PROFILER` is on |

## Shaders

GLSL shaders live in `shaders/`, one folder per program, grouped by pipeline
stage (each backend pass owns its program):

```
shaders/
  forward/      # forward shading: pbr/ (the ubershader), phong/, prepass/
  shadow/       # shadow_2d/, shadow_cube/
  ibl/          # equirect/, irradiance/, prefilter/, brdf/  (IBL bake)
  gtao/  bloom/  skybox/  composite/  grid/
  clustering/  fog/  dof/  decal/  particle/  irradiance/  ui/
  _generated/   # engine_config.glsl, generated from engine_config.h at configure time
```

Each folder contains the program's source files, named after the GL stage they
target. The loader (vkmGL) hard-codes these names:

| Stage      | Filename             | Required?                          |
|------------|----------------------|------------------------------------|
| Vertex     | `vertex.shader`      | Required for graphics programs     |
| Fragment   | `fragment.shader`    | Required for graphics programs     |
| Geometry   | `geometry.shader`    | Optional; loaded if present        |
| Compute    | `computeShader.shader` | A compute-only program (`Vkm::GL::ComputeShader`) |

A program is loaded by path prefix:

```cpp
Vkm::GL::Shader pbr("shaders/forward/pbr");
```

The engine's own shader preprocessor resolves `#include` directives between
`.shader`/`.glsl` files (cycle-safe). `shaders/_generated/engine_config.glsl` is
derived from `engine_config.h` so cross-language constants have one C++ source -
though the forward shaders currently still hand-define their copies (see
[lighting.md](system/lighting.md#limits-and-the-generated-constants-contract)).

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
| `VKM_PROFILER=1` | vkm_core (public) | Enables Tracy CPU+GPU zones via debug/profiler.h. Default ON unless `CMAKE_BUILD_TYPE` is exactly `Release`. Pass `-DVKM_PROFILER=OFF` to force off. |
| `GLM_ENABLE_EXPERIMENTAL` | vkm_core (public) | GLM experimental features |
| `GLM_FORCE_INTRINSICS` | vkm_core (public) | GLM SIMD intrinsics |
| `APP_VERSION` | Executable | Engine version string |
| `APP_ROOT_DIR` | Executable | Absolute path to the **engine** root - the fallback `ProjectPaths::engineRoot()` uses to find `shaders/` and `assets/` when the exe is run from a build tree. Not the project root; see [system/io.md](system/io.md#projects-and-the-two-roots) |
| `APP_BRANCH`, `APP_COMMIT_HASH`, `APP_BUILD_DATE` | vkm_build_info | Git metadata |

### Profiling with Tracy

When `VKM_PROFILER=1` (the default in Debug), the engine emits per-frame
`FrameMark`, per-stage CPU zones, and per-pass CPU+GPU zones over TCP.
Attach the Tracy profiler GUI (built separately from `modules/tracy/profiler`)
to inspect a live capture. Macros are in `src/engine/debug/profiler.h` -
engine code never includes Tracy headers directly.
