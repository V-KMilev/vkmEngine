# Scripting (Behaviors)

Native C++ gameplay logic. A `Behavior` is the engine's MonoBehaviour /
ActorComponent analogue: subclass it, override lifecycle hooks, and attach
instances to an entity through a `ScriptComponent`. Behaviors live in the
project's gameplay module, which the editor hot-reloads without restarting.

`BehaviorSystem` runs in `SystemStage::Simulation`, **before** `AnimationSystem`
and `PhysicsSystem`, which advance only on the fixed tick. A frame runs its ticks
first and its frame pass after, so what `onFixedUpdate` writes is integrated
that same tick, and what `onUpdate` writes waits for the next tick, on a later
frame.

## Your first behavior

A behavior is a C++ class in your project's `src/`, built into the project's
module by `vkm build`. Include `system/script/behavior_api.h`: it brings the
base class, the reflect block, the body components, input, the physics and
audio events, the asset graph, the log (`LOG_INFO`, `LOG_WARNING`,
`LOG_ERROR`) and GLFW's key codes - through `platform/window/glfw_include.h`,
never `<GLFW/glfw3.h>`, which would take the GL declarations from the wrong
header. Anything else is included by its path under `src/engine/`.

**The class and its fields.** Derive from `ReflectedBehavior<YourClass>`, put
the values a designer tunes in a public block, and list them in a reflect block
below the class - one `VKM_F` per field, no commas. The class name is the
behavior's name: it is what a scene file stores and what the editor's Add
Behavior menu lists.

```cpp
// src/crate.h
#pragma once

#include "system/script/behavior_api.h"

namespace Game {

using namespace Vkm::Engine;

class Crate : public ReflectedBehavior<Crate> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;
        void onCollisionEnter(const Collision& hit) override;

    public:
        float                    pushStrength = 6.0f;
        glm::vec4                glowColor    = {1.0f, 0.6f, 0.2f, 1.0f};
        AssetRef<AudioClipAsset> thud;

    private:
        AudioClipHandle m_thud;
};

} // namespace Game

VKM_REFLECT_BEGIN(::Game::Crate)
    VKM_F(pushStrength)
    VKM_F(glowColor)
    VKM_F(thud)
VKM_REFLECT_END()
```

A field the block does not list is runtime state: not saved, not shown, not
copied. A behavior with nothing to tune still writes the block, empty. The
inspector labels `pushStrength` "Push Strength", and edits a `glm::vec3` or
`glm::vec4` whose name ends in `color` with a colour picker.

**Registering it.** The module names its behaviors once, in `src/module.cpp`:

```cpp
#include "crate.h"

VKM_MODULE_ENTRY
void vkmRegisterBehaviors() {
    Vkm::Engine::BehaviorRegistry::get().registerBehaviors<Game::Spinner, Game::Crate>();
}
```

A behavior left out of that list cannot be loaded from a scene: it is held as
text, and the log says so.

**Attaching it.** In the editor, select an entity and pick the behavior from
Add Behavior; its fields appear on the card. In code, `addBehavior` gives the
entity its `ScriptComponent` if it has none and returns the new behavior:

```cpp
Game::Crate& crate = addBehavior<Game::Crate>(scene, entity);
crate.pushStrength = 9.0f;
```

Added during play it starts on the next simulation tick, as a loaded one does.

**Reading input.** Name an action once, in `onStart`, and ask for it by name
([input.md](input.md) has axes, and why a fixed update reads `command()`
instead):

```cpp
// src/crate.cpp
#define VKM_LOG_CATEGORY "GAME"

#include "crate.h"

void Game::Crate::onStart() {
    input().define("Push", { {InputSource::Key, GLFW_KEY_E, 1.0f} });
    m_thud = resources().find(thud);
    LOG_INFO("Crate %u ready", entity().slot());
}

void Game::Crate::onUpdate(float dt) {
    Rigidbody* body = tryGet<Rigidbody>();
    if (body && input().pressed("Push")) body->linearVelocity += Math::WORLD_FORWARD * pushStrength;
}
```

