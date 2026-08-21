# Getting started

Making a game with vkmEngine, from an installed SDK. If you are working *on* the
engine rather than *with* it, read [building.md](building.md) instead.

## What you need

- A C++17 compiler **matching the one the SDK was built with**. The archive name
  says which: `vkmEngine-1.4.0-Linux-x86_64-GNU-12.3.0.tar.xz` was built with
  GCC 12. This is not advisory - see [the toolchain pin](#the-toolchain-pin).
- CMake 3.25 or newer.
- A GPU and driver supporting OpenGL 4.3.

## Install

Unpack the archive anywhere and put its `bin/` on your PATH:

```sh
tar xf vkmEngine-1.4.0-Linux-x86_64-GNU-12.3.0.tar.xz
export PATH="$PWD/vkmEngine-1.4.0/bin:$PATH"
```

You now have `vkm`, plus the three hosts it drives.

## Your first project

```sh
vkm new mygame
cd mygame
vkm build
vkm run
```

A spinning cube under a directional light. `vkm new` copied the SDK's template,
`vkm build` compiled `src/` into `bin/game`, and `vkm run` handed the project to
`vkm_runtime`, which loaded that module and ran it.

## What a project is

```
mygame/
    project.json      what the game is called, and which scene it opens
    CMakeLists.txt    finds the engine, compiles src/ into one module
    src/              your gameplay code
    assets/           your art
    scenes/           your saved scenes
    bin/              the built module - the one place a host looks
```

The engine is never rebuilt for your game. A packaged game is a renamed copy of
`vkm_runtime` plus your project's data, which is why `vkm package` takes
seconds rather than recompiling an engine.

## The commands

| | |
|---|---|
| `vkm new <name>` | make a project from the template |
| `vkm build` | compile `src/` into the loadable module |
| `vkm run` | play it |
| `vkm edit` | open it in the editor |
| `vkm cook` | bake assets into the form the runtime reads - no window needed, so it runs on a build machine |
| `vkm package` | assemble a standalone game under `dist/` |

## Shipping

```sh
vkm package
```

That cooks the assets, then assembles everything a player needs:

```
dist/mygame/
    bin/            the game executable, the engine libraries, the gameplay module
    project.json    what the game is
    assets/  scenes/  cooked/  library/     its content
    shaders/        the engine's
```

The executable is a renamed copy of `vkm_runtime` - the engine is never
rebuilt for a game, which is why this takes seconds. The player runs
`bin/mygame` and passes nothing: both roots resolve to the package directory, so
the game finds itself.

Zip that directory and it is the whole product. Nothing needs installing, and it
runs from wherever it is unpacked - the binaries carry a relative rpath rather
than a path baked in at build time.

Cook before you ship, which `vkm package` does for you. The runtime can neither
import nor cook; it reads what the cooker produced. That step needs no window and
no GPU, so it runs on a build machine.

Each takes an optional project path and otherwise uses the current directory,
including from any subdirectory of a project.

## Writing a behavior

A `Behavior` is the engine's MonoBehaviour analogue: subclass it, override the
hooks you want, attach it to an entity. Deriving from `ReflectedBehavior`
generates `typeName()`, `visitFields()` and `clone()` from the reflected fields,
so tunable values appear in the editor and survive a save with no extra code.

```cpp
namespace Game {

class Spinner : public Vkm::Engine::ReflectedBehavior<Spinner> {
    public:
        static constexpr const char* TYPE_NAME = "Spinner";
        void onUpdate(float dt) override;

    public:
        float degreesPerSecond = 90.0f;
};

} // namespace Game

// At global scope, and named in full. The macro opens Vkm::Engine::Reflect
// itself, so your types stay in your own namespace.
VKM_REFLECT_BEGIN(::Game::Spinner)
    VKM_F(degreesPerSecond)
VKM_REFLECT_END()
```

Register it in `src/module.cpp` so scenes can name it:

```cpp
extern "C" void vkmRegisterBehaviors() {
    Vkm::Engine::BehaviorRegistry::get().registerBehavior<Game::Spinner>();
}
```

See [scripting.md](../reference/system/scripting.md) for the full lifecycle.

## Two entry points

A module exports what the host looks for:

| Entry | Required | Purpose |
|---|---|---|
| `vkmModuleEngineVersion` | yes | the engine version this was built against |
| `vkmRegisterBehaviors` | yes | registers your behavior types |
| `vkmBuildScene` | no | builds a world in code, for a project without an authored scene |

The template writes all three. If your project authors scenes in the editor, set
`entryScene` in `project.json` and delete `vkmBuildScene`.

`vkm run` needs the module. Without it every behavior in your scene is dropped on
load and the game draws a world that does nothing, so `vkm_runtime` refuses and
exits non-zero rather than playing that. `vkm_editor` opens the project anyway -
build the module, then reload it from inside the editor.

## The toolchain pin

The engine ships prebuilt libraries and C++ headers, and your module is compiled
against them. That is Unreal's model and it is a deliberate trade: struct
layouts, inline functions and templates can change between engine versions, which
is what lets them keep improving. The price is that **your module must be built
with the same toolchain as the engine**, and rebuilt for each engine release.

Two guards enforce it, because getting this wrong does not fail at link time - it
fails at run time, as a crash inside a function that looks innocent:

- `find_package(vkmEngine)` **fails to configure** if your compiler differs in
  id or major version from the one that built the SDK.
- The host **refuses to load** a module built against a different engine version,
  and says so in a sentence.

If you need to override the first (you are on your own):
`-DVKMENGINE_SKIP_TOOLCHAIN_CHECK=ON`.

## The one convention that will catch you

**Forward is `+Z`, not `-Z`.** A sun overhead needs a *positive* pitch, and
`glm::quatLookAt` aims 180 degrees the other way. Nothing errors when you get it
wrong - the scene just looks wrong, usually unlit.

## Where to go next

- [ecs.md](../reference/ecs.md) - entities, components, and the query API
- [scripting.md](../reference/system/scripting.md) - the behavior lifecycle in full
- [ui.md](../reference/system/ui.md) - in-game UI
- [editor.md](../reference/editor.md) - what the editor can author
