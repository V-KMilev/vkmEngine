# Scripting (Behaviors)

Native C++ gameplay logic. A `Behavior` is the engine's MonoBehaviour /
ActorComponent analogue: subclass it, override lifecycle hooks, and attach
instances to an entity through a `ScriptComponent`. `BehaviorSystem` drives the
hooks during play; behaviors live in a separate gameplay module that the editor
can hot-reload without restarting.

`BehaviorSystem` runs in `SystemStage::Simulation`, **before** `AnimationSystem`
and `PhysicsSystem`, so a behavior can set state the same frame those integrate
it (events -> gameplay -> animation -> physics). It opts into `fixedUpdate`.

## Key files

- `src/engine/system/script/behavior.h` - `Behavior` base + lifecycle hooks
- `src/engine/system/script/reflected_behavior.h` - CRTP base that generates the boilerplate from reflected fields
- `src/engine/system/script/behavior_field_visitor.h` - type-erased field visitor (editor + serializer bridge)
- `src/engine/system/script/script_component.h` - `ScriptComponent` (the ECS component holding the behaviors)
- `src/engine/system/script/behavior_registry.h` - name -> factory registry
- `src/engine/system/script/behavior_system.h/.cpp` - `BehaviorSystem` (the driver)
- `src/engine/system/script/script_module.h/.cpp` - `ScriptModule` (hot-reload of the gameplay DLL)
- `src/engine/platform/library/dynamic_library.h/.cpp` - cross-platform `.dll`/`.so`/`.dylib` loader
- `examples/<project>/src/` - a project's own behaviors + its `vkmRegisterBehaviors` / `vkmBuildScene` entry points. The engine ships none of its own

## Behavior

```cpp
class Behavior {
    public:
        virtual void onStart()                   {}  // first SIMULATION tick in play mode
        virtual void onUpdate(float dt)          {}  // variable step; dt = simDelta, > 0
        virtual void onRealtimeUpdate(float dt)  {}  // every frame, paused or not; dt = real delta, > 0
        virtual void onFixedUpdate(float dt)     {}  // fixed step; dt = fixedStep
        virtual void onCollision(EntityId other) {}  // non-trigger contact this tick
        virtual void onTrigger(EntityId other)   {}  // trigger overlap this tick
        virtual void onDestroy()                 {}  // teardown

        virtual const char*               typeName() const = 0;   // == BehaviorRegistry key
        virtual void                      visitFields(BehaviorFieldVisitor&) {}
        virtual std::unique_ptr<Behavior> clone() const = 0;      // deep copy for duplication

    protected:
        BehaviorContext& context();     // scene / resources / window / events / input / clock
        EntityId spawn();               // create a new entity
        void     destroy(EntityId);     // deferred until after the hook pass
        void     loadScene(const std::string& scenePath);  // deferred until the tick ends
        template<typename E> void subscribe(std::function<void(const E&)>);  // auto-unsubscribes

        EntityId m_entity;              // the entity this behavior is attached to
};

// The gameplay capability surface, owned by the BehaviorSystem and stable for
// the whole session (a field belongs here exactly when behaviors may use it).
struct BehaviorContext {
    Scene*                 scene;
    ResourceManager*       resources;
    WindowManager*         window;
    EventBus*              events;
    InputMap*              input;
    Clock*                 clock;
    std::vector<EntityId>* pendingDestroy;
    std::string*           pendingSceneLoad;
};
```

`Behavior` is **non-copyable and non-movable** - instances are owned by
`unique_ptr` inside the `ScriptComponent`. `BehaviorSystem` binds its
session-stable `BehaviorContext` (`bindContext`) before `onStart()`, so hooks
reach the engine through one pointer - and because the context outlives every
frame (unlike `FrameContext`), `subscribe()` callbacks may use `context()`
safely too. Growing the capability surface is one field on `BehaviorContext`;
`bindContext` never changes.

- `spawn()` / `destroy()` are the safe structural-edit helpers. `destroy()` is
  deferred to after the current hook pass, so a behavior may destroy its own
  entity from a hook.
- `loadScene()` requests a scene transition. Like `destroy()` it only records the
  request; `BehaviorSystem::update` drains it at the end of the tick, swapping the
  request out before loading. A behavior may therefore ask for the scene that
  will destroy it, from one of its own hooks. The path is project-relative.
- `subscribe<E>()` registers an `EventBus` listener bound to the behavior's
  lifetime - it auto-unsubscribes on destroy, so there is nothing to clean up by
  hand (a raw subscribe on the bus would dangle once the instance dies).

