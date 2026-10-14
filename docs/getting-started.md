# Getting started

Making a game with vkmEngine, from an installed SDK. If you are working *on* the
engine rather than *with* it, read [building.md](reference/building.md) instead.

## What you need

- Linux or Windows on x86-64, and a GPU and driver supporting OpenGL 4.5.
- On Linux, the C library's headers, which most distributions install with
  their compiler: `sudo apt install libc6-dev` (Debian, Ubuntu), `glibc-devel`
  (Fedora). `vkm doctor` says when they are missing.

Nothing else. The SDK carries the Python `vkm` runs on, and the first build
fetches the compiler, CMake and Ninja the engine was built with
([the toolchain pin](#the-toolchain-pin)). Every release comes built with GCC
and with Clang; you pick which compiler builds your game.

## Install

On Linux:

```sh
curl -fsSL https://github.com/V-KMilev/vkmEngine/releases/latest/download/install.sh | sh
```

On Windows, in PowerShell:

```powershell
irm https://github.com/V-KMilev/vkmEngine/releases/latest/download/install.ps1 | iex
```

Either installs the newest release for you alone, built with GCC, with no
administrator: the engine in `~/.local/share/vkmEngine`
(`%LOCALAPPDATA%\Programs\vkmEngine` on Windows), `vkm` in `~/.local/bin`
(beside the engine on Windows), which the script puts on your PATH or tells you to, and
**vkmEngine** in your app menu, which opens the editor. For Clang, give the
script `VKM_COMPILER=clang` - on the shell's side of the pipe,
`curl ... | VKM_COMPILER=clang sh`, or `$env:VKM_COMPILER = 'clang'` before
`irm` on Windows; for a release other than the newest, `VKM_VERSION=<version>`
the same way. One engine is installed at a time. `uninstall.sh` in
the engine's folder removes it on Linux, and Apps & features does on Windows -
your projects stay where they are.

Or unpack a release archive anywhere and run its `vkm` where it is:

```sh
tar xf vkmEngine-<version>-linux-x64-gcc.tar.xz
vkmEngine-<version>-linux-x64-gcc/vkm new mygame
```

On Windows, Git Bash runs `vkm` as it is, and `cmd.exe` and PowerShell through
the `vkm.cmd` beside it - either way, as `vkm`.

## Your first project

```sh
vkm new mygame
cd mygame
vkm run
```

A cube spinning over a floor under a directional light, until a dropped ball
turns it the other way. `vkm new` copied the SDK's template and stamped your
name and this SDK's version into it. `vkm run` compiled `src/` into the
**gameplay module**, `bin/libgame.so` (`bin/game.dll` on Windows), cooked the
assets, and handed the project to `vkm_runtime`, which loaded the module and ran
it. Building and cooking are no-ops when nothing changed, so
`vkm run` is the one command to repeat while you work.

To start from one of the examples instead, copy it:

```sh
vkm new lab -t physics_lab      # or potion_runner, stress_arena, city_block
```

The editor runs the same commands - New Project, File > Build Scripts and
Package Game - and shows their output in its Build tab.

`engineVersion` in `project.json` records the engine a project was made for.
Built against another *minor* release, configure stops and names both; set
`engineVersion` to the new one when you mean to move, or let the editor's start
screen do it. A later patch of the same minor builds as it is.

When something will not build or start, `vkm doctor` checks the engine, the
toolchain and the project, and says what to fix.

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

A new `.cpp` under `src/` is in the next build. A library your code links is
one `target_link_libraries(game_module PRIVATE ...)` in `CMakeLists.txt`. The
engine is never rebuilt for your game.

## The commands

| | |
|---|---|
| `vkm new <name>` | make a project from the template; `-t physics_lab` copies an example |
| `vkm run` | build, cook and play it |
| `vkm run --players N` | the same, as a local server with N players joined to it - multiplayer from one terminal |
| `vkm run --connect HOST[:PORT]` | join a game someone is serving |
| `vkm edit` | build it and open it in the editor; a failed build still opens it |
| `vkm serve` | build, cook and host it for players - `vkm_server`, no window, needs `vkmSetupNetwork` |
| `vkm build` | only compile `src/` into the gameplay module |
| `vkm cook` | only bake assets into the form the runtime reads - no window needed, so it runs on a build machine |
| `vkm package` | build, cook and assemble the game a player gets, under `dist/` |
| `vkm doctor` | check the engine, the toolchain and the project, and say what to fix |
| `vkm toolchain` | fetch the pinned compiler, CMake and Ninja now, rather than at the first build |
| `vkm clean` | delete what the commands generated: `build/ bin/ cooked/ logs/`, and `dist/` with `--all` |

Each but `vkm new` and `vkm toolchain` takes an optional project path, and
otherwise uses the project the current directory is in. `vkm <command> -h` shows
a command's options with examples; `-v` shows every command it runs and their
output.

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

**Source art stays behind**: your `.blend`, `.fbx`, `.gltf` and source
textures are in `cooked/` in the only form the runtime opens, and of the import
recipes only your materials ship, since their recipe *is* their runtime form. A
file nothing imported - an `.hdr` your scene names, `assets/logo/icon.png` -
ships as it is.

The executable is `vkm_runtime` renamed for your game; the player runs
`bin/mygame` and passes nothing. On Windows the package also writes the game's
name, version and icon (`assets/logo/icon.png`, at most 256 pixels a side) into
it. The folder is the whole product: nothing needs installing, and it carries
the engine's license as `LICENSE-vkmEngine.txt`. `vkm package --archive` also
packs it into a `.zip` on Windows, a `.tar.gz` elsewhere.

Beside the folder, `dist/mygame-0.1.0-linux-x64-symbols/` holds the binaries' debug
info. Keep it for every version you ship - a player's crash report is read
against it - and never ship it. `--debug-info` leaves it in the binaries.

A multiplayer game is hosted by the dedicated server and nothing else, so
`vkm package --server` carries it too, as `bin/mygame-server`; started with no
arguments, it serves the game it sits in.

**On a laptop with two GPUs** the game wants the discrete one. On Windows the
game and the editor ask NVIDIA's or AMD's driver for the discrete GPU
themselves. On Linux the environment decides:
`__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia` before the command
for NVIDIA's offload, `DRI_PRIME=1` for Mesa's.

### The shipping engine

A game ships on a second build of the engine in the SDK's `shipping/`, with
asserts off ([building.md](reference/building.md#the-shipping-engine)).
`vkm package --development` packages on the development engine instead, for
testing.

## Writing a behavior

A `Behavior` is a C++ class you attach to an entity: override the hooks you
want, and list the fields a designer tunes in a reflect block, which makes them
appear in the editor and survive a save. The template's `src/game.h` and
`src/game.cpp` hold one, `Spinner`; cut to its core:

```cpp
// src/game.h
#pragma once

#include "system/script/behavior_api.h"

namespace Game {

// Your types stay in Game, never in Vkm::Engine; this makes the engine's reachable.
using namespace Vkm::Engine;

class Spinner : public ReflectedBehavior<Spinner> {
    public:
        void onUpdate(float dt) override;

    public:
        float degreesPerSecond = 90.0f;
};

} // namespace Game

// At global scope, the type named in full. One VKM_F per field, no commas.
VKM_REFLECT_BEGIN(::Game::Spinner)
    VKM_F(degreesPerSecond)
VKM_REFLECT_END()
```

```cpp
// src/game.cpp
#include "game.h"

void Game::Spinner::onUpdate(float dt) {
    Transform* transform = tryGet<Transform>();   // this entity's, or null
    if (!transform) return;

    const glm::quat spin = glm::angleAxis(glm::radians(degreesPerSecond * dt), Math::WORLD_AXIS_Y);
    transform->rotation = glm::normalize(transform->rotation * spin);
}
```

Register it in `src/module.cpp` so scenes can name it:

```cpp
VKM_MODULE_ENTRY
void vkmRegisterBehaviors() {
    Vkm::Engine::BehaviorRegistry::get().registerBehaviors<Game::Spinner>();
}
```

[scripting.md](reference/scripting.md#your-first-behavior) walks a behavior end
to end - attaching it, input, collisions, sound - and then the full lifecycle.

## The module's entry points

`src/module.cpp` defines the functions the host looks for, each marked
`VKM_MODULE_ENTRY` ([scripting.md](reference/scripting.md#the-gameplay-module)).

The template writes `vkmModuleEngineVersion`, `vkmRegisterBehaviors` and
`vkmBuildScene`. If your project authors scenes in the
editor, set `entryScene` in `project.json` and delete `vkmBuildScene`; if it is
never played over a wire, leave `vkmSetupNetwork` out and the host runs offline.

`vkm_runtime` refuses to start without the module, which is why `vkm run` builds
first ([what each host does](reference/io.md#what-each-host-does-when-a-project-will-not-open)).

## The toolchain pin

Your module is compiled against the engine's prebuilt libraries and C++
headers, whose layouts and inline code change between releases. So **your
module must be built with the same compiler as the engine**, and rebuilt for
each engine release.

The SDK brings that compiler. `bin/toolchain.json` names the compiler, CMake and
Ninja the release was built with, where to download each and its SHA-256. The
first build fetches them into `~/.cache/vkm/tools` (`%LOCALAPPDATA%\vkm\tools`
on Windows; `VKM_TOOLS_DIR` moves it), checks each against its hash, and every
build after uses them, whatever else is installed. A packaged game carries the
compiler's runtime libraries, so a player needs nothing installed.

Getting this wrong fails at run time, as a crash somewhere innocent, so guards
stop it earlier:

- `find_package(vkmEngine)` **fails to configure** if your compiler differs in
  id or major.minor version from the one that built the SDK.
- The host **refuses to load** a module built against a different engine version,
  and says so in a sentence.

`vkm build --compiler <path>` names another compiler, which the first guard then
judges; `-DVKMENGINE_SKIP_TOOLCHAIN_CHECK=ON` overrides it (you are on your own).

## The one convention that will catch you

**Forward is `-Z`, screen-right is `+X`, up is `+Y`** - right-handed, glm's own
convention, so `glm::quatLookAt` and the rest agree with the engine. Ask through
`Math::computeForward`, `computeRight` and `computeUp` rather than writing an
axis by hand. Screen-right is `cross(forward, up)`; `cross(up, forward)` is the
mirror. Nothing errors when you get it wrong - the scene just looks wrong,
usually unlit.

## Where to go next

- [ecs.md](reference/ecs.md) - entities, components, and the query API
- [scripting.md](reference/scripting.md) - the behavior lifecycle in full
- [ui.md](reference/ui.md) - in-game UI
- [networking.md](reference/networking.md) - what `vkmSetupNetwork`
  owes a session, and what does and does not travel
- [editor.md](reference/editor.md) - what the editor can author
