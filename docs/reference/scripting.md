# Scripting (Behaviors)

Native C++ gameplay. A behavior derives from `Behavior<T>`, overrides the hooks it needs, and
rides an entity in a `ScriptComponent`. Behaviors live in the project's gameplay module, which
the editor reloads without restarting.

`BehaviorSystem` runs in `SystemStage::Simulation`, **before** `AnimationSystem` and
`PhysicsSystem`, which advance only on the fixed tick. A frame runs its ticks first and its
frame pass after, so what `onFixedUpdate` writes is integrated that same tick, and what
`onUpdate` writes waits for the next tick, on a later frame.

## Your first behavior

A behavior is a C++ class in your project's `src/`, built into the project's module by
`vkm build`. Include `system/script/behavior_api.h`: it brings what gameplay commonly
reaches - the base class, every component, input, physics queries, the UI, the network
session, the log and the keys by name ([input.md](input.md#keys-and-buttons)) - and the
module's build precompiles it, so it costs a file nothing. Anything else is included by its
path under `src/engine/`. A module logs in the category `GAME`, which its build sets: a
`#define VKM_LOG_CATEGORY` of its own redefines it, and the compiler warns.

**The class and its fields.** Derive from `Behavior<YourClass>`, put the values a designer
tunes in a public block, and list them in a reflect block below the class - one `VKM_F` per
field, no commas. The class name is the behavior's name: it is what a scene file stores and
what the editor lists under Add Component's **Behaviors**.

```cpp
// src/crate.h
#pragma once

#include "system/script/behavior_api.h"

namespace Game {

using namespace Vkm::Engine;

class Crate : public Behavior<Crate> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;
        void onCollisionEnter(const Collision& hit) override;

    public:
        float                    pushStrength = 6.0f;
        glm::vec4                glowColor    = {1.0f, 0.6f, 0.2f, 1.0f};
        AssetRef<AudioClipAsset> thud;
};

} // namespace Game

VKM_BEHAVIOR(::Game::Crate)
    VKM_F(pushStrength, 0.0f, 20.0f, "How hard a push shoves it, m/s")
    VKM_F(glowColor)
    VKM_F(thud, "Played when it lands")
VKM_REFLECT_END()
```

A field the block does not list is runtime state: not saved, not shown, not copied. A
behavior with nothing to tune still writes the block, empty. A row may carry a tip -
`VKM_F(name, "tip")` - and before it the range the inspector drags it in -
`VKM_F(name, min, max, "tip")`. A `glm::vec3` or `glm::vec4` whose name ends in `color`
edits with a color picker.

**Registering it.** The `VKM_BEHAVIOR` block registers the behavior: a `Behavior<T>` without
one does not compile. `src/module.cpp` names none of them:

```cpp
#include "system/script/behavior_api.h"

VKM_GAME_MODULE()
```

A behavior whose header no `.cpp` includes is in no file the module compiles, and is not
registered.

**Attaching it.** In the editor, pick the behavior from the entity's Add Component, under
**Behaviors**; its fields appear on the entity's Script card. In code, `addBehavior` gives
the entity its `ScriptComponent` if it has none and returns the new behavior:

```cpp
Game::Crate& crate = addBehavior<Game::Crate>(scene, entity);
crate.pushStrength = 9.0f;
```

**Reading input.** List the action once, in `project.json`'s `actions` - `"Push": ["E"]` -
and ask for it by name ([input.md](input.md) has axes, and why a fixed update reads the
tick's command):

```cpp
// src/crate.cpp
#include "crate.h"

void Game::Crate::onStart() {
    LOG_INFO("Crate %u ready", entity().slot());
}

void Game::Crate::onUpdate(float dt) {
    Rigidbody* body = tryGet<Rigidbody>();
    if (body && pressed("Push")) body->linearVelocity += Math::WORLD_FORWARD * pushStrength;
}
```

**Collisions.** A body is a `Collider` and a `Rigidbody` together; a Collider without a
Rigidbody touches nothing and no hook hears it. Static ground takes a Rigidbody whose
`motion` is `RigidbodyMotion::Static`; something a behavior moves by hand takes `Kinematic`.
`onCollisionEnter` / `Stay` / `Exit` and `onTriggerEnter` / `Stay` / `Exit` hear this
entity's contacts ([physics.md](physics.md#events)).

**A sound.** A one-shot that needs no entity of its own is `playSoundAt`, or `playSound`
from this entity, for the clip the authored `thud` names. A replayed tick plays none: it
re-runs a moment already presented.

```cpp
void Game::Crate::onCollisionEnter(const Collision& hit) {
    playSoundAt(thud, hit.point);
}
```

A sound that must follow something, loop or stop is an `AudioSource` instead
([audio.md](audio.md)).

**Another behavior.** `findBehavior<T>(entity)` answers the `T` on an entity, or null
([recipes.md](../recipes.md#touching-and-finding) has it in use), and `findInScene<T>()` the
one a game has of it - a director, a menu.

**Listening for an event.** `subscribe` reads the event type off the lambda, and drops the
listener when the behavior goes:

```cpp
subscribe([this](const CrateReset& event) { reset(); });
```

A UI control is heard by its entity: `onClick(button, fn)` and `onChange(control, fn)`
([ui.md](ui.md#interaction)).

Before writing a second behavior, read [Time and pause](#time-and-pause): it says when each
hook runs.

## Behavior

`BehaviorBase` (`system/script/behavior_base.h`) is everything a behavior has beneath
`Behavior<T>`: its hooks, what its entity carries, the session's services, the network's
questions and the verbs that change the world. Its declarations say what each does; these
are the rules they leave to the page.

- **The services** from `scene()` to `debugDraw()` read one context that outlives every
  frame: safe from a `subscribe()` callback as well as a hook, and not worth caching in a
  member. None works in a constructor, as the context is bound just before `onStart`.
- **Debug drawing.** Drawn in `onFixedUpdate`, a line stands until the next tick's
  ([rendering.md](rendering.md#debug-drawing)).
- **Networking.** What a behavior asks the session, and how each question answers offline
  so a project is written the same with or without one, is
  [networking.md](networking.md#the-five-questions); an effect the server counts is
  [networking.md](networking.md#an-effect-the-server-counts).
- **The screen.** To aim the cursor into the world or pin a label over something, ask
  `screen()`, not `window()`: in the editor the scene is not drawn over the whole window
  ([ui.md](ui.md#the-world-on-the-screen)).
- **`destroy()`, `loadScene()` and `restartScene()`** wait for the hook pass to end, so a
  behavior may end its own entity or the scene it is in. What a restart starts over is the
  host's (`BehaviorSystem::setPlayedScene`, `BehaviorSystem::setRestartHandler`).
- **Spawning.** `spawn(name, parent)` parents through `HierarchyOperations::setParent`, UI
  elements included. A spawn is local to this end; so is a prefab,
  `instantiate(enemy, at)` with a `PrefabRef` field, which returns the subtree's root. What
  every end should see is `instantiateShared`, on the authority
  ([networking.md](networking.md#spawning)).
- **`subscribe()`** is the only way a behavior listens; see
  [Events](events.md#subscribing-and-publishing).

## Fields

`Behavior<Derived>` generates `typeName()`, `visitFields()` and `clone()` from the reflect
block, so the block is required even with no fields (missing, the compile error says what to
write). `typeName()` is the class name without its namespace (`Reflect::Traits<T>::NAME`).
The reflected fields drive the inspector, serialization, duplication and hot reload alike.

### Where your types live

**In your own namespace, never in `Vkm::Engine`**, so a project type cannot clash in C++
with an engine type of the same name. The namespace does not reach `BehaviorRegistry`, which
keys on the class name alone: two behaviors of one name share a key, whatever their
namespaces, and the later registration overwrites the earlier with a warning. Write
`using namespace Vkm::Engine;` once inside your namespace, as above. Module entry points are
`extern "C"` at global scope, so they qualify names or open with a function-scope
`using namespace Vkm::Engine;` (`examples/the_lab/src/module.cpp`).

### Field types

A field may be a `float`, `int`, `bool`, `std::string`, a glm vector or quaternion, a
`VKM_ENUM_NAMES` enum, an `AssetRef<Asset>`, a `PrefabRef`, or a `VKM_REFLECT`-ed struct,
which is descended into; reflecting anything else is a compile error that says so.
`Behavior<T>` says how a field is saved and read back.

### Naming an asset

A field that points at an asset is an `AssetRef<Asset>` (`resource/asset_ref.h`), never a
bare string, as `thud` is above. The type argument is what lists the name in the scene's (or
prefab's) `assets` block, through `BehaviorFieldVisitor::assetField`, and the loader
recreates only what that block lists: the asset a `std::string` field names would be missing
on load.

It holds a *name*; `ResourceManager::find` takes it and answers this session's handle -
nothing resolves it for you but `playSoundAt` and `playSound`, which find it as they play.
An empty name means "none". Only kinds with an `ASSET_TYPE` (`resource/asset_type.h`) can be
referenced: `AssetRef<FontAsset>` is a compile error.

A field that names a prefab is a `PrefabRef` (`io/scene/prefab_ref.h`), its project-relative
path, which `instantiate` and `instantiateShared` take; a scene saves it as the plain string.
A prefab's own file names its assets, so it puts nothing in the scene's block.

## ScriptComponent

`ScriptComponent` (`ecs/component/script/script_component.h`) holds an entity's behaviors, and, as
text, those whose type no module registered (`UnknownBehavior`): written back unread, so a
project whose module never loaded loses nothing, on save either - the scene's assets block keeps
each loaded asset a string in that text names - and the reload that loads a working build turns
them back into behaviors. It is the one move-only component
([architecture.md](architecture.md#known-exceptions)). It saves
under the key `"Script"`, each behavior by its registered name and its reflected fields
([io.md](io.md#componentserializer)).

### Authored fields inside a prefab instance

A behavior's authored values, asset references included, are the prefab's on every instance:
edit them in the prefab (`ScriptComponent` says why).

## Time and pause

Three tick hooks, two timelines. This section is the whole contract for what `dt` means and
when a hook runs.

| Hook | Timeline | Runs | `dt` is |
|------|----------|------|---------|
| `onUpdate(dt)` | simulation | only on frames where simulation time advanced | `getSimDelta()`, always `> 0` |
| `onFixedUpdate(dt)` | simulation | once per fixed step, from an accumulator fed by the sim delta | `getFixedStep()` |
| `onRealtimeUpdate(dt)` | real | every frame, paused or not | `getDeltaTime()`, always `> 0` |

1. **`onUpdate` is simulation time.** Paused, stopped or scaled to zero it **does not run at
   all** - not with a zero delta - so nothing written there ticks while the world is frozen.
2. **`onFixedUpdate` is simulation time too**, so pause and time scale reach it. It runs zero
   or many times per frame while input is sampled once per frame, so `pressed()` and its
   siblings read the tick's command there, never the frame
   ([input.md](input.md#reading-it-on-the-tick)). A behavior that moves anything also asks
   `isSimulated()`; see [networking.md](networking.md#what-a-project-writes).
3. **`onRealtimeUpdate` is real time**: every frame, paused or not, and `setTimeScale()` does
   not reach it. Menu animation, unscaled timers, ducking the music, holding a key to quit
   live here.
4. **"Started" is the play session.** A behavior starts on its first *simulation* tick, never
   in the realtime pass. In the editor, paused is also Edit mode, so a scene merely open in
   the editor runs **nothing at all**.
5. **An entity spawned while paused starts when simulation time next flows.** So build a pause
   menu at `onStart`, as a canvas that pauses the game (`UICanvas::pausesGame`): shown, it holds
   the world and frees the cursor, and its `toggleAction` opens and closes it with no hook
   ([ui.md](ui.md#menus-that-pause-the-game)); [recipes.md](../recipes.md#screens) has one, and
   `examples/the_lab/src/lab_pause_menu.h` a whole menu.
6. **`destroy()` and `loadScene()` drain on the realtime pass as well**, so a pause menu's
   "quit to the main menu" works while frozen. The load takes a pausing canvas with the old
   world, so the new one runs; but nothing in it starts while `clock()` is paused, and the asking
   behavior is destroyed by the load, so one that paused the clock itself resumes it too (the
   engine warns when a load lands on a frozen clock). Ending a
   session - Stop, a scene replace, shutdown - discards whatever its `onDestroy` hooks queued.
7. **Queued contacts are dropped while paused**: physics did not run, so they describe a world
   older than the pause.
8. **`onUpdate` runs before `onRealtimeUpdate`** within a frame, and **both see the same input
   edge**: `pressed()` is a fact about a frame. Put a toggle in one hook - for anything that
   must work while paused, `onRealtimeUpdate`.
9. **A moving body is drawn where the frame falls.** In `onUpdate` its `Transform` holds its
   last tick carried forward by its velocities, so a camera following it moves smoothly. A
   position or rotation `onUpdate` sets on it - a teleport - is kept, and the next tick moves
   it on from there; a nudge added to the carried pose keeps the carry too, so steer a body in
   `onFixedUpdate` ([physics.md](physics.md#drawn-between-ticks)).
10. **`clock()` is the Clock**, so a game can pause itself and, still getting realtime ticks,
    resume. Call `setPaused()` / `setTimeScale()` / `requestStep()`; never `beginFrame()` or
    `consumeFixedStep()`, which belong to the main loop. Ending the session returns the clock
    to Edit mode - paused, at 1x. A step asked while the engine holds a world - waiting on a
    mesh its colliders or water need, or under the renderer's first programs - runs on the
    frame that lets it go, with `onUpdate` and its contacts (`Clock::setHeld`).
11. **The world runs under the splash and the loading screen** once the meshes its colliders
    and water need are in, so it is already moving when they lift ([ui.md](ui.md#the-loading-screen)). A timer the player must
    see counts only while `isCovered()` answers false: a round's clock, a banner's seconds.

**Not supported:** per-entity or per-layer time scales, a nested pause stack, pausing
individual systems, and running behaviors in Edit mode. `EventBus::flush` is unconditional,
so a paused game still *receives* a UI click and answers it in `onRealtimeUpdate`.

## The gameplay module

The engine ships no gameplay: **the project brings its code**, built into `game.dll` /
`libgame.so` in its own `bin/` - the one place a host looks; an engine tree but `build/` keeps
the examples' beside its hosts ([building.md](building.md#build-commands)). Every host loads it the same way
through `ScriptModule`: `vkm_runtime` to play, `vkm_server` to serve, `vkm_editor` to edit, so
the shipped and edited game run the same binary.

The host `dlopen`s the module and calls the entry points it finds, all declared in
`system/script/module/module_entry.h`. `VKM_GAME_MODULE()` defines the two required; a definition of
the third is marked `VKM_MODULE_ENTRY` (`extern "C"`, plus the `__declspec(dllexport)` a
Windows DLL needs):

| Entry | Signature | Required? | Purpose |
|-------|-----------|-----------|---------|
| `vkmModuleEngineVersion` | `const char* ()` | Yes | Reports the engine the module was built against; the host refuses a mismatch |
| `vkmRegisterBehaviors` | `void ()` | Yes | Registers every behavior a `VKM_BEHAVIOR` block lists into the engine's `BehaviorRegistry` |
| `vkmSetupNetwork` | `void (NetSession&)` | Optional | Says what a joining player is given; a project without it runs offline |

- **Include `module_entry.h`.** The host looks each symbol up by name and casts it to the
  type above, so a wrong signature would load and read garbage; the header's declarations
  make it a compile error.
- **The version guard is an ABI guard.** The engine is not ABI-stable between versions, so
  `ScriptModule` refuses a module built against another version, or reporting none, and says
  which to rebuild against.
- **The module links `vkm_core`** (`vkm_add_gameplay_module()` does it), a shared library, so
  module and host share one typeId registry and one set of singletons
  ([building.md](building.md#cmake-targets)). A behavior's `LOG_*` lines land in the
  project's log beside the engine's.

### Hot reload

`ScriptModule` loads a **copy** of the module (`DynamicLibrary::loadCopy`), so a rebuild may
overwrite the original while the editor holds it; from a folder that takes no copy, an installed
game's, it loads the module in place. `ScriptModule::reload` checks the new build before it
touches the running one; its doc says what it refuses and in what order it then swaps. Every behavior is
recreated from its reflected fields, so `onStart` runs again.

**The editor reloads a new build on its own**
([editor.md](editor.md#building-from-the-editor)). **Reloading during play restarts the
session**: Stop (restoring the authored scene), swap, Play.

- **Assets made in `onStart` need no guard**: `resources().add` replaces what stands under the
  name. `findOrAdd` keeps it, so what it made before a reload is the old code's. See
  [Resources](resources.md).
- **A module's statics do not survive a reload.** Under GCC every module is compiled with
  `-fno-gnu-unique` (`vkm_gameplay_module_options` in `cmake/gameplay_module.cmake`), without
  which GCC's unique symbols would bind the fresh copy to the old one's. State that must
  survive belongs in reflected fields, a component or an asset - never a `static`.
- **A failed reload goes through `reportError`**, so the Console's Errors names it, and the
  running module stays.

---

## How it works inside

### BehaviorSystem

`BehaviorSystem::update` runs the simulation pass only when simulation time advanced (start
new instances, `onUpdate`, then the queued physics contacts), then `onRealtimeUpdate` on
behaviors already started, then drains the deferred `destroy()` and `loadScene()` requests -
after both passes, so a self-destroy cannot free the `ScriptComponent` being iterated.
`onDestroy` fires on entity deletion (a `Scene` observer) and from `endSession()` - Stop, a
scene replaced, a hot reload, shutdown - which also empties both queues, because an entity id
re-aims at whatever takes its slot after a scene swap.

Hooks and `subscribe()` listeners run under a catch net that sends a throw to
`reportError()`; a hook that throws also disables its behavior, a listener's does not. The
accessors read the session's `FrameContext` and queue requests through a `BehaviorContext`
the system owns and binds before `onStart()`; growing the surface is an accessor beside the
others.

### Unloading a module

Nothing may hold module code across the `dlclose`. `ScriptModule::releaseRegistrations`
drops the behavior factories, the wire schema's thunks, the session's spawn callbacks and
every idle event bus; `BehaviorSystem::endSession` first drops each behavior's
`subscribe()`s (see [Events](events.md)). Storage can carry module code too - a
`SparseSet<T>` carries the vtable of whichever binary made it - so:

- **`Scene::clear` destroys the component sets rather than emptying them**, and the editor
  puts the scene through its serializer before a reload, so the sets a load makes are the
  engine's. A component type only the module declares has no row in the scene format, so it
  does not survive that trip - as it does not survive a Stop.
- **`ResourceManager` makes every engine asset type's slot when it is constructed or
  cleared**, in vkm_core, so assets an `onStart` adds land in storage the engine made.
- **A clip's samples are built in vkm_core** ([audio.md](audio.md#the-clip-asset)), and the
  editor stops every voice before it unmaps a module.

## Key files

- `src/engine/system/script/behavior_base.h` - `BehaviorBase`: the hooks and verbs every behavior has
- `src/engine/system/script/behavior.h` - `Behavior<T>` and `VKM_BEHAVIOR`
- `src/engine/system/script/behavior_api.h` - the one include a behavior needs
- `src/engine/system/script/behavior_field_visitor.h` - the field visitor the inspector and serializer share
- `src/engine/ecs/component/script/script_component.h` - `ScriptComponent` and `UnknownBehavior`
- `src/engine/system/script/script_component.h` - `addBehavior` and `removeBehavior`
- `src/engine/system/script/scene_script.cpp` - the scene format's Script row
- `src/engine/system/script/behavior_registry.h` - name -> factory registry
- `src/engine/system/script/behavior_system.h/.cpp` - `BehaviorSystem`, the driver
- `src/engine/system/script/module/module_entry.h` - the module entry points, `VKM_MODULE_ENTRY` and
  `VKM_GAME_MODULE`
- `src/engine/system/script/module/script_module.h/.cpp` - `ScriptModule`, loading and reloading the module
- `modules/vkmPlatform/src/vkm_platform/process/dynamic_library.h/.cpp` - the `.dll` / `.so` loader, and its copy
- `examples/<project>/src/` - a project's own behaviors and its `module.cpp`
