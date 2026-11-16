# Building

How to configure, build, run and test the engine and its examples, what each
CMake target is, and what an SDK install and a shipping build contain.

## Prerequisites

- CMake 3.25+ and Ninja
- Python 3.8+, for `tools/vkm`; on Windows a Windows build of it (MSYS2's
  `mingw-w64-ucrt-x86_64-python`, or python.org's)
- Git LFS: `.gitattributes` keeps images, HDR maps, terrains and `.bin` buffers in it, and a
  clone without it holds a pointer in place of each
- OpenGL 4.5 capable GPU and drivers
- The network at the first configure: vkmOpenGL downloads glew's release into its
  `modules/glew`, as glew's repository holds none of the sources it generates

No compiler is needed: a configure that names none builds with the pinned GCC,
or with `-DVKM_COMPILER=clang` the pinned Clang, which `tools/toolchain.json`
pins and `cmake/pinned_toolchain.cmake` fetches through `vkm toolchain`
([the toolchain pin](../getting-started.md#the-toolchain-pin)). Naming one -
`-DCMAKE_CXX_COMPILER=...`, `CC`/`CXX`, a toolchain file, or
`-DVKM_PINNED_TOOLCHAIN=OFF` for whatever CMake finds - builds with that instead,
and vkm builds a game's module with the same.

## Setup

```bash
git lfs install                          # once a machine, before the first clone
git clone <repo-url>
cd vkmEngine
git submodule update --init --recursive
```

A clone made before `git lfs install` runs it, then `git lfs pull` to fetch its LFS files: a pull
alone leaves the next checkout writing pointers again.

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
non-zero, and `Engine::run` shuts down cleanly when the system asks it to stop
(`Platform::stopRequested`): SIGINT or SIGTERM, and on Windows Ctrl+C, its console
closing, or `Platform::ChildProcess::stop`, which asks a Windows program first, through
an event named for its process, as SIGTERM does on POSIX:

```bash
timeout 10 ./build/bin/vkm_runtime examples/potion_runner
# 124 - still running when the timeout killed it, i.e. it booted
#   1 - it refused: no gameplay module, or no world of its own to open
```

Which conditions are fatal to which host is in
[io.md](io.md#what-each-host-does-when-a-project-will-not-open).

The executables land in `build/bin/`. A gameplay module is named `game` in every
configuration (the host finds it by name), and a module compiled against another
tree's public flags - the profiler, the asserts - beside an engine built with others
may refuse to load, or load and disagree with the engine about the code they share.
So each tree writes its own, and its hosts load it:

| Tree | Writes an example's module to | Its hosts load an example's from |
|------|-------------------------------|----------------------------------|
| `build/`, the tree `tools/vkm` builds and runs on | the example's own `bin/` (`examples/<name>/bin/libgame.so`, `game.dll` on Windows) | the example's `bin/` |
| Any other: `build-clang/`, `build-asan/`, a cross-build | `bin/examples/<name>/` beside its hosts (`VKM_TREE_EXAMPLE_MODULES`) | the same, beside themselves |
| `build-shipping/` | nothing: it builds no example | a package's own `bin/` |

A project of your own is built by `tools/vkm` against `build/` into its `bin/`, and
a shipped one into its `build/shipping/bin/` (below). Every host loads a project
that is not an example of the engine's from its `bin/`.

## The examples build the way a game does

`examples/potion_runner`, `examples/the_lab`, `examples/stress_arena` and
`examples/city_block` are projects, and one `CMakeLists.txt` each serves two
ways of building them. Configured on its own (`./tools/vkm build
examples/potion_runner`) it calls `find_package(vkmEngine)` against the engine's
build tree or an installed SDK, as a project made by `vkm new` does. Added by
the engine's own `CMakeLists.txt`, it skips `project()` and `find_package`.

Both call `vkm_add_gameplay_module()` (`cmake/gameplay_module.cmake`, shipped in
the SDK). It compiles every `.cpp` under the project's `src/`, which is also its
include root, with `system/script/behavior_api.h` precompiled and the log
category `GAME`, and refuses at configure an engine of another minor release
than `engineVersion` in `project.json` (`vkm_check_engine_version`).

## Tests

```bash
ctest --test-dir build                  # everything this machine can run
ctest --test-dir build -L '^physics$'   # one area (-L is a regular expression)
ctest --test-dir build -LE '^gpu$'      # all but the GPU's, as CI's Test job runs
./build/bin/vkm_engine_tests --list     # every test, by name
./build/bin/vkm_engine_tests "physics: a stack of boxes stands"
```

Each test is a ctest of its own, named for what it proves: `<area>: <promise>`, the area being
the source folder it tests, which is also its label. CTest asks each binary for its tests when
it runs (`tests/harness/discover.cmake`), so there is no list to keep. Under a multi-config
generator (Ninja Multi-Config, Visual Studio) `ctest -C <config>` names the build type to run.

| Runs | Tests | Needs |
|------|-------|-------|
| `vkm_engine_tests` | every area but the renderer's, `tests/<area>/<topic>.cpp` | the art Git LFS holds, which the editor's import tests read; no GPU, no display |
| `vkm_render_tests` | `tests/render/opengl/`, labeled `gpu` as well | an EGL device |
| `tests/cli/*.py` | `cli: <file>`: `tools/vkm` run as a user runs it | Python 3.8 |
| `tests/docs/check_docs.py` | `docs: the docs and the tree agree` | Python 3.8, and git, whose list of the tree it reads |
| `modules/vkmOpenGL/tests/error_check.cmake` | `build: GL's error check is compiled into a Debug build alone`: vkmOpenGL's objects call it in the build type `ctest -C` names exactly when that is Debug; labeled `build` | the toolchain's `nm` |
| `tests/build/no_tracy.cmake` | `build: a multi-config build carries no Tracy unless asked`: in a multi-config tree that took `VKM_PROFILER`'s default, no object of vkm_core calls into Tracy; labeled `build` | the toolchain's `nm` |
| `vkm_physics_hash` | `determinism: <example> hashes as recorded at <n> steps a frame`: the lab and the arena, each cooked first (`determinism: <example> is cooked`), played 600 ticks at 1, 2 and 3 steps a frame against the hash `tests/CMakeLists.txt` records, any logged error failing it; labeled `determinism` | the lab's art, which Git LFS holds |
| `vkm_log_tests` | `vkm_log: <promise>`, vkmLog's own suite, labeled `vkm_log` (`modules/CMakeLists.txt` turns `VKM_LOG_BUILD_TESTS` on in every tree but a shipping one) | nothing |
| `vkm_platform_tests` | `vkm_platform: <promise>`, vkmPlatform's own suite, labeled `vkm_platform` (`modules/CMakeLists.txt` turns `VKM_PLATFORM_BUILD_TESTS` on in every tree but a shipping one) | nothing |
| `vkm_audio_tests` | `vkm_audio: <promise>`, vkmAudio's own suite, labeled `vkm_audio` (`modules/CMakeLists.txt` turns `VKM_AUDIO_BUILD_TESTS` on in every tree but a shipping one) | nothing |
| `vkm_network_tests` | `vkm_network: <promise>`, vkmNetwork's own suite, labeled `vkm_network` (`modules/CMakeLists.txt` turns `VKM_NETWORK_BUILD_TESTS` on in every tree but a shipping one) | nothing |
| `vkm_physics_tests` | `vkm_physics: <promise>`, vkmPhysics's own suite, labeled `vkm_physics` (`modules/CMakeLists.txt` turns `VKM_PHYSICS_BUILD_TESTS` on in every tree but a shipping one) | nothing |
| `vkm_opengl_tests` | `vkm_opengl: <promise>`, vkmOpenGL's own suite, labeled `vkm_opengl` (`modules/CMakeLists.txt` turns `VKM_OPENGL_BUILD_TESTS` on in every tree but a shipping one) | nothing |

A test is `TEST("what works") { ... }` in `tests/<area>/<topic>.cpp`, with `CHECK` and
`REQUIRE` from `tests/harness/check.h`; a new file needs no CMake edit. It runs in a process of
its own, in a scratch directory of its own (`scratch()`), and goes through the interface a game,
the editor or a user has. `tests/harness/world.h` stands up what it needs: `Game`, a headless
engine as a host has one, on a pinned clock; `Frame`, for one system on its own; `openProject()`,
a project root. `tests/harness/scratch.h` reads what the engine logged at warning and up, or at
info in a `TEST_WITH_INFO_LOG` (`countInLog()`), counts heap allocations (`allocations()`, which
skips under a sanitizer and on Windows) and skips a test the machine cannot run (`skip()`, exit 77).
`tests/harness/lockstep.h` runs a server and its client in one process; `tests/fixture/` holds the
gameplay modules the script and end-to-end tests load.

`vkm_render_tests` draws through `GLBackend` into an off-screen EGL surface
(`tests/render/opengl/gpu.h`). It is built only where CMake finds EGL, and with no device each test is
skipped, unless `VKM_GPU_REQUIRED=1` (CI's GPU jobs) makes that a failure. A fact stated in C++ and
again in GLSL is held to its twin on the GPU: a block's layout by asking the linked program
(`blocks.cpp`), a value worked out by a probe, a compute shader in `tests/render/opengl/probes` that
works out the GLSL's side over rows of numbers (`Probes`). A reference picture in
`tests/render/opengl/pictures/` is compared only on the renderer its `renderer.txt` names, and skipped
on any other; it agrees when no more than one pixel in a thousand is more than 8 levels off in a
channel. A picture that does not is written to `build/test_pictures/`, beside a map of where.
After a change meant to alter the picture, make them again on that GPU, look at them, and commit
them with the change:

```sh
VKM_UPDATE_PICTURES=1 ctest --test-dir build -L gpu
```

`cli` is registered where CMake finds Python; a test that drives the engine's build skips
unless this tree is the one `tools/vkm` finds, `build/`.

The editor is tested without a window, through its commands and sessions (`tests/editor/desk.h`);
what needs one - the editor's drawing, a real window's input - is walked by hand.

**The docs suite** (`tests/docs/check_docs.py`) holds the pages to the tree
([what it checks of them](../README.md#the-rule-these-pages-are-kept-by)) and the tree to the
rules the pages state of it: American spelling, every path a comment names is in the tree, every
vkm library's license the engine's, a recipe's ten-line limit, a graphics API named nowhere
outside a backend's places, no call taking two random draws in its arguments, ImGui kept to the
editor, every reflected field named by its Inspector card, and the layer order of the engine's
includes. It also reports each area's code lines and any file of `src/` or `app/` past 1,500
lines, failing nothing. It reads no sentence: when a change touches a name, read the comments
and doc lines that name it against the new code before committing.

**A cached build hides a header that does not compile on its own.** Compile it alone, with
the flags from `build/compile_commands.json`, before trusting that it includes what it uses.

## CMake Targets

| Target | Type | Description |
|--------|------|-------------|
| `vkm_core` | **Shared lib** | Core engine: ECS, resources, IO, the non-render systems, input, debug, opening a project |
| `vkm_render` | **Shared lib** | Render system, backend abstraction, render view |
| `vkm_platform` | Static lib | The platform, `modules/vkmPlatform`, linked whole into `vkm_core` as `vkm_physics` is. It links GLFW privately: only the OpenGL backend and the editor, which link `glfw` themselves, include its header |
| `vkm_audio` | Static lib | The audio, `modules/vkmAudio`, linked whole into `vkm_core` as `vkm_physics` is |
| `vkm_network` | Static lib | The network's transport, `modules/vkmNetwork`, linked whole into `vkm_core`: one binary of the engine links it, so it ships nothing of its own, and a gameplay module and the tests reach it through `vkm_core`. It links Winsock on Windows |
| `vkm_physics` | Static lib | The physics, `modules/vkmPhysics`, linked whole into `vkm_core`: one binary of the engine links it, so it ships nothing of its own, and a gameplay module and the tests reach it through `vkm_core` |
| `vkm_backend_gl` | Static lib | OpenGL backend |
| `vkm_backend_gl_editor` | Static lib | `GLEditorBackend`: the OpenGL backend with ImGui's OpenGL renderer, from `src/backend/opengl/editor/`. Linked by `vkm_editor_app` alone, and not built in a shipping engine |
| `vkm_loader` | Static lib | What the hosts decode or bake themselves, from `src/loader/`: image and HDR decode, the SDF font baker (the server bakes no font, so its link takes none of it). No model import, no texture bake |
| `vkm_pipeline` | Static lib | The asset pipeline, from `src/pipeline/`: the model, image and audio importers, the asset cooker and the mesh operations they share. Linked by `vkm_editor` (so `vkm_editor_app`), `vkm_cook_app` and the tests, never the runtime |
| `vkm_editor` | Static lib | Editor UI, panels, overlays, gizmo, scene I/O |
| `vkm_build_info` | Interface lib | Build metadata: the name, version and root as definitions, and the branch and commits in the build tree's generated/build_stamp.h, which `vkm_build_stamp` writes at every build a commit changed (`cmake/build_stamp.cmake`) |
| `vkm_warnings` | Interface lib | Shared GCC/Clang warning flags; first-party targets opt in, submodules don't |
| `<project>_module` | Shared lib | One per project (`potion_runner_module`, ...), built by `vkm_add_gameplay_module()` as `game.dll`/`libgame.so` into the project's `bin/` |
| `vkm_runtime_app` | Executable | Bare engine; links no importer, no editor and no ImGui. Runs as `vkm_runtime`; on Windows a windowed program, so no terminal opens behind a game |
| `vkm_editor_app` | Executable | Engine libs + `vkm_editor` + `vkm_pipeline` + `vkm_backend_gl_editor`; hot-reloads the open project's module. Runs as `vkm_editor`, windowed on Windows as the runtime is |
| `vkm_cook_app` | Executable | Headless asset cook: no window, no graphics context, no `Engine`. Runs as `vkm_cook` |
| `vkm_server_app` | Executable | The refereeing host: a client's system stack with no window and no render backend. Runs as `vkm_server`; still loads glfw's shared library, which vkmPlatform's window links |
| `vkm_log_tests` | Executable | A CTest per test, `vkm_log: <promise>`. Not installed |
| `vkm_platform_tests` | Executable | A CTest per test, `vkm_platform: <promise>`. Not installed |
| `vkm_audio_tests` | Executable | A CTest per test, `vkm_audio: <promise>`. Not installed |
| `vkm_network_tests` | Executable | A CTest per test, `vkm_network: <promise>`. Not installed |
| `vkm_physics_tests` | Executable | A CTest per test, `vkm_physics: <promise>`. Not installed |
| `vkm_opengl_tests` | Executable | A CTest per test, `vkm_opengl: <promise>`. Not installed |
| `vkm_engine_tests` | Executable | A CTest per test, `<area>: <promise>`. Not installed |
| `vkm_render_tests` | Executable | A CTest per test, labeled `gpu`. Built only where CMake finds EGL. Not installed |
| `vkm_physics_hash` | Executable | Plays a project headless and says whether its bodies hash as recorded (`tests/determinism/physics_hash.cpp`), for the `determinism` ctests. Not installed |
| `docs` | Custom | The API reference: Doxygen over `src/engine`, the `src/` of vkmPlatform, vkmAudio, vkmNetwork and vkmPhysics, and `docs/api` into `build/docs/html`, the undocumented public surface listed in `build/docs/doxygen-warnings.log`. Offered only where CMake finds Doxygen (`cmake/docs.cmake`) |

Host targets carry an `_app` suffix because two of the names are library names;
`OUTPUT_NAME` drops it.

`vkm_core` and `vkm_render` are shared because a gameplay module must reach
engine symbols without a second copy (two typeId registries, two sets of
singletons). A static engine could only manage that by resolving from the host
executable, which on Windows binds a module to one exe.

The three hosts that build an `Engine` include `app/engine_app.h` for
`setupEngineApp`. That `vkm_runtime_app` carries no cooker, no editor and no
ImGui is enforced by its link list, not a build flag; the `docs` suite holds
ImGui's headers to the editor's two folders and a backend's `editor/`, as a static
library brings only what something reaches. Each host names its backend in
`app/backend_choice.h` and links it: the runtime `vkm_backend_gl`, the editor
`vkm_backend_gl_editor`.

## Installing an SDK

`cmake --install` produces an **SDK** - what games are built with - not a game:

```bash
cmake --install build --prefix /path/to/sdk
```

```
<prefix>/vkm, vkm.cmd   the command, as a person runs it (vkm.cmd on Windows)
<prefix>/README.md      what to do first (tools/install/sdk_readme.md)
<prefix>/bin/       the hosts, the shared engine and its compiler's runtime, and
                    the command's vkm.py and vkmcli/, and the toolchain.json and
                    generated.txt it reads
<prefix>/include/   the engine's public headers plus the library and
                    third-party headers they reach into
<prefix>/lib/cmake/vkmEngine/   what find_package(vkmEngine) loads, plus the
                    gameplay-module recipe it includes
<prefix>/shaders/   engine shaders
<prefix>/assets/    the editor's font and logo - engine chrome, not anyone's art
<prefix>/content/   the packs the editor copies into a project: Create > Effect's
                    effects, Render Settings' neutral grading table
<prefix>/templates/ <prefix>/examples/   what `vkm new` copies, less what running
                    them wrote (tools/generated.txt)
<prefix>/python/    the pinned Python vkm runs on, when VKM_PYTHON names it
<prefix>/shipping/  the shipping engine, when VKM_SHIPPING_ENGINE names it
<prefix>/LICENSE    the engine's license
<prefix>/licenses/  the engine's license and every module's, gathered by the build into its own
                    licenses/ (modules/CMakeLists.txt); `vkm package` puts them in every game
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

A packaged game runs on `VKM_SHIPPING`: `Release` with asserts, the profiler,
debug drawing and the driver's debug output off and `-g` kept, so a player's
crash reads against the symbols `vkm package` splits out. It configures only the
runtime, the server and what they load - no editor, cooker, tests or examples.

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
build the shipping engine and what the SDK carries and package the archive, then
install it on a clean machine and use it (`vkm new`, `vkm doctor`,
`vkm package`, every example). Beside them the same four build everything and
run the tests, and the GPU suite runs, each compiler's, split across runners on
their software renderer. Every job has a time limit, so a hang fails the run
rather than holding it. A version tag (`vX.Y.Z`) runs
`.github/workflows/release.yml`: the same build, then the archives, the examples
as games, the installers (`tools/install/install.sh`, `install.ps1`) and
`SHA256SUMS` on the tag's GitHub release, with notes from `CHANGELOG.md`.

## External Modules

Every external dependency but one is under `modules/`, a git submodule pinned to a commit,
and updating one is moving its pin; each library of ours names `master` as the branch it
follows (`.gitmodules`). `bc7e` there is vendored, and updating it is copying its
files again. The one outside is `shaders/opengl/post/upscale/`, a GLSL port of Lottes's spatial
upscale and sharpen (2021) under the MIT license in its `LICENSE-MIT`; `fsr1.glsl` says what it
changed. The docs suite (`tests/docs/check_docs.py`) holds this table to `modules/`:

| Module | Path | Provides |
|--------|------|----------|
| **vkmLog** | `modules/vkmLog` | Logging (`Vkm::Log::`): LOG_TRACE..LOG_FATAL, VKM_ASSERT |
| **vkmPlatform** | `modules/vkmPlatform` | The platform (`Vkm::Platform::`): the window and its input by keys of our own, the thread pool, child processes, dynamic libraries, where the OS keeps things, the stop signal. Vendors GLFW, which the engine makes shared. Static, linked whole into vkm_core |
| **vkmAudio** | `modules/vkmAudio` | Audio (`Vkm::Audio::`): the device and its mixer, a clip and its wav/mp3/flac decoder, the acoustics' estimates over the rays the engine casts, and the room's reverb. Vendors miniaudio, which nothing else includes. Static, linked whole into vkm_core |
| **vkmNetwork** | `modules/vkmNetwork` | The network's transport (`Vkm::Network::`): UDP sockets, the connection, reliable messages, the join token, the bit stream and its coders. Static, linked whole into vkm_core |
| **vkmPhysics** | `modules/vkmPhysics` | Rigid-body physics (`Vkm::Physics::`): the step the engine hands its bodies to - collision, the solver, joints, sleep. Static, linked whole into vkm_core |
| **vkmOpenGL** | `modules/vkmOpenGL` | OpenGL object wrappers (`Vkm::OpenGL::`), shader loading/preprocessing. Vendors GLEW, and exports it |
| **vkmVulkan** | `modules/vkmVulkan` | The Vulkan layer to come, as vkmOpenGL is OpenGL's. It holds no code yet, so the build adds nothing of it |
| **glm** | `modules/glm` | Vector/matrix math |
| **stb** | `modules/stb` | `stb_image` (texture decode), `stb_image_write` (screenshots), `stb_truetype` (SDF font bake), `stb_image_resize2` (the cooker's mip chains) |
| **imgui** | `modules/imgui` | Dear ImGui, `docking` branch, for editor UI |
| **json** | `modules/json` | nlohmann/json (`nlohmann_json`) |
| **bc7e** | `modules/bc7e` | The cooker's BC7 encoder (the `bc7e` target): the two self-contained files of basis_universal's scalar encoder, with its license and notice, at the commit `modules/CMakeLists.txt` names. BC4/BC5 are the engine's own, `src/pipeline/cook/texture/bc4_encoder.h` |
| **cgltf** | `modules/cgltf` | glTF and GLB import |
| **ufbx** | `modules/ufbx` | FBX and OBJ import, converted to the engine's axes and meters on load |
| **mikktspace** | `modules/mikktspace` | MikkTSpace tangents for imported meshes that carry none |
| **meshoptimizer** | `modules/meshoptimizer` | LOD simplification (`decimateMesh`), the vertex-cache / vertex-fetch order of cooked meshes (`optimizeMeshForGpu`), and the import weld |
| **xatlas** | `modules/xatlas` | The lightmap UVs a baked mesh is unwrapped into at cook |
| **tracy** | `modules/tracy` | Tracy profiler client (`TracyClient`), linked by vkm_core only when `VKM_PROFILER` is on |

cgltf, ufbx, mikktspace, meshoptimizer, xatlas and `bc7e` are linked privately by
`vkm_pipeline` and by the test suite only.

## Shaders

GLSL shaders live in a folder of their backend's, `shaders/opengl/`: one folder per program, grouped
by the stage that runs them (`scene/`, `light/`, `post/`, `overlay/`, `offline/`), as the backend's
`pass/` groups the passes.
The libraries several families include are in `include/`; a family's own sits inside the family,
and a program's variants are its folder built with a list of defines
(`Vkm::OpenGL::Shader("shaders/opengl/scene/forward/pbr", {"BAKED"})`), declared in every stage.

The loader (vkmOpenGL) hard-codes each stage's file name:

| Stage      | Filename             | Required?                          |
|------------|----------------------|------------------------------------|
| Vertex     | `vertex.shader`      | Required for graphics programs     |
| Fragment   | `fragment.shader`    | Required for graphics programs     |
| Geometry   | `geometry.shader`    | Optional; loaded if present        |
| Compute    | `compute.shader`     | A compute-only program (`Vkm::OpenGL::ComputeShader`) |

A program is loaded by path prefix:

```cpp
Vkm::OpenGL::Shader pbr("shaders/opengl/scene/forward/pbr");
```

The preprocessor resolves `#include` between `.shader`/`.glsl` files and
prepends a prelude - the `#version` and the engine's cross-language constants -
which `GLBackend` builds from the C++ constants; see
[lighting.md](lighting.md#limits-and-the-shader-prelude).

## Reaching a platform header

The engine includes no system or window header: what it asks of the OS - where the
executable is, where a user's files go, a stop asked for - is vkmPlatform's
(`vkm_platform/process/this_process.h`). Inside vkmPlatform, `vkm_platform/windows_api.h` is
**the one way to include a Windows system header**; vkmNetwork takes Winsock through its
own, `vkm_network/winsock_api.h`, which it keeps private. `vkm_platform/glfw_api.h` is the
one way to include GLFW, and compiles only where the includer links `glfw` itself: the
OpenGL backend (its window surface and ImGui's renderer) and the editor through ImGui.
`vkm_core` passes GLFW's include path on to nobody, so a game and the rest of the engine
cannot include it, and the SDK installs neither door nor GLFW's header.

## Compiler Flags

First-party targets link the `vkm_warnings` interface lib (GCC/Clang): `-Wall
-Wextra` and a few more, each explained in the top-level `CMakeLists.txt`.
Submodules keep their own warnings, and the third-party ones' include paths are
`SYSTEM` (vkmLog, vkmPlatform, vkmAudio, vkmNetwork, vkmPhysics and vkmOpenGL are
first-party, and are not). Clang adds `-Werror=documentation`, which fails on a
`@param` that names no parameter, so a Clang build checks the doc blocks.
First-party code is strict C++17 (`CMAKE_CXX_EXTENSIONS OFF`).

`vkm_core`, `vkm_network` and `vkm_physics` carry `-ffp-contract=off` (GCC/Clang), so no `a * b + c`
the source writes is fused into one multiply-add. A fused result rounds once where the written one rounds
twice, so the same tick would land elsewhere on a machine built for FMA, and replay and the wire
rest on every end landing in the same place ([physics.md](physics.md#determinism)). It is public,
because a gameplay module predicted on both ends of a session compiles the same inline math.
`vkm_audio` carries it privately, so a gain or a ray rounds as it did inside `vkm_core`.

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
| `-UNDEBUG` | vkm_platform (private) | With `VKM_PLATFORM_ASSERTS`, which `modules/CMakeLists.txt` sets from `VKM_ASSERTS`: the library's asserts run as the engine's do |
| `-UNDEBUG` | vkm_audio (private) | With `VKM_AUDIO_ASSERTS`, which `modules/CMakeLists.txt` sets from `VKM_ASSERTS`: the library's `VKM_ASSERT`s run as the engine's do |
| `-UNDEBUG` | vkm_network (private) | With `VKM_NETWORK_ASSERTS`, which `modules/CMakeLists.txt` sets from `VKM_ASSERTS`: the library's `VKM_ASSERT`s run as the engine's do |
| `-UNDEBUG` | vkm_physics (private) | With `VKM_PHYSICS_ASSERTS`, which `modules/CMakeLists.txt` sets from `VKM_ASSERTS`: the library's `VKM_ASSERT`s run as the engine's do |
| `-UNDEBUG` | vkm_opengl (private) | With `VKM_OPENGL_ASSERTS`, which `modules/CMakeLists.txt` sets from `VKM_ASSERTS`: the library's `VKM_ASSERT`s run as the engine's do |
| `VKM_OPENGL_CHECKS` | vkm_opengl (public) | With `VKM_OPENGL_CHECKS`, which `modules/CMakeLists.txt` sets in a `Debug` build alone: `VKM_OPENGL_CHECK` reads GL's error queue after every call and stops the program on an error |
| `VKM_PLATFORM_PROFILER=1` | vkm_platform (private) | With `VKM_PLATFORM_PROFILER`, set from `VKM_PROFILER`: `SwapBuffers` and `FrameLimiter` are Tracy zones of the same client (`modules/vkmPlatform/src/vkm_platform/profile.h`) |
| `VKM_PHYSICS_PROFILER=1` | vkm_physics (private) | With `VKM_PHYSICS_PROFILER`, set from `VKM_PROFILER`: a step's phases are Tracy zones of the same client (`modules/vkmPhysics/src/vkm_physics/profile.h`) |
| `VKM_PROFILER=1` | vkm_core (public) | Enables Tracy CPU+GPU zones via base/profile/profiler.h. Default ON unless `CMAKE_BUILD_TYPE` is exactly `Release`, and OFF under a multi-config generator, where `-DVKM_PROFILER=ON` profiles every build type; always off in a shipping engine |
| `VKM_SHIPPING=1` | vkm_core, vkm_backend_gl (private) | A shipping engine: it hands no system a `DebugDraw`, so a shipped game draws no debug lines, and the backend asks the driver for no debug output |
| `VKM_TREE_EXAMPLE_MODULES=1` | vkm_core (private) | In an engine tree but `build/` and the shipping one: `Roots::gameplayModule` finds an example's module beside the host, where that tree builds it ([the trees](#build-commands)) |
| `GLM_ENABLE_EXPERIMENTAL` | glm (public) | GLM experimental features, in every target that compiles glm |
| `GLM_FORCE_INTRINSICS` | glm (public) | GLM SIMD intrinsics, in every target that compiles glm, so they all round alike |
| `VKM_ENGINE_VERSION` | vkm_core (public) | The engine version a gameplay module reports through `vkmModuleEngineVersion()`; a host refuses a module whose string differs |
| `APP_VERSION` | vkm_build_info | Engine version string |
| `APP_ROOT_DIR` | vkm_build_info | Absolute path to the **engine** root - the fallback `Roots::engineRoot()` uses when the exe is run from a build tree. Not the project root |
| `APP_BRANCH`, `APP_COMMIT_HASH`, `APP_BUILD_DATE`, `VKM_<LIB>_VERSION`, `VKM_<LIB>_COMMIT_HASH` | generated/build_stamp.h (vkm_build_info) | Git metadata, read at every build; the date is the build that last changed it |

### Profiling with Tracy

With `VKM_PROFILER=1` the engine emits a `FrameMark` per frame, a CPU zone per
stage, and CPU+GPU zones per pass. Attach the Tracy GUI (built from
`modules/tracy/profiler`). The CPU macros are in `src/engine/base/profile/profiler.h`
and the GPU ones in `src/backend/opengl/gl_profiler.h`; engine code never
includes Tracy headers directly. The client listens on
localhost only (`TRACY_ONLY_LOCALHOST`, `modules/CMakeLists.txt`); turn that off
to profile from another machine.

- **A `System`'s frame entry point opens with `PROFILE_SCOPE("<ClassName>")`** as its first
  statement - `fixedUpdate` for a system with no `update`. A system with both suffixes the
  second (`"BehaviorSystem::fixed"`) so the two do not merge.
- **A backend pass opens no top-level zone**: the pass loop in `gl_backend.cpp` wraps each
  `execute()` in a CPU and a GPU zone named for the pass. `GLBackend::drawChrome`'s own loop
  opens none. Sub-zones for phases inside a pass are welcome.

### Measuring a change

**`vkm bench`** answers whether a frame fits. It packages the game on the shipping engine, runs it
with `--bench bench.json`, prints each view's fps, 1% low, 0.1% low, worst frame and spikes (frames
over twice the median), and exits non-zero over budget, or with no report: closed before its last
view, or the report not written. `logs/bench/` gets `report.json` and a picture of each view, to
compare what a change did to the look.

```json
{
  "warmupSeconds": 10,
  "seconds": 20,
  "tiers": [{ "tier": "Max", "averageMs": 2.5, "low1Ms": 4.0 }],
  "views": [
    { "name": "spawn" },
    { "name": "night", "position": [-12, 3, 24], "lookAt": [-14, 1.5, 40], "sunElevation": -12 },
    {
      "name": "walk",
      "lapSeconds": 40,
      "path": [
        { "position": [0, 3, -10], "lookAt": [0, 2, 10] },
        { "position": [19, 2, 28], "lookAt": [19, 1.5, 42] }
      ]
    }
  ]
}
```

The game runs as it always does and only the camera is set; a view with no `position` or `path`
keeps the game's own. A `path` goes round its keys and back to the first every `lapSeconds`, a key's
share of the lap being equal, whatever the distance. Every view runs at every tier (`GraphicsPreset`),
and each tier names both its budgets. A 1% low is the slowest 1% of frames, averaged. Time spent
baking, as after a change of sun, and the splash are not counted. Budgets are for one machine, so
state them for the one you test on.

**A capture** says where the time goes:

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

A compiler cache serves builds CMake never mentions: where ccache is installed, a build timing
taken without `CCACHE_DISABLE=1` is not a measurement.