**Collisions.** A body is a `Collider` and a `Rigidbody` together; a Collider
without a Rigidbody touches nothing and no hook hears it. Static ground takes a
Rigidbody whose `motion` is `RigidbodyMotion::Static`; something a behavior
moves by hand takes `Kinematic`. `onCollisionEnter` / `Stay` / `Exit` are handed
a `Collision`: the other entity, a contact point, a normal pointing from the
other entity into this one (a crate landing on the floor hears it pointing up),
and the speed they struck at. On exit the point, normal and speed are zero. A
trigger collider calls `onTriggerEnter` / `Stay` / `Exit` with the entity that
entered instead.

**A sound.** A one-shot that needs no entity of its own is an event; the handle
is the one `onStart` resolved from the authored `thud`:

```cpp
void Game::Crate::onCollisionEnter(const Collision& hit) {
    events().emit(PlaySoundEvent::at(m_thud, hit.point));
}
```

A sound that must follow something, loop or stop is an `AudioSource` instead
([audio.md](audio.md)).

**Another behavior.** `findBehavior<T>(entity)` answers the `T` on an entity,
or null; `findBehavior<T>()` asks this entity:

```cpp
if (Health* health = findBehavior<Health>(hit.other)) health->damage(10.0f);
```

Ask each time rather than keeping the pointer: the other entity can be
destroyed between two hooks. A behavior the game has one of - a director, a
menu - is the exception: `findInScene<T>()` walks every scripted entity for the
scene's one (the lowest slot's, the same on every machine), so look it up once
in `onStart` and keep it while its entity lives.
`findEntityWithBehavior<T>(scene())` answers the entity carrying it.

**Listening for an event.** `subscribe` reads the event type off the lambda,
and drops the listener when the behavior goes:

```cpp
subscribe([this](const UIClickEvent& click) { if (click.eventId == "crate:reset") reset(); });
```

