# Getting started

Making a game with vkmEngine, from an installed SDK. If you are working *on* the
engine rather than *with* it, read [building.md](reference/building.md) instead.

## What you need

- A C++17 compiler **matching the one the SDK was built with**. The archive name
  says which: `vkmEngine-0.10.0-Linux-x86_64-GNU-12.3.0.tar.xz` was built with
  GCC 12. This is not advisory - see [the toolchain pin](#the-toolchain-pin).
- CMake 3.25 or newer.
- Python 3.8 or newer, which runs `vkm`. On Windows it has to be a Windows
  build - MSYS2's `mingw-w64-ucrt-x86_64-python`, or python.org's. Started by
  MSYS's own Python, the one in `usr/bin`, `vkm` restarts itself under one of
  those, and says what to install when there is neither.
- A GPU and driver supporting OpenGL 4.3.

## Install

Unpack the archive anywhere and put its `bin/` on your PATH:

```sh
tar xf vkmEngine-0.10.0-Linux-x86_64-GNU-12.3.0.tar.xz
export PATH="$PWD/vkmEngine-0.10.0-Linux-x86_64-GNU-12.3.0/bin:$PATH"
```

You now have `vkm`, plus the hosts it drives. On Windows, Git Bash and MSYS2
shells run it as it is, and `cmd.exe` and PowerShell through the `vkm.cmd` beside
it - either way, as `vkm`.

## Your first project

```sh
vkm new mygame
cd mygame
vkm run
```

A spinning cube under a directional light. `vkm new` copied the SDK's template
and stamped your name and this SDK's version into it. `vkm run` then did three
things: compiled `src/` into `bin/libgame.so` (`bin/game.dll` on Windows), cooked
the assets, and handed the project to `vkm_runtime`, which loaded that module and
ran it. The first two are no-ops when nothing changed, so `vkm run` is the one
command to repeat while you work: what plays is always what is on disk.

That version, `engineVersion` in `project.json`, is the one place a project
records the engine it was made for. Build it against a later *minor* release
and configure stops, naming both versions, instead of building your module
against an engine it was not written for; set `engineVersion` to the new one
when you mean to move. A later patch of the same minor builds as it is.

The build uses the compiler the engine was built with, and on Windows every
command runs with that toolchain's `bin` first on PATH - so from any shell, Git
Bash or an MSYS2 one of another environment, the build and the engine's
executables find that toolchain's DLLs rather than a neighbour's. When something
will not build or start, `vkm doctor` checks the engine, the toolchain and the
project, and says what to fix.

## What a project is

```
mygame/
    project.json      what the game is called, its version, the engine it was
                      made for, and which scene it opens
    CMakeLists.txt    finds the engine, compiles every .cpp under src/ into one module
    src/              your gameplay code
    assets/           your art
    scenes/           your saved scenes
    library/          how each asset is imported, and your materials - written by
                      the editor, and as much source as your art
    .gitignore        what the tools below generate, none of it worth committing:
        build/        the module's build tree
        bin/          the built module - the one place a host looks
        cooked/       your assets in the form the runtime reads
        logs/         what each run said
        dist/         packaged games
```

You need not open `CMakeLists.txt`: a new `.cpp` under `src/` is in the next
build. It is still yours - a library your code links is one
`target_link_libraries(game_module PRIVATE ...)` below the rest.

The engine is never rebuilt for your game. A packaged game is a renamed copy of
`vkm_runtime` plus your project's data, which is why `vkm package` takes
seconds rather than recompiling an engine.

## The commands

| | |
|---|---|
| `vkm new <name>` | make a project from the template |
| `vkm run` | build, cook and play it |
| `vkm run --players N` | the same, as a local server with N players joined to it - multiplayer from one terminal |
| `vkm edit` | build it and open it in the editor; a failed build still opens it |
| `vkm serve` | build, cook and host it for players - `vkm_server`, no window, needs `vkmSetupNetwork` |
| `vkm build` | only compile `src/` into the gameplay module |
| `vkm cook` | only bake assets into the form the runtime reads - no window needed, so it runs on a build machine |
| `vkm package` | build, cook and assemble the game a player gets, under `dist/` |
| `vkm doctor` | check the engine, the toolchain and the project, and say what to fix |
| `vkm clean` | delete what the commands generated: `build/ bin/ cooked/ logs/`, and `dist/` with `--all` |

Each but `vkm new` takes an optional project path and otherwise uses the current directory,
including from any subdirectory of a project. `vkm <command> -h` shows a
command's options with examples; `-v` on any of them shows every command it runs
and all their output.

### What the steps are called

- **The project tool** is `vkm`, as `cargo` is Rust's.
- **The SDK** is the installed engine you build *against*; it is never rebuilt
  for a game.
- **Build** compiles your code into the **gameplay module**, the library the
  engine's **hosts** (`vkm_runtime`, `vkm_editor`, `vkm_server`, `vkm_cook`) load.
- **Cook** turns source art (`.fbx`, `.png`) into what the runtime reads.
- **Package** (Godot: *export*) assembles the folder a player runs.
- **The shipping engine** is the build a packaged game runs on: optimised, no
  profiler.
- **Debug info** (symbols) turns a crash address into a line of code; a package
  keeps it beside the game, not in it.

## Shipping

```sh
vkm package
```

That cooks, builds your module against the **shipping engine**, then assembles
everything a player needs - and only that:

```
dist/mygame-0.1.0-linux-x64/
    bin/            the game executable, the engine libraries, the gameplay module
    project.json    what the game is
    scenes/  prefabs/  cooked/    the world, and the art in the form it plays
    library/        your materials
    assets/         the engine's fonts and logo, and whatever the game still
                    opens by name: environment maps, its icon
    shaders/        the engine's
```

**What stays behind is the authoring half.** Your `.blend`, `.fbx`, `.gltf` and
source textures do not ship: `cooked/` already holds them in the only form the
runtime can open, and a shipped game links no importer that could read the
originals anyway. Nor do the recipes recording how each was imported - they name
paths on *your* machine, and a runtime cannot act on one. The one library part
that does ship is your materials, whose recipe *is* their runtime form; the
manifest that resolves an asset name in a scene to anything at all is derived,
and travels in `cooked/` with what it indexes.

`vkm package` works this out from what the cook recorded - every file each
asset's source art was read from, a `.gltf`'s buffers included - rather than
from a list of file extensions, so a file nothing imported - an `.hdr` your
scene names, the `assets/logo/icon.png` your window wears - ships as a matter of
course.

The folder is named for the game, its `version` in `project.json`, and what it
runs on (`windows-x64` on Windows), so packages of two versions, or from two
machines, sit side by side. The executable is `vkm_runtime` under your game's
name. The player runs `bin/mygame` and passes nothing: both roots resolve to the
package directory, so the game finds itself. On Windows the package also writes
the game's name and version into the executable, and its icon - your
`assets/logo/icon.png`, at most 256 pixels a side - so Explorer shows your game
rather than the engine.

That directory is the whole product. Nothing needs installing, and it runs from
wherever it is unpacked - the binaries carry a relative rpath rather than a path
baked in at build time. It ships the engine's libraries, so it carries the
engine's license too, as `LICENSE-vkmEngine.txt`: its terms go wherever a copy
of the engine does. `vkm package --archive` also packs it into one file to
hand out, a `.zip` on Windows and a `.tar.gz` elsewhere.

Beside it, `dist/mygame-0.1.0-linux-x64-symbols/` holds the binaries' debug
info, moved out of them: most of what the engine weighs, and nothing a player
needs. Keep it for every version you ship - a crash report from a player is read
against it - and never ship it. `--debug-info` leaves it in the binaries instead.

A multiplayer game is hosted by the dedicated server and nothing else, so
`vkm package --server` carries it too, as `bin/mygame-server`; started with no
arguments, it serves the game it sits in.

The runtime can neither import nor cook; it reads what the cooker produced, which
is why `vkm package` cooks first. Cooking needs no window and no GPU, so it runs
on a build machine.

### The shipping engine

The engine you develop with carries the profiler, so you can measure a frame. A
game ships on a second build, the **shipping engine**: optimised, no profiler,
debug info kept for crash reports. Your module is compiled again against it,
into `build/shipping/`, leaving the one in `bin/` alone. Unreal calls the two
Development and Shipping.

In the engine's tree the first `vkm package` builds it into `build-shipping/`,
which takes a while; an SDK may carry one, and `vkm doctor` says.
`vkm package --development` packages on your development engine in seconds, for
testing.

## Writing a behavior

A `Behavior` is the engine's MonoBehaviour analogue: subclass it, override the
hooks you want, attach it to an entity. Deriving from `ReflectedBehavior`
generates its name, `visitFields()` and `clone()` from the reflect block, so
tunable values appear in the editor and survive a save with no extra code.
`system/script/behavior_api.h` is the one include a behavior file needs.

```cpp
#include "system/script/behavior_api.h"

namespace Game {

// Your types stay in Game; this line makes the engine's reachable unqualified
// from inside it. Vkm::Engine is the engine's own namespace - a project adding
// to it can collide with an engine type added later, silently.
using namespace Vkm::Engine;

class Spinner : public ReflectedBehavior<Spinner> {
    public:
        void onUpdate(float dt) override;

    public:
        float degreesPerSecond = 90.0f;
};

} // namespace Game

// At global scope, and named in full. The macro opens Vkm::Engine::Reflect
// itself, so your types stay in your own namespace. One VKM_F per field, no
// commas; the class name is the behavior's name in a scene file.
VKM_REFLECT_BEGIN(::Game::Spinner)
    VKM_F(degreesPerSecond)
VKM_REFLECT_END()
```

Its `onUpdate` reaches its own entity's components without naming the entity:

```cpp
void Spinner::onUpdate(float dt) {
    Transform* transform = tryGet<Transform>();
    if (!transform) return;

    const glm::quat spin = glm::angleAxis(glm::radians(degreesPerSecond * dt), Math::WORLD_AXIS_Y);
    transform->rotation = glm::normalize(transform->rotation * spin);
}
```

`tryGet<T>()` is the one to reach for: one lookup, and the null check is the
"does it have one" question already answered. `get<T>()` is the same thing for a
component the entity is built with and cannot run without. To touch some *other*
entity, `scene()` has the same calls taking an entity.

Register it in `src/module.cpp` so scenes can name it:

```cpp
VKM_MODULE_ENTRY
void vkmRegisterBehaviors() {
    Vkm::Engine::BehaviorRegistry::get().registerBehaviors<Game::Spinner>();
}
```

[scripting.md](reference/scripting.md#your-first-behavior) walks a first
behavior end to end - attaching it, input, collisions, sound - and then the
full lifecycle.

## The module's entry points

A module exports what the host looks for. Declare each with `VKM_MODULE_ENTRY`
(`system/script/module_entry.h`), which gives it C linkage and, on Windows, the
export MSVC otherwise omits - a module missing the second builds fine and its
symbols cannot be found at run time, on that platform only.

| Entry | Required | Purpose |
|---|---|---|
| `vkmModuleEngineVersion` | yes | the engine version this was built against |
| `vkmRegisterBehaviors` | yes | registers your behavior types |
| `vkmBuildScene` | no | builds a world in code, for a project without an authored scene - it is handed the scene *and* the asset graph, so it can make the meshes and materials that world is drawn from |
| `vkmSetupNetwork` | no | says how players join and what they get; without it `vkm serve` has nothing to start |

The template writes the first three. If your project authors scenes in the
editor, set `entryScene` in `project.json` and delete `vkmBuildScene`; if it is
never played over a wire, leave `vkmSetupNetwork` out and the host runs offline.

The runtime needs the module. Without it every behavior in your scene is held as
text rather than run, and the game draws a world that does nothing, so
`vkm_runtime` refuses and exits non-zero rather than playing that - which is why
`vkm run` builds first. `vkm_editor` opens the project anyway, and loads the
module the moment a build lands.

## The toolchain pin

The engine ships prebuilt libraries and C++ headers, and your module is compiled
against them. That is Unreal's model and it is a deliberate trade: struct
layouts, inline functions and templates can change between engine versions, which
is what lets them keep improving. The price is that **your module must be built
with the same toolchain as the engine**, and rebuilt for each engine release.

Guards enforce it, because getting this wrong does not fail at link time - it
fails at run time, as a crash inside a function that looks innocent:

- `find_package(vkmEngine)` **fails to configure** if your compiler differs in
  id or major version from the one that built the SDK.
- `vkm_add_gameplay_module` **fails to configure** when `project.json` names
  another minor release of the engine.
- The host **refuses to load** a module built against a different engine version,
  and says so in a sentence.

If you need to override the first (you are on your own):
`-DVKMENGINE_SKIP_TOOLCHAIN_CHECK=ON`.

## The one convention that will catch you

**Forward is `-Z`, screen-right is `+X`, up is `+Y`** - right-handed, and glm's
own convention, so `glm::quatLookAt` and the rest agree with the engine without
being corrected. Ask through `Math::computeForward`, `computeRight` and
`computeUp` rather than writing an axis out by hand: a hand-rolled axis is the
one place this convention can be got wrong.

Screen-right is `cross(forward, up)`. `cross(up, forward)` is the mirrored one,
whichever convention you think you are in. Nothing errors when
you get it wrong - the scene just looks wrong, usually unlit.

## Where to go next

- [ecs.md](reference/ecs.md) - entities, components, and the query API
- [scripting.md](reference/scripting.md) - the behavior lifecycle in full
- [ui.md](reference/ui.md) - in-game UI
- [networking.md](reference/networking.md) - what `vkmSetupNetwork`
  owes a session, and what does and does not travel
- [editor.md](reference/editor.md) - what the editor can author