Which hook runs when, and what its `dt` means, is
[Time and pause](#time-and-pause) below - read it before writing the first one.

### ReflectedBehavior - the no-boilerplate path

Most behaviors derive from `ReflectedBehavior<Derived>` (CRTP) instead of
`Behavior` directly. Declare the tunable fields once with the `VKM_REFLECT`
markup and `typeName()`, `visitFields()`, and `clone()` are all generated from
them; you only override the lifecycle hooks. The reflected fields are the
single source of authoring state - they drive the inspector, serialization, and
duplication uniformly.

```cpp
namespace Vkm::Engine {

class CubeSpinner : public ReflectedBehavior<CubeSpinner> {
    public:
        static constexpr const char* TYPE_NAME = "CubeSpinner";
        void onUpdate(float dt) override;
        float degreesPerSecond = 90.0f;   // authored, reflected
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::CubeSpinner)
    VKM_F(degreesPerSecond)
VKM_REFLECT_END()
```

`BehaviorFieldVisitor` is the type-erased bridge that lets code holding only a
`Behavior*` (the inspector, the serializer) read/write a concrete behavior's
fields without knowing its type. The leaf types it supports are `float`, `int`,
`bool`, `glm::vec3` and `std::string`, plus any `VKM_ENUM_NAMES` enum, any
`AssetRef<Asset>`, and any `VKM_REFLECT`-ed struct (descended into). Reflecting
anything else is a compile error that names the ways out: add a `field()`
overload, register the enum or struct, or drop the field.

A `std::string` field is free text - a label, a tag, a bone name. It serializes
as a JSON string and edits as a text box, and like every leaf it keeps its
current value when the key is missing; unlike the numeric leaves it also keeps
it when the key is present but is not a string, because free text is what a
hand-edited scene most often gets wrong and a throw there costs the whole load.

### Naming an asset

A field that points at an asset declares an `AssetRef<Asset>`
(`resource/asset_ref.h`) - never a bare string:

```cpp
class Footsteps : public ReflectedBehavior<Footsteps> {
    public:
        static constexpr const char* TYPE_NAME = "Footsteps";

        void onStart() override {
            m_clip = context().resources->findByName<AudioClipAsset>(step.name);
        }

        AssetRef<AudioClipAsset> step;   // authored, reflected: VKM_F(step)

    private:
        AudioClipHandle m_clip;
};
```

**The type argument is what makes it work, and `std::string step` would not.** A
scene's `assets` block is built by walking what the scene references, and a name
that block never lists is a name the loader never recreates - so `findByName`
would answer null and the sound would never play. `AssetRef` carries the asset
kind through `BehaviorFieldVisitor::assetField`, which is what lets that walk
list the name in the right section. Prefabs get the same treatment: a prefab
writes its own assets block from the same walk, so an instance brings the clip
with it.

It holds a *name* because a name is the engine's serializable identity for an
asset, while a `Handle<T>` names a slot in one session's `ResourceManager`.
Resolving is the behavior's own job, once, as above; nothing resolves it for
you. An empty name means "none".

In the inspector the field is a combo over what the project's asset library
holds of that kind, plus `(none)`. That is deliberately the *library* and not
what is currently loaded: picking a name is what pulls the asset into the
scene's assets block on the next save. A stored name the library does not have -
an asset renamed or deleted under the field, or a scene from another project -
is flagged under the combo rather than left to fail at load. Nothing rewrites
the name for you: it is authored text, not a handle the editor can follow.

Only asset kinds with an `ASSET_TYPE` (`resource/asset_type.h`) can be
referenced; `AssetRef<FontAsset>` is a compile error, because the library holds
no fonts and the assets block has no section to put one in.

## ScriptComponent

```cpp
struct ScriptComponent {
    std::vector<std::unique_ptr<Behavior>> behaviors;
};
```

The one ECS component that is **not** a plain aggregate: it owns `unique_ptr`s,
so it is move-only (the documented exception in the code-style guide). `SparseSet`
stores it through its `std::move` path; per-behavior deep copy for entity
duplication goes through `Behavior::clone()`. See [ecs.md](../ecs.md).

### Authored fields inside a prefab instance

`ScriptComponent` is deliberately absent from `PrefabOverrides::COMPONENT_KEY`,
and **1.7 did not change that.** A behavior's authored values - the asset
references included - are the prefab's, identically, on every instance of it:
edit them in the prefab, not in an instance. The inspector says so on the card
while it is open.

The mechanical reason is that the component serializes as a single field holding
the whole behavior list, so a per-field delta on it would be the whole list.
Making one behavior field overridable is therefore not a change to the reference
encoding; it is an override *address* that reaches inside a list - (uid,
component, behavior index or type, field) - which the prefab format does not
have. Nothing has asked for it yet: the case that wants it (this barrel, that
door) also wants an authored entity reference, which is deferred for the same
reason. Design the two together when one is needed, or the format grows twice.

The loader enforces it, so the rule is the format's and not just the editor's: a
hand-written `"Script"` override is reported as drift and not applied, the way
one on the root's `Transform` is. Both are addresses a file could carry and must
not take effect - the Transform because it could never work, this one because it
would, wholesale and invisibly, leaving content authored against an address the
format has not decided yet.

## Time and pause

Three tick hooks, two timelines. **Which timeline a hook is on is legible in the
hook you are writing**, not in a flag set somewhere else - so this section is the
whole contract, and a behavior never has to read `BehaviorSystem` to learn what
`dt` means.

| Hook | Timeline | Runs | `dt` is |
|------|----------|------|---------|
| `onUpdate(dt)` | simulation | only on frames where simulation time advanced | `getSimDelta()`, always `> 0` |
| `onFixedUpdate(dt)` | simulation | once per fixed step, from an accumulator fed by the sim delta | `getFixedStep()` |
| `onRealtimeUpdate(dt)` | real | every frame, paused or not | `getDeltaTime()`, always `> 0` |

1. **`onUpdate` is simulation time.** While the game is paused, stopped, or
   scaled to zero it **does not run at all**, rather than running with a zero
   delta. That distinction is the point: a per-frame counter, an input edge or a
   state machine written in `onUpdate` cannot tick while the world is frozen, and
   the failure mode where it silently could is invisible. Nothing written against
   this hook before 1.7 changed meaning.
2. **`onFixedUpdate` is simulation time too**, and needs no gate of its own - the
   accumulator behind it is filled from the sim delta, so pause and time-scale
   already reach it. It is also on a *different clock from input*: actions are
   sampled once per render frame, and a fixed step runs zero or many times per
   frame. So a fixed update reads `context().input->command()` - the per-tick
   `InputCommand`, built from the axes as they stand plus the edges latched
   since the previous tick - and never `held()` / `pressed()` / `axis()`, which
   answer for the frame. Asking the frame queries from a fixed update drops a
   tap taken between two ticks and repeats a press across every tick of a slow
   frame.
3. **`onRealtimeUpdate` is real time.** It runs every frame, paused or not, and
   `setTimeScale()` does not reach it either. Menu animation, unscaled timers,
   ducking the music, holding a key to quit - all of it lives here.
4. **"Started" is the play session.** A behavior starts on its first *simulation*
   tick and never in the realtime pass, so `onRealtimeUpdate` begins nothing.
   In the editor, paused is also Edit mode - the transport pauses the clock to
   leave it, and Stop restores the authored scene - so a scene merely open in the
   editor runs **nothing at all**, over a world that has no snapshot to undo.
5. **An entity spawned while paused starts when simulation time next flows.** The
   idiomatic pause menu is therefore built at `onStart` and shown by toggling
   `UIElement::visible`, which is what that field is for.
6. **`destroy()` and `loadScene()` drain on the realtime pass as well**, so a
   pause menu's "quit to the main menu" works while the world is frozen. The
   scene that arrives is still subject to rule 4: nothing in it starts until
   simulation time flows, and the behavior that asked is destroyed by the load,
   so a quit-to-menu resumes the clock as well as asking for the scene - or it
   loads a world that renders and can never run. The engine writes a warning
   when a load lands on a frozen clock, because there is no other symptom. A
   request belongs to the session that made it: ending one - Stop, a scene
   replace, shutdown - discards whatever its `onDestroy` hooks queued on the way
   out, because that request named the world being torn down and the next frame
   to drain it would be an Edit-mode frame over the authored scene.
7. **Queued contacts are dropped while paused.** Physics did not run, so a
   `CollisionEvent` still sitting in the queue describes a world older than the
   pause; delivering it late would be worse than losing it.
8. **`onUpdate` runs before `onRealtimeUpdate`** within a frame, so a realtime
   hook reading world state sees what the simulation produced *this* frame.
9. **`context().clock` is the Clock**, so a game can pause and resume itself: the
   behavior that paused keeps getting realtime ticks and can undo it. Read any
   delta, call `setPaused()` / `setTimeScale()` / `requestStep()`; do **not** call
   `beginFrame()` or `consumeFixedStep()`, which belong to the main loop and
   would corrupt the frame the hook is running in. A game that pauses itself
   inside the editor simply lights the transport's Pause button, which is true,
   and Stop still works: ending the session returns the clock to Edit mode -
   paused, at 1x - so neither a pause nor a time scale outlives the session
   that set it.

```cpp
void PauseMenu::onStart() {
    m_root = buildMenuEntities();   // built once, while time is running
    show(false);
}

void PauseMenu::onUpdate(float dt) {
    // Simulation time: only reached while the world is actually running, which
    // is the only state you can open a pause menu FROM.
    if (context().input->pressed("Cancel")) {
        show(true);
        context().clock->setPaused(true);
    }
}

void PauseMenu::onRealtimeUpdate(float dt) {
    // Real time: still ticking with the world frozen, which is what lets the
    // panel animate in and what lets this behavior undo the pause it made.
    m_fade = std::min(1.0f, m_fade + dt * 4.0f);
    if (m_resumeClicked) {
        m_resumeClicked = false;
        show(false);
        context().clock->setPaused(false);
    }
    if (m_quitClicked) {
        // Resuming is not optional: the loaded scene's behaviors start on a
        // simulation tick, and this one is destroyed by the load.
        context().clock->setPaused(false);
        loadScene("scenes/main_menu.json");                  // drains while paused
    }
}

void PauseMenu::show(bool on) {
    context().scene->get<UIElement>(m_root).visible = on;
}
```

**The engine-wide rule this follows:** *a system reads the timeline its
responsibility lives on*, not the timeline of the stage it sits in. Simulation
state - animation, particles, physics, gameplay's `onUpdate` - reads
`getSimDelta()`. Presentation and services - input, camera, the editor, async
loading, [audio](audio.md#time-pause-and-the-editor), gameplay's
`onRealtimeUpdate` - run every frame regardless of the sim delta, on the real
delta where they need one at all. Which is why
pausing does not cut the music: `AudioSystem` keeps mixing, 3D positions simply
stop changing because nothing moved.

**Deliberately still impossible:** per-entity or per-layer time scales, a nested
pause stack, pausing individual systems, and running behaviors in Edit mode
without pressing Play. `EventBus::flush` also stays unconditional - a paused game
can still *receive* a UI click, and rule 3 is what finally gives it a hook to
answer one in.

## BehaviorRegistry

A process-wide name -> factory registry, mirroring `AssetFactories`. Game code
registers each behavior type at startup; serialization recreates instances by
name.

```cpp
BehaviorRegistry::get().registerBehavior<CubeSpinner>();   // keyed by CubeSpinner::TYPE_NAME
auto instance = BehaviorRegistry::get().create("CubeSpinner");
```

`registerBehavior<T>()` keys off `T::TYPE_NAME`, the same constant `typeName()`
returns - one source of truth shared by registration, serialization, and the
editor's add-behavior menu (`names()`). `clear()` drops every factory before the
game module is unloaded on hot-reload, since the factories close over module code.

## BehaviorSystem

Drives the lifecycle of every entity's `ScriptComponent` behaviors. One `update`
does, in this order:

1. If `getSimDelta() > 0`: tick `onUpdate(simDelta)`, starting (context inject +
   `onStart()`) any instance whose first tick this is; then dispatch the physics
   `CollisionEvent` / `TriggerEvent` collected via subscriptions to the involved
   entities' `onCollision` / `onTrigger`. Otherwise: drop those queues, because
   physics did not run either.
2. Tick `onRealtimeUpdate(getDeltaTime())` on the behaviors that have **already**
   started - never starting one, which is what keeps Edit mode inert.
3. Drain the deferred `destroy()` and `loadScene()` requests, after both passes,
   so a self-destroy can't free its own `ScriptComponent` mid-iterate and a
   paused game can still quit to its menu. `endSession()` empties both queues,
   so a request an outgoing `onDestroy` made cannot drain into the scene that
   replaced it - an entity id does not go dead across a scene swap, it re-aims
   at whatever took its slot.

`fixedUpdate` ticks `onFixedUpdate(getFixedStep())` and starts instances too; its
accumulator is fed from the sim delta, so it needs no pause gate.
[Time and pause](#time-and-pause) is the contract those two paragraphs implement.

Every hook runs under a catch net: a behavior that throws is reported via
`reportError()` (logged, and captured by the editor-owned `EngineErrorLog`) and
disabled, never fatal. `onDestroy` fires on three paths -
entity deletion (wired through `Scene::addObserver` /
`ISceneObserver::onEntityDestroyed` in `init`, dropped again in `shutdown`), play stop,
and shutdown (`endSession`, static so the editor's stop path can call it without
a system handle).

## The gameplay module

The engine ships no gameplay of its own: **the project brings its code**. Each
project builds its sources into `game.dll` / `libgame.so` in its own `bin/`, and
both hosts load it the same way through `ScriptModule` - `vkm_runtime` to play
it, `vkm_editor` to edit it. There is no static-linked variant and no
editor-only path; the shipped game and the edited game run the same binary.

The host `dlopen`s the module and calls the `extern "C"` entry points it finds:

| Entry | Signature | Required? | Purpose |
|-------|-----------|-----------|---------|
| `vkmModuleEngineVersion` | `const char* ()` | Yes | Reports the engine the module was built against; the host refuses a mismatch |
| `vkmRegisterBehaviors` | `void ()` | Yes | Registers the project's behavior types into the engine's `BehaviorRegistry` |
| `vkmBuildScene` | `void (Scene&)` | Optional | Builds the project's world in code. Projects whose scene is generated rather than authored use this instead of `entryScene` |

**The signatures are the contract, and nothing enforces them.** These are
`extern "C"`, so there is no mangling for the linker to disagree about: the host
looks the symbol up by name, `reinterpret_cast`s it to the type above and calls
it. A module that declares an extra parameter compiles, links and loads, and
reads whatever the calling convention left in that register. Copy the signature
from this table exactly.

**The version guard is an ABI guard.** The engine ships prebuilt libraries and is
not ABI-stable between versions: struct layouts, inline functions and templates
are all free to change, which is what lets them keep improving. A module built
against a different version therefore disagrees with the host about memory that
both of them read and write, and the symptom is a crash somewhere unrelated
rather than a load failure - so `ScriptModule` refuses the load and says which
version to rebuild against. A module reporting no version at all predates the
guard and is refused on the same terms rather than assumed compatible.

The module **links `vkm_core`** - `vkm_add_gameplay_module()` does it for every
project - and that is not a second copy of the engine. `vkm_core` is a shared
library, so the module and the host reach the same one, with its single typeId
registry and its single set of singletons. A *static* engine could only manage
that by making the module resolve its symbols from whichever host loaded it,
which is why the engine is shared instead (see [Building](../building.md)).
What a module must not link is the static archives absorbed **into** `vkm_core`;
the helper links none of them.

So everything `vkm_core` carries is available to gameplay code, `logger.h`
included: a behavior logs with the engine's `LOG_*` macros and its lines land in
the project's log file beside the engine's own, rather than only on a console
nobody keeps. Name the file's lines with a category, exactly as engine code does:

```cpp
#define VKM_LOG_CATEGORY "POTION"

#include "potion_runner.h"
...
#include "logger.h"
```

Both example games do this.

The module is looked for in the open project's `bin/` and nowhere else: a game
brings its code with it, and that is the one place a project builds it.

`ScriptModule` loads a **copy** of it (`game.loaded.<n>.so`), so a rebuild is
free to overwrite the original while the editor still holds it - which is what
`vkm build` does mid-session, and what Windows would otherwise refuse outright.
A directory that will take no copy loads the original in place: an installed
game's `bin/` is read-only, and nothing rebuilds into one of those, so the copy
has nothing left to buy there.

`ScriptModule::reload(scene)` swaps in a freshly built module without
restarting: it serializes each entity's behaviors (type + reflected fields),
destroys them, unloads the old module, loads the new one, and recreates the
behaviors from the saved type + fields. Entities and all other components are
untouched - only the behavior C++ objects are rebuilt, and they start fresh
(`onStart` runs again).

**A reload that fails goes through `reportError`, not `LOG_ERROR`.** The saved
documents are put back either way, so a failed reload leaves every behavior held
as text rather than gone: nothing is lost, including on save, and a later reload
that works turns them back into behaviors. What is lost until then is that they
run. That is worth a named entry in **Bottom > Errors**, the way an unresolved
asset reference is, rather than a toast that expires in a few seconds pointing
at a log the editor has no view of.

## Serialization

`ScriptComponent` is in the scene save/load set (key `"Script"`). Each behavior
is stored as its registered type name plus a `properties` object holding every
reflected field, walked through `visitFields` - the same visitor the inspector
and the hot-reload path use, so the three cannot drift. On load
`BehaviorRegistry` recreates the instance by name and the reader fills the
fields back in, keeping a field's constructed default wherever the file has no
value for it. A type the registry does not know is not dropped: it is kept as an
`UnknownBehavior` and written back unread, so a scene saved with the module
missing still holds it. See
[io.md](io.md).