Before writing a second behavior, read [Time and pause](#time-and-pause): it
says when each hook runs.

## Behavior

```cpp
class Behavior {
    public:
        virtual void onStart()                        {}  // first SIMULATION tick in play mode
        virtual void onUpdate(float dt)               {}  // variable step; dt = simDelta, > 0
        virtual void onRealtimeUpdate(float dt)       {}  // every frame, paused or not; dt = real delta, > 0
        virtual void onFixedUpdate(float dt)          {}  // fixed step; dt = fixedStep
        virtual void onCollisionEnter(const Collision& hit) {}  // a non-trigger contact began
        virtual void onCollisionStay(const Collision& hit)  {}  // ... lasted another tick, unless both rest
        virtual void onCollisionExit(const Collision& hit)  {}  // ... ended; hit.other may be dead
        virtual void onTriggerEnter(EntityId other)   {}  // other entered this trigger
        virtual void onTriggerStay(EntityId other)    {}  // ... is still inside, on the same terms
        virtual void onTriggerExit(EntityId other)    {}  // ... left it, or is gone
        virtual void onDestroy()                      {}  // teardown

        virtual const char*               typeName() const = 0;   // == BehaviorRegistry key, the class name
        virtual void                      visitFields(BehaviorFieldVisitor&) {}
        virtual std::unique_ptr<Behavior> clone() const = 0;      // deep copy for duplication

        EntityId entity() const;                    // this entity, to pass to anything taking one

    protected:
        // What this entity carries.
        template<typename T> T*    tryGet();        // its T, or null - reach for this one
        template<typename T> T&    get();           // its T, which it must have
        template<typename T> bool  has() const;     // does it carry one
        template<typename T> auto& add(T && component); // give it one

        Scene&           scene();       // the world
        ResourceManager& resources();   // the assets
        EventSender      events();      // emit or enqueue; subscribe() to listen
        InputMap&        input();       // named actions, frame and tick alike
        NetSession&      net();         // the wire, or an offline stand-in
        Clock&           clock();       // time, and the play state behind it
        WindowManager*   window();      // null on a host that draws nothing
        RenderSettings&  render();      // the quality the frame is drawn at
        const RenderSettings& projectRender();  // project.json's, a quality tier's base (rendering.md)
        ScreenView       screen();      // the 3D view: a ray through a point, a point on a canvas
        std::optional<Math::Ray> pointerRay(); // the ray under the cursor, if it is on the view
        const OceanData* ocean();       // the scene's sea, this tick's or frame's, or null

        bool                isSimulated();  // does this end decide this entity
        bool                isMine();       // is this the player sitting here
        bool                isReplaying();  // is this tick being run again
        const InputCommand& command();      // the input driving this entity, this tick

        EntityId spawn();                               // an empty entity
        EntityId spawn(const char* name);               // ... carrying a Name
        EntityId spawn(const char* name, EntityId parent);  // ... under a parent
        void     destroy();             // this entity and its subtree, after the hook pass
        void     destroy(EntityId);     // that one and its subtree, same deferral
        void     loadScene(const std::string& scenePath);  // deferred to the end of the frame's hooks
        void     restartScene();        // the scene being played, over again; same deferral
        template<typename T> T* findBehavior();          // its behavior of type T, or null
        template<typename T> T* findBehavior(EntityId);  // another entity's
        template<typename T> T* findInScene();           // the scene's one T
        template<typename E = void, typename Fn> void subscribe(Fn&&);  // E read off Fn; auto-unsubscribes
};
```

- **Components.** Reach for `tryGet<T>()`; `get<T>()` is for a component the
  entity cannot run without, such as one its own `onStart` added. The four
  accessors are `scene()` calls with this entity filled in; for another entity
  call them on `scene()`. `Scene::tryGet` takes a null id, and every `find` in
  the engine answers null when there is nothing to find, so
  `scene().tryGet<Transform>(findActiveCamera(scene()))` is safe.
- **The accessors** from `scene()` to `ocean()` read one context that outlives
  every frame: safe from a `subscribe()` callback as well as a hook, and not
  worth caching in a member.
- **Networking.** `isSimulated()`, `isMine()`, `isReplaying()` and `command()`
  read the session through `net()`. Offline they answer the single-player way
  (`isSimulated()` and `isMine()` yes, `isReplaying()` no, `command()` the local
  player's), so a project is written the same with or without a session; see
  [networking.md](networking.md#what-a-project-writes).
- **The screen.** `window()` is null on a dedicated server. To aim the cursor
  into the world or pin a label over something, ask `screen()`, not the window:
  in the editor's viewport the scene is not drawn over the whole window
  ([ui.md](ui.md#the-world-on-the-screen)).
- **`destroy()`** takes the entity's whole subtree and is deferred to after the
  current hook pass, so a behavior may destroy its own entity. **`loadScene()`**
  is deferred to the end of the frame's hooks, so a behavior may ask for the
  scene that destroys it; the path is project-relative. **`restartScene()`**
  starts the scene being played over: the file `loadScene` last loaded, else
  what the host began play with - a runtime's or server's entry scene
  (`BehaviorSystem::setPlayedScene`), or under the editor the scene as Play
  found it, which the editor puts back by Stop and Play
  (`BehaviorSystem::setRestartHandler`).
- **Spawning.** `spawn(name, parent)` parents through
  `HierarchyOperations::setParent`, UI elements included. A spawn is local to
  this end; what every end should see is `NetSession::spawn`, on the server
  ([networking.md](networking.md#spawning)). A prefab is
  `Prefab::instantiate(scene(), resources(), "prefabs/enemy.json")`
  (`io/scene/prefab.h`), which returns the subtree's root.
- **`subscribe()`** is the only way a behavior listens; see
  [Events](events.md#subscribing-and-publishing).

## Fields

`ReflectedBehavior<Derived>` generates `typeName()`, `visitFields()` and
`clone()` from the reflect block, so the block is required even with no fields
(missing, the compile error says what to write). `typeName()` is the class name
without its namespace (`Reflect::Traits<T>::NAME`). The reflected fields drive
the inspector, serialization, duplication and hot reload alike.

### Where your types live

**In your own namespace, never in `Vkm::Engine`**, so a project type cannot
clash in C++ with an engine type of the same name. The namespace does not reach
`BehaviorRegistry`, which keys on the class name alone: two behaviors of one
name share a key, whatever their namespaces, and the later registration
overwrites the earlier with a warning. Write
`using namespace Vkm::Engine;` once inside your namespace, as above. Module
entry points are `extern "C"` at global scope, so they qualify names or open
with a function-scope `using namespace Vkm::Engine;`
(`examples/physics_lab/src/module.cpp`).

### Field types

`BehaviorFieldVisitor` lets the inspector and serializer read and write a
behavior's fields through a `Behavior*`. It supports `float`, `int`, `bool`,
`std::string`, `glm::vec2`, `glm::vec3`, `glm::vec4` and `glm::quat`, plus any
`VKM_ENUM_NAMES` enum, any `AssetRef<Asset>`, and any `VKM_REFLECT`-ed struct
(descended into). Reflecting anything else is a compile error.

A field's name is the key it is saved under. A `glm::quat` edits as Euler angles
in degrees and saves as the quaternion. A missing key keeps the field's current
value; a key holding the wrong type keeps it with a warning.

### Naming an asset

A field that points at an asset is an `AssetRef<Asset>`
(`resource/asset_ref.h`), never a bare string, as `thud` is above. The type
argument is what lists the name in the scene's (or prefab's) `assets` block,
through `BehaviorFieldVisitor::assetField`, and the loader recreates only what
that block lists: the asset a `std::string` field names would be missing on load.

It holds a *name*; `ResourceManager::find` takes it and answers this session's
handle - nothing resolves it for you. An empty name means "none". The inspector
shows a combo over the project library's assets of that kind, plus `(none)`,
and flags a stored name the library does not have. Only kinds with an
`ASSET_TYPE` (`resource/asset_type.h`) can be referenced: `AssetRef<FontAsset>`
is a compile error.

## ScriptComponent

`ScriptComponent` (`system/script/script_component.h`) holds an entity's
behaviors, and as text those whose type no module registered.

The one ECS component that is not a plain aggregate: it is move-only, and
duplication deep-copies through `Behavior::clone()`. See [ecs.md](ecs.md).

### Authored fields inside a prefab instance

A behavior's authored values, asset references included, are the prefab's on
every instance: edit them in the prefab. The component serializes as one field
holding the whole behavior list, and a prefab override cannot address inside a
list, so none is recorded and a hand-written `"Script"` override is reported and
ignored.

## Time and pause

Three tick hooks, two timelines. This section is the whole contract for what
`dt` means and when a hook runs.

| Hook | Timeline | Runs | `dt` is |
|------|----------|------|---------|
| `onUpdate(dt)` | simulation | only on frames where simulation time advanced | `getSimDelta()`, always `> 0` |
| `onFixedUpdate(dt)` | simulation | once per fixed step, from an accumulator fed by the sim delta | `getFixedStep()` |
| `onRealtimeUpdate(dt)` | real | every frame, paused or not | `getDeltaTime()`, always `> 0` |

1. **`onUpdate` is simulation time.** Paused, stopped or scaled to zero it
   **does not run at all** - not with a zero delta - so nothing written there
   ticks while the world is frozen.
2. **`onFixedUpdate` is simulation time too**, so pause and time scale reach it.
   It runs zero or many times per frame while input is sampled once per frame,
   so a fixed update reads `command()`, never the frame queries
   ([input.md](input.md#reading-it-on-the-tick)). A behavior that moves
   anything also asks `isSimulated()`; see
   [networking.md](networking.md#what-a-project-writes).
3. **`onRealtimeUpdate` is real time**: every frame, paused or not, and
   `setTimeScale()` does not reach it. Menu animation, unscaled timers, ducking
   the music, holding a key to quit live here.
4. **"Started" is the play session.** A behavior starts on its first
   *simulation* tick, never in the realtime pass. In the editor, paused is also
   Edit mode, so a scene merely open in the editor runs **nothing at all**.
5. **An entity spawned while paused starts when simulation time next flows.** So
   build a pause menu at `onStart` and show it by toggling `UICanvas::visible`
   (or `UIElement::visible`).
6. **`destroy()` and `loadScene()` drain on the realtime pass as well**, so a
   pause menu's "quit to the main menu" works while frozen. But nothing in the
   new scene starts until simulation time flows, and the asking behavior is
   destroyed by the load, so quit-to-menu resumes the clock too (the engine
   warns when a load lands on a frozen clock). Ending a session - Stop, a scene
   replace, shutdown - discards whatever its `onDestroy` hooks queued.
7. **Queued contacts are dropped while paused**: physics did not run, so they
   describe a world older than the pause.
8. **`onUpdate` runs before `onRealtimeUpdate`** within a frame, and **both see
   the same input edge**: `pressed()` is a fact about a frame. Put a toggle in
   one hook - for anything that must work while paused, `onRealtimeUpdate`.
9. **`clock()` is the Clock**, so a game can pause itself and, still getting
   realtime ticks, resume. Call `setPaused()` / `setTimeScale()` /
   `requestStep()`; never `beginFrame()` or `consumeFixedStep()`, which belong to
   the main loop. Ending the session returns the clock to Edit mode - paused,
   at 1x.

```cpp
void PauseMenu::onStart() {
    m_root = buildMenuEntities();   // built once, while time is running
    show(false);
}

void PauseMenu::onUpdate(float dt) {
    // Only reached while the world runs - the only state a pause menu opens from.
    if (input().pressed("Cancel")) {
        show(true);
        clock().setPaused(true);
    }
}

void PauseMenu::onRealtimeUpdate(float dt) {
    // Still ticking with the world frozen: the panel animates, and the pause can be undone.
    m_fade = std::min(1.0f, m_fade + dt * 4.0f);
    if (m_resumeClicked) {
        m_resumeClicked = false;
        show(false);
        clock().setPaused(false);
    }
    if (m_quitClicked) {
        // The loaded scene's behaviors start on a simulation tick; this one dies in the load.
        clock().setPaused(false);
        loadScene("scenes/main_menu.json");                  // drains while paused
    }
}

void PauseMenu::show(bool on) {
    if (UIElement* panel = scene().tryGet<UIElement>(m_root)) panel->visible = on;
}
```

**Not supported:** per-entity or per-layer time scales, a nested pause stack,
pausing individual systems, and running behaviors in Edit mode. `EventBus::flush`
is unconditional, so a paused game still *receives* a UI click and answers it in
`onRealtimeUpdate`.

## The gameplay module

The engine ships no gameplay: **the project brings its code**, built into
`game.dll` / `libgame.so` in its own `bin/` - the one place a host looks. Every
host loads it the same way through `ScriptModule`: `vkm_runtime` to play,
`vkm_server` to serve, `vkm_editor` to edit, so the shipped and edited game run
the same binary.

The host `dlopen`s the module and calls the entry points it finds, all declared
in `system/script/module_entry.h`. Include it and mark each definition with
`VKM_MODULE_ENTRY` (`extern "C"`, plus the `__declspec(dllexport)` a Windows DLL
needs):

| Entry | Signature | Required? | Purpose |
|-------|-----------|-----------|---------|
| `vkmModuleEngineVersion` | `const char* ()` | Yes | Reports the engine the module was built against; the host refuses a mismatch |
| `vkmRegisterBehaviors` | `void ()` | Yes | Registers the project's behavior types into the engine's `BehaviorRegistry` |
| `vkmBuildScene` | `void (Scene&, ResourceManager&)` | Optional | Builds the project's world in code, instead of `entryScene`; it gets the asset graph to make the meshes and materials that world needs |
| `vkmSetupNetwork` | `void (NetSession&)` | Optional | Says what a joining player is given; a project without it runs offline |

- **Include `module_entry.h`.** The host looks each symbol up by name and casts
  it to the type above, so a wrong signature would load and read garbage; the
  header's declarations make it a compile error.
- **The version guard is an ABI guard.** The engine is not ABI-stable between
  versions, so `ScriptModule` refuses a module built against another version, or
  reporting none, and says which to rebuild against.
- **The module links `vkm_core`** (`vkm_add_gameplay_module()` does it), a
  shared library, so module and host share one typeId registry and one set of
  singletons ([Building](building.md)). A behavior's `LOG_*` lines land in the
  project's log beside the engine's.

`BehaviorRegistry` is the process-wide name -> factory map scenes recreate
behaviors through: `registerBehaviors<A, B, C>()` keys each type `T` on
`Reflect::Traits<T>::NAME`, `create("Crate")` makes one, and `names()` is what
Add Behavior lists.

### Hot reload

`ScriptModule` loads a **copy** of the module (`libgame.loaded.<n>.so`,
`game.loaded.<n>.dll`), so a rebuild may overwrite the original while the
editor holds it; a directory that refuses writes loads the original in place.

`ScriptModule::reload(scene, behaviors, events)` first opens the new build
beside the running one and refuses one that is not a library, reports another
engine version or lacks `vkmRegisterBehaviors`, leaving the running module as
it was. Then it serializes each entity's behaviors (type + reflected fields),
runs their `onDestroy`, unloads the old module, registers the new one, and
recreates the behaviors. They start fresh: `onStart` runs again.

**The editor reloads a new build on its own**
([editor.md](editor.md#building-from-the-editor)). **Reloading during play
restarts the session**: Stop (restoring the authored scene), swap, Play.

- **Assets made in `onStart` need no guard**: `resources().add` replaces what
  stands under the name. `findOrAdd` keeps it, so what it made before a reload
  is the old code's. See [Resources](resources.md).
- **A module's statics do not survive a reload.** Under GCC every module is
  compiled with `-fno-gnu-unique` (`vkm_gameplay_module_options` in
  `cmake/gameplay_module.cmake`), without which GCC's unique symbols would bind
  the fresh copy to the old one's. State that must survive belongs in reflected
  fields, a component or an asset - never a `static`.
- **A failed reload goes through `reportError`**, so the Errors panel names it.
  A project whose module never loaded has its behaviors held as text: nothing is
  lost, including on save, and the reload that loads a working build turns them
  back into behaviors.

---

## How it works inside

### BehaviorSystem

`BehaviorSystem::update` runs the simulation pass only when `getSimDelta() > 0`
(start new instances, `onUpdate`, then the queued physics contacts, which it
drops otherwise), then `onRealtimeUpdate` on already-started behaviors, then
drains the deferred `destroy()` and `loadScene()` requests - after both passes,
so a self-destroy cannot free the `ScriptComponent` being iterated.
`fixedUpdate` ticks `onFixedUpdate` and drains `destroy()`s after each tick.
`onDestroy` fires on entity deletion (a `Scene` observer) and from
`endSession()` - Stop, a scene replaced, a hot reload, shutdown - which also
empties both queues, because an entity id re-aims at whatever takes its slot
after a scene swap.

Hooks and `subscribe()` listeners run under a catch net that sends a throw to
`reportError()`; a hook that throws also disables its behavior. The accessors read a
`BehaviorContext` the system owns and binds before `onStart()`; growing the
surface is a field there and an accessor beside the others.

### Unloading a module

Nothing may hold module code across the `dlclose`.
`ScriptModule::releaseRegistrations` drops the behavior factories, the wire
schema's thunks, the session's spawn callbacks and every idle event bus;
`BehaviorSystem::endSession` first drops each behavior's `subscribe()`s (see
[Events](events.md)). Storage can carry module code too - a `SparseSet<T>`
carries the vtable of whichever binary made it - so:

- **`Scene::clear` destroys the component sets rather than emptying them**, and
  the editor puts the scene through its serializer before a reload, so the sets
  a load makes are the engine's. A component type only the module declares has
  no row in the scene format, so it does not survive that trip - as it does not
  survive a Stop.
- **`ResourceManager` makes every engine asset type's slot when it is
  constructed or cleared**, in vkm_core, so assets a `vkmBuildScene` or an
  `onStart` adds land in storage the engine made.
- **A clip's samples are built in vkm_core**
  ([audio.md](audio.md#the-clip-asset)), and the editor stops every voice
  before it unmaps a module.

### Serialization

`ScriptComponent` saves under the key `"Script"`: each behavior as its
registered type name plus a `properties` object of every reflected field, walked
through `visitFields` - the same walk the inspector and a hot reload use. On
load `BehaviorRegistry` recreates the instance by name and the reader fills the
fields, keeping the constructed default where the file has none. An unknown type
is kept as an `UnknownBehavior` and written back unread. See [io.md](io.md).

### Key files

- `src/engine/system/script/behavior.h` - `Behavior` base + lifecycle hooks
- `src/engine/system/script/behavior_api.h` - the one include a behavior needs
- `src/engine/system/script/reflected_behavior.h` - CRTP base that generates the boilerplate from reflected fields
- `src/engine/system/script/behavior_field_visitor.h` - type-erased field visitor (editor + serializer bridge)
- `src/engine/system/script/script_component.h` - `ScriptComponent` and `addBehavior`
- `src/engine/system/script/behavior_registry.h` - name -> factory registry
- `src/engine/system/script/behavior_system.h/.cpp` - `BehaviorSystem` (the driver)
- `src/engine/system/script/module_entry.h` - the module entry points and `VKM_MODULE_ENTRY`
- `src/engine/system/script/script_module.h/.cpp` - `ScriptModule` (hot-reload of the gameplay DLL)
- `src/engine/platform/library/dynamic_library.h/.cpp` - the `.dll` / `.so` loader
- `examples/<project>/src/` - a project's own behaviors + its `vkmRegisterBehaviors` / `vkmBuildScene` entry points
