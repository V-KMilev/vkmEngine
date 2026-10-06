# Building

## Prerequisites

- C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+)
- CMake 3.25+
- Ninja build system
- Python 3.8+, for `tools/vkm`; on Windows a Windows build of it (MSYS2's
  `mingw-w64-ucrt-x86_64-python`, or python.org's)
- OpenGL 4.3 capable GPU and drivers

## Setup

Clone and initialize submodules:

```bash
git clone <repo-url>
cd vkmEngine
git submodule update --init --recursive
```

### What the clone does not carry

The engine's own `assets/` is excluded except for the three shipped
subdirectories - `logo/`, `fonts/` and `docs/` - so none of the art the engine
was developed against comes with a clone: no models, no environment maps. A
clone builds and runs without them; what is missing is only what a scene names,
so a project whose `assets/envs/` is empty gets an environment picker listing
nothing rather than an editor that will not open. The two examples that build
their worlds in code need nothing at all, and the one that does not -
`physics_lab` - carries its own source art and asset recipes inside the project,
which is why `examples/physics_lab/assets/` and `examples/physics_lab/library/`
are named exceptions to the blanket `examples/*/` rules in `.gitignore`.

Test art is whatever you point it at. What the engine was developed against, if
you want the same: Khronos' glTF sample models (`DamagedHelmet`,
`DragonAttenuation`, `Sponza`, `BrainStem`) and any equirectangular `.hdr`
dropped in `assets/envs/`. The one fixture the repo does own is generated rather
than stored - `tools/make_multimesh_rig.py <out.gltf>` writes the multi-mesh rig
that `docs/reference/animation.md` describes.

## Build Commands

An in-source build is refused rather than warned about, and a configure that
names no build type gets `RelWithDebInfo` - an unset one silently means neither
optimisation nor debug info. Asserts stay on whatever the build type
(`VKM_ASSERTS`); turn them off in a tree you profile.

```bash
# Configure
cmake -B build -G Ninja

# Build
cmake --build build

# Run (every host builds by default)
./build/bin/vkm_editor examples/potion_runner    # edit a project
./build/bin/vkm_runtime examples/potion_runner   # play it
./build/bin/vkm_cook examples/potion_runner      # bake its assets, no window
./build/bin/vkm_server examples/physics_lab      # serve it to players, no window
build\bin\vkm_editor.exe                         # Windows
```

Those run what was built last. `tools/vkm` is the same front end a game uses,
and builds the project's module and cooks its assets before it starts anything:

```bash
./tools/vkm run examples/physics_lab               # build, cook and play
./tools/vkm run examples/physics_lab --players 2   # with a local server
./tools/vkm package examples/physics_lab           # a standalone game under its dist/
```

