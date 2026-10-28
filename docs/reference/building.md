# Building

How to configure, build, run and test the engine and its examples, what each
CMake target is, and what an SDK install and a shipping build contain.

## Prerequisites

- CMake 3.25+ and Ninja
- Python 3.8+, for `tools/vkm`; on Windows a Windows build of it (MSYS2's
  `mingw-w64-ucrt-x86_64-python`, or python.org's)
- OpenGL 4.5 capable GPU and drivers

No compiler is needed: a configure that names none builds with the pinned GCC,
or with `-DVKM_COMPILER=clang` the pinned Clang, which `tools/toolchain.json`
pins and `cmake/pinned_toolchain.cmake` fetches through `vkm toolchain`
([the toolchain pin](../getting-started.md#the-toolchain-pin)). Naming one -
`-DCMAKE_CXX_COMPILER=...`, `CC`/`CXX`, a toolchain file, or
`-DVKM_PINNED_TOOLCHAIN=OFF` for whatever CMake finds - builds with that instead,
and vkm builds a game's module with the same.

## Setup

```bash
git clone <repo-url>
cd vkmEngine
git submodule update --init --recursive
```

The engine's own `assets/` is ignored except for `logo/`, `fonts/` and `docs/`,
so a clone carries no models and no environment maps; it builds and runs
without them. `examples/the_lab/assets/` and `examples/the_lab/library/`
are named exceptions in `.gitignore`.

## Build Commands

An in-source build is refused, and a configure that names no build type gets
`RelWithDebInfo`. Asserts stay on whatever the build type (`VKM_ASSERTS`); turn
them off in a tree you profile.

```bash
cmake -B build -G Ninja
cmake --build build

./build/bin/vkm_editor examples/potion_runner    # edit a project
./build/bin/vkm_runtime examples/potion_runner   # play it
./build/bin/vkm_cook examples/potion_runner      # bake its assets, no window
./build/bin/vkm_server examples/the_lab          # serve it to players, no window
build\bin\vkm_editor.exe                         # Windows
```

Those run what was built last. `tools/vkm` builds the project's module and cooks
its assets first:

```bash
./tools/vkm run examples/the_lab                # build, cook and play
./tools/vkm run examples/the_lab --players 2    # with a local server
./tools/vkm package examples/the_lab            # a standalone game under its dist/
```

[getting-started.md](../getting-started.md#the-commands) lists every command.
Each host takes a project directory, defaulting to the one beside its
executable ([io.md](io.md#projects-and-their-roots)).

**A boot check is an exit code.** A host that cannot open its project exits
non-zero, and `Engine::run` shuts down cleanly on SIGINT/SIGTERM:

```bash
timeout 10 ./build/bin/vkm_runtime examples/potion_runner
# 124 - still running when the timeout killed it, i.e. it booted
#   1 - it refused: no gameplay module, or no world of its own to open
```

Which conditions are fatal to which host is in
[io.md](io.md#what-each-host-does-when-a-project-will-not-open).

The executables land in `build/bin/`. Each project's gameplay module builds into
that project's own `bin/`, and is named `game` in every configuration (the host
finds it by name). So two build trees write the same `<project>/bin/libgame.so`
and the later build wins: **run a host from the tree that built the module it
will load**. Mixing them runs a module compiled against another tree's public
flags - the profiler, the asserts - beside an engine built with others: it may
refuse to load, or load and disagree with the engine about the code they share.

## The examples build the way a game does

`examples/potion_runner`, `examples/the_lab`, `examples/stress_arena` and
`examples/city_block` are projects, and one `CMakeLists.txt` each serves two
ways of building them. Configured on its own (`./tools/vkm build
examples/potion_runner`) it calls `find_package(vkmEngine)` against the engine's
build tree or an installed SDK, as a project made by `vkm new` does. Added by
the engine's own `CMakeLists.txt`, it skips `project()` and `find_package`.

Both call `vkm_add_gameplay_module()` (`cmake/gameplay_module.cmake`, shipped in
the SDK). It compiles every `.cpp` under the project's `src/`, which is also its
include root, and refuses at configure an engine of another minor release than
`engineVersion` in `project.json` (`vkm_check_engine_version`).

## Tests

```bash
ctest --test-dir build
```

The suites are split by what each one needs to run:

| CTest name   | Runs                          | Needs                                    |
|--------------|-------------------------------|------------------------------------------|
| `vkm_gl`     | `vkm_gl_tests`                | nothing                                  |
| `vkm_engine` | `vkm_engine_tests`            | nothing                                  |
| `render`     | `vkm_render_tests`            | an EGL device                            |
| `vkm_tool`   | `tests/tools/vkm_tool.cmake`  | Python 3.8, and the build tree at `build/` |
| `vkm_download` | `tests/tools/vkm_download_tests.py` | Python 3.8                         |

`vkm_gl` is vkmGL's context-free suite (`modules/CMakeLists.txt` forces
`VKM_GL_BUILD_TESTS` on).

`vkm_engine` is the engine's own, and needs no GPU or display. A suite is
`tests/<area>/<suite>_tests.cpp`, the area being the source folder it tests
(`tests/physics/solver_tests.cpp`); only the harness - `main.cpp`, `suites.h`,
`support.h` - sits at the root. A new suite is two edits: the file, and a row in
`VKM_TEST_SUITES` (`tests/suites.h`); the `docs` suite checks that rows, files
and the list below agree. The suites are `core`, `ecs`, `hierarchy`, `query`,
`collision`, `solver`, `joint`, `ragdoll`, `character`, `resource`, `async`,
`cook`, `import`, `authoring`, `prefab`, `scene`, `play`, `undo`, `culling`,
`sky`, `ocean`, `water`, `terrain`, `animation`, `particle`, `wire`,
`transport`, `replication`, `prediction`, `session`, `ui`, `audio`, `script`,
`debug`, `hostile`, `docs`.

```bash
./build/bin/vkm_engine_tests --list      # what there is
./build/bin/vkm_engine_tests solver wire # just those two
./build/bin/vkm_engine_tests             # all of them, which is what CI runs
```

`render` is the GPU suite. It compiles the shipped shaders, renders small scenes
built in code through `RenderView` and `GLBackend` off-screen, and compares each
with its image in `tests/render/golden/` (tolerances in
`tests/render/golden_tests.cpp`; a failing frame is written to
`/tmp/vkm_golden_<name>.png`). It is built only where CMake finds EGL and skips
at runtime with no device, unless `VKM_GPU_REQUIRED=1` (CI's GPU jobs) makes
that a failure. Each golden is compared only on the GPU that made it.
`--shard i/n` runs the i-th of n parts, so a slow device splits the suite across
machines. After a change meant to alter the picture, regenerate them, look at
them, and commit them with the change:

```sh
VKM_UPDATE_GOLDENS=1 ./build/bin/vkm_render_tests
```

`vkm_tool` runs `tools/vkm` as a user does - `vkm new`, then `doctor`,
`package --development` and `clean` - and is registered only for a build tree
at `build/`.

Asserts are on in these builds, so a check of what a shipping build does with a
broken invariant is skipped; a tree configured `-DVKM_ASSERTS=OFF` runs it.

Nothing covers what needs a window - the editor, input, and how the pieces meet;
walk the tool for those. New engine tests join the existing suites rather than
starting another binary.

## CMake Targets

| Target | Type | Description |
|--------|------|-------------|
| `vkm_core` | **Shared lib** | Core engine: ECS, resources, IO, the non-render systems, platform, debug |
| `vkm_render` | **Shared lib** | Render system, backend abstraction, render view |
| `vkm_backend_gl` | Static lib | OpenGL backend |
| `vkm_tools` | Static lib | What every host needs from `src/tools/`: image and HDR decode, the SDF font baker (`loader/`), the host prologue (`project_boot`). No model import, no texture bake |
| `vkm_cook` | Static lib | The model, image and audio importers and the asset cooker. Linked by `vkm_editor_app` and `vkm_cook_app` only |
| `vkm_editor` | Static lib | Editor UI, panels, overlays, gizmo, scene I/O |
| `vkm_build_info` | Interface lib | Compile-time build metadata (version, branch, commit hash) |
| `vkm_warnings` | Interface lib | Shared GCC/Clang warning flags; first-party targets opt in, submodules don't |
| `<project>_module` | Shared lib | One per project (`potion_runner_module`, ...), built by `vkm_add_gameplay_module()` as `game.dll`/`libgame.so` into the project's `bin/` |
| `vkm_runtime_app` | Executable | Bare engine; links no importer and no editor. Runs as `vkm_runtime`; on Windows a windowed program, so no terminal opens behind a game |
| `vkm_editor_app` | Executable | Engine libs + `vkm_editor` + `vkm_cook`; hot-reloads the open project's module. Runs as `vkm_editor`, windowed on Windows as the runtime is |
| `vkm_cook_app` | Executable | Headless asset cook: no window, no graphics context, no `Engine`. Runs as `vkm_cook` |
| `vkm_server_app` | Executable | The refereeing host: a client's system stack with no window and no render backend. Runs as `vkm_server`; still links glfw through `vkm_core` |
| `vkm_gl_tests` | Executable | CTest `vkm_gl`. Not installed |
| `vkm_engine_tests` | Executable | CTest `vkm_engine`. Not installed |
| `vkm_render_tests` | Executable | CTest `render`. Built only where CMake finds EGL. Not installed |

Host targets carry an `_app` suffix because two of the names are library names;
`OUTPUT_NAME` drops it.

`vkm_core` and `vkm_render` are shared because a gameplay module must reach
engine symbols without a second copy (two typeId registries, two sets of
singletons). A static engine could only manage that by resolving from the host
executable, which on Windows binds a module to one exe.

The three hosts that build an `Engine` include `app/engine_app.h` for
`setupEngineApp`. That `vkm_runtime_app` carries no cooker and no editor is
enforced by its link list, not a build flag.

## Installing an SDK

`cmake --install` produces an **SDK** - what games are built with - not a game:

```bash
cmake --install build --prefix /path/to/sdk
```

```
<prefix>/vkm, vkm.cmd   the command, as a person runs it (vkm.cmd on Windows)
<prefix>/README.md      what to do first (tools/install/sdk_readme.md)
<prefix>/bin/       the hosts, the shared engine and its compiler's runtime, and
                    vkm.py, vkmcli/ and toolchain.json, which the command runs
<prefix>/include/   the engine's public headers plus the third-party headers
                    they reach into
<prefix>/lib/cmake/vkmEngine/   what find_package(vkmEngine) loads, plus the
                    gameplay-module recipe it includes
<prefix>/shaders/   engine shaders
<prefix>/assets/    the editor's font and logo - engine chrome, not anyone's art
<prefix>/content/   the packs the editor's Create > Effect copies into a project
<prefix>/templates/ <prefix>/examples/   what `vkm new` copies, less what running
                    them wrote (tools/generated.txt)
<prefix>/python/    the pinned Python vkm runs on, when VKM_PYTHON names it
<prefix>/shipping/  the shipping engine, when VKM_SHIPPING_ENGINE names it
<prefix>/LICENSE    the engine's license, which `vkm package` puts in every game
```

`vkm package` takes `shaders/` and `assets/` into a game (`ENGINE_DATA` in
`tools/vkmcli/package.py`) from an SDK or from the build tree alike. CPack makes
the downloadable archive:

```bash
cmake --build build --target package
# -> vkmEngine-<version>-linux-x64-gcc.tar.xz, or -windows-x64-clang.zip, ...
```

Building a game *with* the SDK is [getting-started.md](../getting-started.md).

### The shipping engine

A packaged game runs on `VKM_SHIPPING`: `Release` with asserts, the profiler and
debug drawing off and `-g` kept, so a player's crash reads against the symbols
`vkm package` splits out. It configures only the runtime, the server and what
they load - no editor, cooker, tests or examples.

```bash
cmake -B build-shipping -G Ninja -DVKM_SHIPPING=ON
cmake --build build-shipping --target vkm_runtime_app vkm_server_app -j4
```

`vkm package` runs exactly that, so this is only for doing it by hand. A shipped
module is built against it into the project's `build/shipping/`
(`VKM_MODULE_DIR`), never over the development one. An SDK carries one, in
`<prefix>/shipping`, when the development tree is told where it is:

```bash
cmake -B build -G Ninja -DVKM_SHIPPING_ENGINE=$PWD/build-shipping
cmake --build build --target package
```

Without it, a game packaged from the SDK ships on the development engine, and
`vkm package` says so.

### CI and releases

Nothing runs on a push. A branch is built by hand - Actions > CI > Run workflow,
or `gh workflow run ci.yml --ref <branch>` - through `.github/workflows/ci.yml`,
which runs `.github/workflows/build.yml`: GCC and Clang on Linux and Windows
build the shipping engine and the SDK, run the tests, package the archive, then
install it on a clean machine and use it (`vkm new`, `vkm doctor`,
`vkm package`, every example); the GPU suite runs beside them, each compiler's,
split across runners on their software renderer. A version tag (`vX.Y.Z`) runs
`.github/workflows/release.yml`: the same build, then the archives, the examples
as games, the installers (`tools/install/install.sh`, `install.ps1`) and
`SHA256SUMS` on the tag's GitHub release, with notes from `CHANGELOG.md`.

## External Modules

Every external dependency is a git submodule under `modules/`, pinned to a
commit; updating one is moving its pin. `testTheModuleListsAreTheModules` holds
this table and the README's list to that directory:

| Module | Path | Provides |
|--------|------|----------|
| **vkmGL** | `modules/vkmGL` | OpenGL object wrappers (`Vkm::GL::`), shader loading/preprocessing. Vendors GLEW, and exports it |
| **vkmLog** | `modules/vkmLog` | Logging (`Vkm::Log::`): LOG_TRACE..LOG_FATAL, VKM_ASSERT |
| **glm** | `modules/glm` | Vector/matrix math |
| **glfw** | `modules/glfw` | Window + input platform layer |
| **stb** | `modules/stb` | `stb_image` (texture decode), `stb_image_write` (screenshots), `stb_truetype` (SDF font bake), `stb_image_resize2` (the cooker's mip chains) |
| **miniaudio** | `modules/miniaudio` | Playback device + wav/mp3/flac decode; only `system/audio` and the audio importer include it |
| **imgui** | `modules/imgui` | Dear ImGui, `docking` branch, for editor UI |
| **freetype** | `modules/freetype` | Font rasterizer, trimmed to the core |
| **json** | `modules/json` | nlohmann/json (`nlohmann_json`) |
| **basis_universal** | `modules/basis_universal` | The cooker's BC7 encoder: only `encoder/basisu_bc7e_scalar.cpp` is compiled (the `bc7e` target). BC4/BC5 are the engine's own, `src/tools/cook/bc4_encoder.h` |
| **cgltf** | `modules/cgltf` | glTF and GLB import |
| **ufbx** | `modules/ufbx` | FBX and OBJ import, converted to the engine's axes and metres on load |
| **mikktspace** | `modules/mikktspace` | MikkTSpace tangents for imported meshes that carry none |
| **meshoptimizer** | `modules/meshoptimizer` | LOD simplification (`decimateMesh`), the vertex-cache / vertex-fetch order of cooked meshes (`optimizeMeshForGpu`), and the import weld |
| **xatlas** | `modules/xatlas` | The lightmap UVs a baked mesh is unwrapped into at cook |
| **tracy** | `modules/tracy` | Tracy profiler client (`TracyClient`), linked by vkm_core only when `VKM_PROFILER` is on |

cgltf, ufbx, mikktspace, meshoptimizer, xatlas and `bc7e` are linked privately by
`vkm_cook` and by the test suite only.

## Shaders

GLSL shaders live in `shaders/`, one folder per program, grouped by pipeline
stage:

```
shaders/
  *.glsl        # the shared includes every program below pulls from, at the root
  forward/      # pbr/ (the ubershader), pbr_skinned/, pbr_plain/ and pbr_plain_skinned/ (without its optional features), pbr_layered/, a *_baked/ twin of each static one (reads the lightmaps), prepass/, prepass_skinned/
  shadow/       # depth/ (atlas tiles and cube faces alike), depth_masked/ (cut by alpha), a skinned variant of each, restore/ (the last cascade's held fixed depth)
  ibl/          # equirect/, sky/, irradiance/, prefilter/, brdf/  (IBL bake)
  atmosphere/   # transmittance/, multiscatter/, sky_view/, aerial_perspective/
  irradiance/   # surfels/, relight/, fill/, surfel.glsl  (the SH probe grid: capture, light, repair)
  lightmap/     # raster/, dilate/, shadow/  (the fixed lights' baked shadows)
  resolve/      # geometry/, color/  (the MSAA resolves)
  cull/         # pyramid/, instances/  (the GPU occlusion cull)
  exposure/     # histogram/, bins.glsl  (auto-exposure)
  ocean/        # swell, ripples and shore fields
  material/     # layer_copy/  (a layered material's maps into arrays)
  gtao/  screen_shadow/  cluster/  fog/  reflection/
  bloom/  dof/  decal/  particle/  particle_strip/
  skybox/  grid/  composite/  ui/  splash/  debug/
  loading/      # water/  water_step/  bar/  (the loading screen's water and bar)
```

The loader (vkmGL) hard-codes each stage's file name:

| Stage      | Filename             | Required?                          |
|------------|----------------------|------------------------------------|
| Vertex     | `vertex.shader`      | Required for graphics programs     |
| Fragment   | `fragment.shader`    | Required for graphics programs     |
| Geometry   | `geometry.shader`    | Optional; loaded if present        |
| Compute    | `compute.shader`     | A compute-only program (`Vkm::GL::ComputeShader`) |

A program is loaded by path prefix:

```cpp
Vkm::GL::Shader pbr("shaders/forward/pbr");
```

The preprocessor resolves `#include` between `.shader`/`.glsl` files and
prepends a prelude - the `#version` and the engine's cross-language constants -
which `GLBackend` builds from the C++ constants; see
[lighting.md](lighting.md#limits-and-the-shader-prelude).

## Reaching a platform header

`src/engine/platform/windows_api.h` is **the one way to include a Windows system
header**, and `src/engine/platform/window/glfw_include.h` the one way to include
GLFW. Nothing enforces it: a file that includes `<windows.h>` or
`<GLFW/glfw3.h>` directly compiles on Linux and fails on Windows.

## Compiler Flags

First-party targets link the `vkm_warnings` interface lib (GCC/Clang): `-Wall
-Wextra` and a few more, each explained in the top-level `CMakeLists.txt`.
Submodules keep their own warnings, and the third-party ones' include paths are
`SYSTEM` (vkmGL and vkmLog are first-party, and are not). Clang
adds `-Werror=documentation`, which fails on a `@param` that names no parameter,
so a Clang build checks the doc blocks. First-party code is strict C++17
(`CMAKE_CXX_EXTENSIONS OFF`).

### Sanitizers

A separate tree with AddressSanitizer and UndefinedBehaviorSanitizer on every
target, submodules included; the first report is fatal:

```bash
cmake -B build-asan -G Ninja -DVKM_SANITIZE=address,undefined -DVKM_PROFILER=OFF
cmake --build build-asan -j4
ctest --test-dir build-asan
```

Run it after touching anything that reads bytes the process did not write - the
cooked readers, the scene and prefab loaders, the wire.

## Build Definitions

| Define | Scope | Purpose |
|--------|-------|---------|
| `-UNDEBUG` | vkm_core (public) | With `VKM_ASSERTS` (on by default, off in a shipping engine): `VKM_ASSERT` and `assert()` run in every first-party file and every gameplay module, whatever the build type |
| `VKM_PROFILER=1` | vkm_core (public) | Enables Tracy CPU+GPU zones via debug/profiler.h. Default ON unless `CMAKE_BUILD_TYPE` is exactly `Release`; always off in a shipping engine |
| `VKM_SHIPPING=1` | vkm_core (private) | A shipping engine: it hands no system a `DebugDraw`, so a shipped game draws no debug lines |
| `GLM_ENABLE_EXPERIMENTAL` | vkm_core (public) | GLM experimental features |
| `GLM_FORCE_INTRINSICS` | vkm_core (public) | GLM SIMD intrinsics |
| `VKM_ENGINE_VERSION` | vkm_core (public) | The engine version a gameplay module reports through `vkmModuleEngineVersion()`; a host refuses a module whose string differs |
| `APP_VERSION` | vkm_build_info | Engine version string |
| `APP_ROOT_DIR` | vkm_build_info | Absolute path to the **engine** root - the fallback `ProjectPaths::engineRoot()` uses when the exe is run from a build tree. Not the project root |
| `APP_BRANCH`, `APP_COMMIT_HASH`, `APP_BUILD_DATE` | vkm_build_info | Git metadata |

### Profiling with Tracy

With `VKM_PROFILER=1` the engine emits a `FrameMark` per frame, a CPU zone per
stage, and CPU+GPU zones per pass. Attach the Tracy GUI (built from
`modules/tracy/profiler`). The CPU macros are in `src/engine/debug/profiler.h`
and the GPU ones in `src/backend/opengl/gl_profiler.h`; engine code never
includes Tracy headers directly. The client listens on
localhost only (`TRACY_ONLY_LOCALHOST`, `modules/CMakeLists.txt`); turn that off
to profile from another machine.

### Measuring a change

A headless capture:

```sh
./build/bin/vkm_runtime examples/stress_arena &
modules/tracy/capture/build/tracy-capture -o run.tracy -a 127.0.0.1 -s 40 -f
modules/tracy/csvexport/build/tracy-csvexport -u run.tracy    > cpu.csv
modules/tracy/csvexport/build/tracy-csvexport -g -u run.tracy > gpu.csv
```

(`tracy-capture` and `tracy-csvexport` build from `modules/tracy/capture` and
`modules/tracy/csvexport`.) Skip the start-up - the scene build and probe bakes,
until the `SwapBuffers` zones come steadily - then divide each zone's summed
time by the number of `SwapBuffers` zones, one per frame, for time per frame -
not by a stage's own count, as a stage opens a zone on every fixed tick too.
Compare `GPU.Frame` with the main thread's stages to see which side binds; on
the CPU side a pass costs mostly what it binds and draws, not what its shaders
compute.