[getting-started.md](../getting-started.md#the-commands) lists every command.

Every one takes **a project directory**, and every one applies the same rule: the
project is the one beside the executable, unless an argument names a different
one. So a shipped game ships its exe next to its `project.json` and the player
passes nothing. See [io.md](io.md#projects-and-the-three-roots) for
what a project is and how the three roots divide engine data, project data and
one user's own settings.

**Exit codes are meant to be read.** A host that could not open the project it
was handed exits non-zero rather than falling back to something that merely looks
like it worked, so `vkm_runtime <project>` doubles as a boot check from a shell:

```bash
timeout 10 ./build/bin/vkm_runtime examples/potion_runner
# 124 - still running when the timeout killed it, i.e. it booted
#   1 - it refused: no gameplay module, or no world of its own to open
```

Every host that runs the engine loop - `vkm_editor`, `vkm_runtime` and
`vkm_server` - handles SIGINT/SIGTERM (`Engine::run`), so a boot check exits
through the same shutdown a closed window does. Which conditions are fatal to which host,
and why the editor opens projects the runtime refuses, is in
[io.md](io.md#what-each-host-does-when-a-project-will-not-open).

The executables land in `build/bin/` - one directory so an exe finds its DLLs
(set by `CMAKE_RUNTIME_OUTPUT_DIRECTORY` in the top-level CMakeLists). Each
project's gameplay module builds into that project's own `bin/` instead, because
it belongs to the project rather than to this build tree.

That module is called `game` in every configuration - `vkm_add_gameplay_module`
clears `DEBUG_POSTFIX`, because the host resolves the file by name at run time
and a name that moved with the build type would be a file nothing loads. The
consequence is that two build trees of different configurations write the same
`<project>/bin/libgame.so` and the later build wins, so **run a host from the
tree that built the module it will load**. Mixing them puts two copies of
`vkm_core` in one process. ELF usually interposes the two into one and it
appears to work; what does not survive is anything with a static initializer
per copy - Tracy's profiler asserts on its own singleton guard.

## The examples build the way a game does

`examples/potion_runner`, `examples/physics_lab` and `examples/stress_arena` are
projects, and one `CMakeLists.txt` each serves both ways of building them. Configured on its own -

```bash
./tools/vkm build examples/potion_runner
```

- it is the top-level project, calls `find_package(vkmEngine)` against the
package the engine's build tree exports (or against an installed SDK), and links
`vkmEngine::vkm_core`: the same path a project made by `vkm new` takes. Added by
the engine's own `CMakeLists.txt` it skips `project()` and `find_package`,
because those targets are already defined in that tree.

The recipe both paths call is `vkm_add_gameplay_module()`, in
`cmake/gameplay_module.cmake`. It ships beside `vkmEngineConfig.cmake` and is
included from there, so the examples are built by the code an SDK hands a user
rather than by a second copy of it that can drift. `vkm build` is what a user
runs; keeping the examples on it is what keeps it working.

Given no `SOURCES`, it compiles every `.cpp` under the project's `src/`, which is
also the module's include root. It reads the engine the project was made for
from `engineVersion` in `project.json` - the one place a project records it -
and refuses at configure an engine of another minor release
(`vkm_check_engine_version`).

## Tests

```bash
ctest --test-dir build
```

`enable_testing()` sits in the **top-level** `CMakeLists.txt`, which is what
makes `ctest` in `build/` see anything: an `add_test` in a subdirectory writes
nothing unless the top level asked for tests first.

The suites are split by **what each one needs to run**, not by what it covers:

| CTest name   | Runs                          | Needs                                    |
|--------------|-------------------------------|------------------------------------------|
| `vkm_gl`     | `vkm_gl_tests`                | nothing                                  |
| `vkm_engine` | `vkm_engine_tests`            | nothing                                  |
| `render`     | `vkm_render_tests`            | an EGL device                            |
| `vkm_tool`   | `tests/tools/vkm_tool.cmake`  | Python 3.8, and the build tree at `build/` |

`vkm_gl` covers the parts of vkmGL that need no GL context (vertex layout
arithmetic, and the shader preprocessor's include resolution, cycle guard and
version injection). It builds by default: `modules/CMakeLists.txt` forces
vkmGL's `VKM_GL_BUILD_TESTS` on, because that option otherwise defaults to "only
when vkm_gl is the top-level project" - which as a submodule means never.

`vkm_engine` is the engine's own, and it is not one file: a suite is
`tests/<area>/<suite>_tests.cpp`, the area being the source folder it tests
(`tests/physics/solver_tests.cpp`, `tests/net/wire_tests.cpp`,
`tests/tools/cook_tests.cpp`), globbed by `tests/CMakeLists.txt`. Only the
harness - `main.cpp`, `suites.h`, `support.h` - sits at the root, and a helper
one area's suites share sits in that area (`tests/physics/physics_support.h`).
A new suite is two edits: the file, and a row in `VKM_TEST_SUITES`
(`tests/suites.h`), from which both the entry-point declarations and
`main.cpp`'s dispatch table expand. The `docs` suite checks that every row is a
file named for it, every such file a row, and that the list below names them
all. The suites that run today are `core`, `ecs`, `hierarchy`, `query`,
`collision`, `solver`, `joint`, `ragdoll`, `character`, `resource`, `async`,
`cook`, `import`, `authoring`, `prefab`, `scene`, `play`, `undo`, `culling`,
`sky`, `animation`, `particle`, `wire`, `transport`, `replication`, `prediction`,
`session`, `ui`, `audio`, `script`, `hostile`, `docs`. All of it is computation over a
`Scene`, so it runs on a machine with no GPU and no display.

The binary takes suite names, which is what you want while working on one:

```bash
./build/bin/vkm_engine_tests --list      # what there is
./build/bin/vkm_engine_tests solver wire # just those two
./build/bin/vkm_engine_tests             # all of them, which is what CI runs
```

`render` is the GPU suite. It is separate precisely so `vkm_engine_tests` can
stay runnable with no graphics libraries at all: this one links EGL, opens the
device itself, compiles the shipped shaders with the backend's own prelude, and
**skips at runtime when there is no device** - so it is green on a build machine
and useful on a workstation. It is built only where CMake finds EGL, and
configure says which happened.

It also renders whole frames. Small scenes built in code, one per image in
`tests/render/golden/` - materials under the sun, a glossy floor's reflections,
fog, decals, a cut-out's shadow and others - go through visibility, `RenderView` and
`GLBackend` as a host drives them, onto an off-screen surface, and are compared
with the golden images in `tests/render/golden/` (a pixel counts as different
past 12/255 in a channel, a frame past 0.2% of its pixels; a failing frame is
written to `/tmp/vkm_golden_<name>.png`). Each golden records the GPU it was
made on and is compared only there. After a change that is meant to alter the
picture, regenerate them, look at them, and commit them with the change:

```sh
VKM_UPDATE_GOLDENS=1 ./build/bin/vkm_render_tests
```

`vkm_tool` runs `tools/vkm` as a user does - `vkm new` from the template, then
`doctor` and `clean` over the result - and builds nothing, so it takes a moment.
It is registered only for a build tree at `build/`, the one the tool finds the
engine in.

Asserts run in these builds (`VKM_ASSERTS`), so a check of what a shipping build
does with a broken invariant - a stale id quietly refused - is skipped there. A
second tree configured `-DVKM_ASSERTS=OFF` runs it; a Clang tree is a natural
one, since it also holds the doc blocks to their declarations.

What none of them cover is anything needing a window: the editor, input, and
every way the pieces meet. Walking the tool is still the verification for those.
New engine tests join the existing suites rather than starting a fourth binary.

What the hosts do offer a script is the boot check above: their exit codes
separate "opened the project" from "opened something else instead", which is the
half a smoke test turns on.

## CMake Targets

| Target | Type | Description |
|--------|------|-------------|
| `vkm_core` | **Shared lib** | Core engine: ECS, resources, IO, the non-render systems (animation/visibility/event/physics/script/hierarchy/...), platform, debug |
| `vkm_render` | **Shared lib** | Render system, backend abstraction, render view |
| `vkm_backend_gl` | Static lib | OpenGL backend implementation (GLBackend, GLView, passes, GPU resources) |
| `vkm_tools` | Static lib | What every host needs from `src/tools/`: the HDR environment and plain image decoders and the SDF font baker (`loader/`), and the host prologue (`project_boot`). No model import, no texture bake |
| `vkm_cook` | Static lib | The heavy importers (cgltf / ufbx model import with MikkTSpace tangents, stb image decode, miniaudio audio decode) + the asset cooker that bakes recipes into the cooked cache, with what it bakes with: mesh simplification and draw ordering (meshoptimizer), and the texture bake's mip filter and block encoder. Linked by `vkm_editor_app` and `vkm_cook_app` only |
| `vkm_editor` | Static lib | Editor UI, panels, overlays, gizmo, scene I/O |
| `vkm_build_info` | Interface lib | Compile-time build metadata (version, branch, commit hash) |
| `vkm_warnings` | Interface lib | Shared GCC/Clang warning flags; first-party targets opt in, submodules don't |
| `<project>_module` | Shared lib | One per project (`potion_runner_module`, `physics_lab_module`, `stress_arena_module`): that project's gameplay sources built by `vkm_add_gameplay_module()` as `game.dll`/`libgame.so` into the project's own `bin/`. The engine ships no gameplay of its own |
| `vkm_runtime_app` | Executable | Bare engine, no editor. Includes `app/engine_app.h` for the shared bootstrap; links no model importer and no ImGui. Runs as `vkm_runtime` |
| `vkm_editor_app` | Executable | Engine libs + `vkm_editor` + `vkm_cook`; loads the open project's module for hot-reload. Runs as `vkm_editor` |
| `vkm_cook_app` | Executable | Headless asset cook: `vkm_cook` with no window, no GL context and no `Engine`, so it runs over SSH and on CI. Runs as `vkm_cook` |
| `vkm_server_app` | Executable | The refereeing host: the same system stack a client runs, with no window and no render backend. Runs as `vkm_server` |
| `vkm_gl_tests` | Executable | vkmGL's context-free suite, registered with CTest as `vkm_gl`. Built by default; not installed. See [Tests](#tests) |
| `vkm_engine_tests` | Executable | The engine's own context-free suite, one file per suite under `tests/<area>/`, registered as `vkm_engine`. Takes suite names as arguments. Not installed |
| `vkm_render_tests` | Executable | The GPU suite, registered as `render`. Built only where CMake finds EGL; skips at runtime with no device. Not installed |

Every host executable's target carries an `_app` suffix: a CMake target name
must be unique across the project, and two of them are library names. The files
never collide - the executable `vkm_editor` (or `vkm_editor.exe`) goes to
`build/bin/` and the static library `libvkm_editor.a` to `build/lib/` - so
`OUTPUT_NAME` drops the suffix.

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
<prefix>/bin/       the hosts, the shared engine, and the vkm command
<prefix>/include/   the engine's public headers plus the third-party headers
                    they reach into
<prefix>/lib/cmake/vkmEngine/   what find_package(vkmEngine) loads, plus the
                    gameplay-module recipe it includes
<prefix>/shaders/   engine shaders
<prefix>/assets/    the editor's font and logo - engine chrome, not anyone's art
<prefix>/templates/ what `vkm new` copies
```

The install's `shaders/` and `assets/` are what `vkm package` takes out of an
SDK into a game (`ENGINE_DATA` in `tools/vkm`), beside a renamed `vkm_runtime`
and the shared libraries, so a package assembled from the engine's own build
tree is the same thing as one assembled from an install. The repo's `assets/`
additionally holds the sample art the engine is developed against, which is
gigabytes and belongs to no game.

A downloadable archive comes from CPack, and carries the compiler in its name
because the engine is not ABI-stable across compilers:

```bash
cmake --build build --target package
# -> vkmEngine-<version>-Linux-x86_64-GNU-12.3.0.tar.xz
```

Building a game *with* that SDK is [getting-started.md](../getting-started.md).
Nothing there involves writing CMake.

### The shipping engine

A packaged game runs on a build of the engine of its own: `VKM_SHIPPING`, which
is `Release` with the profiler off and `-g` kept, so a crash a player sends
reads against the symbols `vkm package` splits out. It configures the runtime,
the server and what they load, and leaves out the editor, the cooker, the tests
and the examples - whose modules would otherwise land in the very `bin/` the
development hosts load from.

```bash
cmake -B build-shipping -G Ninja -DVKM_SHIPPING=ON
cmake --build build-shipping --target vkm_runtime_app vkm_server_app -j4
```

`vkm package` runs exactly that in the engine's tree, so this is only for doing
it by hand. A module a game ships is built against it too, into the project's
`build/shipping/` (`VKM_MODULE_DIR`), never over the development one.

An SDK carries one when the development tree is told where it is, and installs
it whole into `<prefix>/shipping`:

```bash
cmake -B build -G Ninja -DVKM_SHIPPING_ENGINE=$PWD/build-shipping
cmake --build build --target package
```

Without it, a game packaged from the SDK ships on the development engine, and
`vkm package` says so.

### Dependency Graph

```
vkm_editor_app (executable)             vkm_runtime_app (executable)
  |-- vkm_core                            |-- vkm_core (glm, vkm_log, nlohmann_json; glfw, glew, stb, miniaudio private)
  |-- vkm_render -- vkm_core              |-- vkm_render -- vkm_core
  |-- vkm_tools -- vkm_core               |-- vkm_tools -- vkm_core (registration, loader/, project_boot; no importer)
  |-- vkm_backend_gl -- vkm_gl, ...       |-- vkm_backend_gl -- vkm_gl, vkm_render, vkm_tools
  |-- vkm_cook -- vkm_core,               |-- vkm_build_info
  |     vkm_tools (+ cgltf, ufbx, mikktspace, meshoptimizer, stb_image_resize, bc7e private)
  |-- vkm_editor -- vkm_core,           vkm_cook_app (executable)
  |     vkm_tools, vkm_cook,              |-- vkm_cook -- vkm_core, vkm_tools
  |     imgui, vkm_gl                     |-- vkm_build_info
  |-- vkm_build_info                      (no window, no GL, no Engine)

vkm_server_app (executable)
  |-- vkm_core, vkm_render, vkm_tools, vkm_build_info
  (no window, no render backend; still links GL and X11 through vkm_core's glfw)

vkm_editor_app, vkm_runtime_app and vkm_server_app #include app/engine_app.h
for Vkm::App::setupEngineApp (no EngineApp lib). vkm_cook_app does not: it
constructs a Scene and a ResourceManager directly and never builds an Engine,
which is what lets it run headless. vkm_runtime_app links neither vkm_cook nor vkm_editor, so it pulls in
no model importer and no ImGui - the link lists enforce that, not a build flag.

<project>_module (shared, per project) -- vkm_core; the engine is one shared
  library, so linking it references the copy the hosts already load rather than
  carrying a second one. That is what keeps a single typeId registry and a
  single set of singletons, and it is why one module file serves both hosts: the
  module binds to the engine, not to whichever exe opened it. Against an
  installed SDK the same link is spelled vkmEngine::vkm_core, which is what
  vkm_add_gameplay_module() writes.
```

## External Modules

Every external dependency is a git submodule under `modules/`, pinned to a
commit (see `.gitmodules`); updating one is moving its pin. A library the
engine uses one file of is still a submodule; `basis_universal`, of which one
encoder file is compiled, is cloned shallow.
`testTheModuleListsAreTheModules` holds this table and the README's list to
that directory:

| Module | Path | Provides |
|--------|------|----------|
| **vkmGL** | `modules/vkmGL` | OpenGL object wrappers (`Vkm::GL::`), shader loading/preprocessing. Vendors GLEW privately; everything else it needs is a target the engine supplies |
| **vkmLog** | `modules/vkmLog` | Logging library (`Vkm::Log::`): LOG_TRACE..LOG_FATAL, VKM_ASSERT |
| **glm** | `modules/glm` | Vector/matrix math, used engine-wide and by vkmGL |
| **glfw** | `modules/glfw` | Window + input platform layer |
| **stb** | `modules/stb` | `stb_image` (texture decode), `stb_truetype` (SDF font bake), and for the cooker alone `stb_image_resize2` (mip chains) |
| **miniaudio** | `modules/miniaudio` | Playback device + wav/mp3/flac decode; absorbed privately by vkm_core, and only `system/audio` and the audio importer include it |
| **imgui** | `modules/imgui` | Dear ImGui, its `docking` branch, for editor UI: the panels dock into one dockspace |
| **freetype** | `modules/freetype` | Font rasterizer, trimmed to the core; vendored so the build does not depend on a system libfreetype |
| **json** | `modules/json` | nlohmann/json (`nlohmann_json`); serialization + asset `source` descriptors |
| **basis_universal** | `modules/basis_universal` | The cooker's BC7 encoder: only `encoder/basisu_bc7e_scalar.cpp` of Binomial's maintained library is compiled (the `bc7e` target), so the submodule is shallow; linked by vkm_cook alone (BC4/BC5 are the engine's own, `src/tools/cook/bc4_encoder.h`) |
| **cgltf** | `modules/cgltf` | glTF and GLB import: `cgltf.h`, pinned to v1.15; linked privately by vkm_cook and by the test suite |
| **ufbx** | `modules/ufbx` | FBX and OBJ import: `ufbx.h` and `ufbx.c`, pinned to v0.23.1; converts every file to the engine's axes and metres as it loads; linked privately by vkm_cook and by the test suite |
| **mikktspace** | `modules/mikktspace` | The MikkTSpace reference implementation, `mikktspace.c` and its header: the tangents bakers bake normal maps against, built for every imported mesh that carries none; linked privately by vkm_cook and by the test suite |
| **meshoptimizer** | `modules/meshoptimizer` | LOD simplification (`decimateMesh`), the vertex-cache / vertex-fetch order every cooked mesh is baked in (`optimizeMeshForGpu`) and the weld every imported mesh goes through; pinned to v1.3, linked privately by vkm_cook and by the test suite |
| **tracy** | `modules/tracy` | Tracy profiler client (`TracyClient`), linked by vkm_core only when `VKM_PROFILER` is on |

## Shaders

GLSL shaders live in `shaders/`, one folder per program, grouped by pipeline
stage (each backend pass owns its program):

```
shaders/
  *.glsl        # the shared includes every program below pulls from, at the root
  forward/      # pbr/ (the ubershader), pbr_skinned/, prepass/, prepass_skinned/
  shadow/       # depth/ (atlas tiles and cube faces alike), depth_masked/ (cutouts), and a skinned variant of each
  ibl/          # equirect/, sky/, irradiance/, prefilter/, brdf/  (IBL bake)
  irradiance/   # project/, backface/  (the SH probe grid bake)
  resolve/      # geometry/, color/  (the MSAA resolves)
  gtao/  cluster/  fog/  reflection/
  bloom/  dof/  decal/  particle/
  skybox/  grid/  composite/  ui/  splash/
```

Each folder contains the program's source files, named after the GL stage they
target. The loader (vkmGL) hard-codes these names:

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

The engine's own shader preprocessor resolves `#include` directives between
`.shader`/`.glsl` files (cycle-safe). It also prepends a prelude - the
`#version` and the engine's cross-language constants - which `GLBackend` builds
from the C++ constants themselves; see
[lighting.md](lighting.md#limits-and-the-shader-prelude).

## Reaching a platform header

Two headers exist so that nothing else has to get this right, and both are worth
knowing before adding a file that needs either.

`src/engine/platform/windows_api.h` is **the one way this project includes a
Windows system header**. Three defines have to precede the first `<windows.h>`
in a translation unit and `NOGDI` is not optional - the GDI half defines `ERROR`
as `0`, which stops vkmLog's `LogLevel::ERROR` from parsing. `windef.h` then
defines `far`, `near` and `pascal` as empty macros, which turn an ordinary
identifier of any of those names into a syntax error somewhere else in the file;
the header undefines them again. `winsock2.h` goes first, because `windows.h`
would otherwise pull in Winsock 1 and a translation unit cannot have both. All
of it sits inside a `_WIN32` guard, so include it unconditionally and it costs
nothing on Linux.

`src/engine/platform/window/glfw_include.h` is the same idea for GLFW: it pulls
the Windows headers in through the header above, then defines
`GLFW_INCLUDE_NONE` so GLEW owns the GL function declarations rather than GLFW's
own header.

Neither is enforced by anything. A new file that includes `<windows.h>` or
`<GLFW/glfw3.h>` directly compiles on Linux and fails on Windows, in a place
that has nothing to do with what it changed.

## Compiler Flags

First-party targets opt into a shared warning set by linking the `vkm_warnings`
interface lib (GCC/Clang dialect only; MSVC is left untouched). Submodules do
**not** link it, so third-party code keeps its own warning level:
- `-Wall -Wextra` (warnings enabled)
- `-Wno-unused-parameter` - an interface method that ignores an argument is the
  point of a default implementation
- `-Wshadow`, `-Wunused-variable`, `-Wunused-value`, `-Wformat=2`,
  `-Wnon-virtual-dtor` and `-Woverloaded-virtual` on top of `-Wall -Wextra`: six
  the tree already satisfies, kept on so it goes on satisfying them
- GCC only: `-Wno-stringop-truncation` - `Name`'s fixed-buffer `strncpy` is
  intentional
- Clang only: `-Werror=documentation` - a `@param` that names no parameter, or a
  `@return` on a function returning nothing, fails the build, so a Clang build
  is the check on the doc blocks

Third-party include paths are added `SYSTEM`, so none of this reaches a header
the engine does not own.

First-party code also builds as strict C++17 (`CMAKE_CXX_EXTENSIONS OFF`).

### Sanitizers

A separate build tree with AddressSanitizer and UndefinedBehaviorSanitizer on
every target, submodules included:

```bash
cmake -B build-asan -G Ninja -DVKM_SANITIZE=address,undefined -DVKM_PROFILER=OFF
cmake --build build-asan -j4
ctest --test-dir build-asan
```

`-fno-sanitize-recover=all` makes the first report fatal, so a suite that
passes under it read no byte it should not have. Run it after touching anything
that reads bytes the process did not write - the cooked readers, the scene and
prefab loaders, the wire.

## Build Definitions

| Define | Scope | Purpose |
|--------|-------|---------|
| `-UNDEBUG` | vkm_core (public) | With `VKM_ASSERTS` (on by default, off in a shipping engine): `VKM_ASSERT` and `assert()` run in every first-party file and every gameplay module, whatever the build type |
| `VKM_PROFILER=1` | vkm_core (public) | Enables Tracy CPU+GPU zones via debug/profiler.h. Default ON unless `CMAKE_BUILD_TYPE` is exactly `Release`. Pass `-DVKM_PROFILER=OFF` to force off; a shipping engine (`VKM_SHIPPING`) always does |
| `GLM_ENABLE_EXPERIMENTAL` | vkm_core (public) | GLM experimental features |
| `GLM_FORCE_INTRINSICS` | vkm_core (public) | GLM SIMD intrinsics |
| `VKM_ENGINE_VERSION` | vkm_core (public) | The engine version a gameplay module reports through `vkmModuleEngineVersion()`; a host refuses a module whose string differs |
| `APP_VERSION` | vkm_build_info | Engine version string |
| `APP_ROOT_DIR` | vkm_build_info | Absolute path to the **engine** root - the fallback `ProjectPaths::engineRoot()` uses to find `shaders/` and `assets/` when the exe is run from a build tree. Not the project root; see [io.md](io.md#projects-and-the-three-roots) |
| `APP_BRANCH`, `APP_COMMIT_HASH`, `APP_BUILD_DATE` | vkm_build_info | Git metadata |

### Profiling with Tracy

When `VKM_PROFILER=1` - which is every build type but an exact `Release`, the
default `RelWithDebInfo` included - the engine emits per-frame
`FrameMark`, per-stage CPU zones, and per-pass CPU+GPU zones over TCP.
Attach the Tracy profiler GUI (built separately from `modules/tracy/profiler`)
to inspect a live capture. Macros are in `src/engine/debug/profiler.h` -
engine code never includes Tracy headers directly.

The client is on demand and listens on its own machine only
(`TRACY_ONLY_LOCALHOST`, `modules/CMakeLists.txt`): it costs nothing until a
profiler connects, and a game packaged on the development engine
(`vkm package --development`) opens no port to the network. To profile from another machine, turn that option off.

### Measuring a change

A performance change is judged by a capture, before and after, on the scenes
it touches - never by reading the code. Headless, with no GUI:

```sh
./build/bin/vkm_runtime examples/stress_arena &
modules/tracy/capture/build/tracy-capture -o run.tracy -a 127.0.0.1 -s 40 -f
modules/tracy/csvexport/build/tracy-csvexport -u run.tracy    > cpu.csv
modules/tracy/csvexport/build/tracy-csvexport -g -u run.tracy > gpu.csv
```

(`tracy-capture` and `tracy-csvexport` build from `modules/tracy/capture` and
`modules/tracy/csvexport`.) Divide each zone's summed time by the number of
`SwapBuffers` zones - one per frame; every stage name also opens a zone on each
fixed tick - after skipping the first ten seconds - scene build
and probe bakes land there. That is milliseconds per frame, which is the unit
a frame budget is written in; a one-second FPS counter cannot see 0.2 ms.

Measure both sides of the frame. `GPU.Frame` against the main thread's stages
says which one binds, and on the example projects it moves: the stress arena
is CPU-bound, the small scenes sit within a tenth of a millisecond of their
GPU. On the CPU side most of what the render loop pays is the driver
validating state - on NVIDIA roughly 9 us per framebuffer bind and 7 us per
draw after a state change - so a pass costs what it binds and draws, not what
its shaders compute.
