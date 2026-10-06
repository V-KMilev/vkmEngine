# Scripting (Behaviors)

Native C++ gameplay logic. A `Behavior` is the engine's MonoBehaviour /
ActorComponent analogue: subclass it, override lifecycle hooks, and attach
instances to an entity through a `ScriptComponent`. `BehaviorSystem` drives the
hooks during play; behaviors live in a separate gameplay module that the editor
can hot-reload without restarting.

`BehaviorSystem` runs in `SystemStage::Simulation`, **before** `AnimationSystem`
and `PhysicsSystem`, so a behavior can set state the same frame those integrate
it (events -> gameplay -> animation -> physics). It opts into `fixedUpdate`.

## Your first behavior

A behavior is a C++ class in your project's `src/`, built into the project's
module by `vkm build`. One include brings in what a behavior commonly reaches -
the base class, the reflect block, the body components, input, the physics and
audio events, the asset graph and the log: `system/script/behavior_api.h`.

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
inspector labels a field in words - `pushStrength` shows as "Push Strength" -
and edits a `glm::vec3` or `glm::vec4` whose name ends in `color` or `colour`
with a colour picker.

**Registering it.** The module names its behaviors once, in `src/module.cpp`:

```cpp
VKM_MODULE_ENTRY
void vkmRegisterBehaviors() {
    Vkm::Engine::BehaviorRegistry::get().registerBehaviors<Game::Crate, Game::Door>();
}
```

A behavior left out of that list cannot be loaded from a scene: it is held as
text, and the log says so.

**Attaching it.** In the editor, select an entity and pick the behavior from
Add Behavior; its fields appear on the card. In code - a `vkmBuildScene`, or a
hook spawning something - `addBehavior` gives the entity its `ScriptComponent`
if it has none and returns the new behavior:

```cpp
Crate& crate = addBehavior<Game::Crate>(scene, entity);
crate.pushStrength = 9.0f;
```

Added during play it starts on the next simulation tick, as a loaded one does.

**Reading input.** Name an action once, in `onStart`, and ask for it by name
([input.md](input.md) has the rest - axes, and why a fixed update reads
`command()` instead):

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

**Collisions.** A body is a `Collider` and a `Rigidbody` together. A Collider
without a Rigidbody is in no broadphase at all: it touches nothing and no hook
hears it. Static ground takes a Rigidbody whose `motion` is
`RigidbodyMotion::Static`; something a behavior moves by hand takes `Kinematic`.
`onCollisionEnter`, `onCollisionStay` and `onCollisionExit` are handed a
`Collision`: the entity on the other side, a contact point, and a normal that
points from the other entity into this one - so a crate landing on the floor
hears a normal pointing up. On exit the two touch nowhere, and the point and
normal are zero. A trigger collider calls `onTriggerEnter` / `Stay` / `Exit`
with the entity that entered instead.

**A sound, in one line.** A one-shot that needs no entity of its own - an
impact, a pickup - is an event:

```cpp
void Game::Crate::onCollisionEnter(const Collision& hit) {
    events().emit(PlaySoundEvent::at(m_thud, hit.point));
}
```

`resources().find(thud)` resolved the authored reference to a handle in
`onStart`. A sound that must follow something, loop or stop is an `AudioSource`
instead ([audio.md](audio.md)).

**Another behavior.** `findBehavior<T>(entity)` answers the `T` on an entity,
or null; `findBehavior<T>()` asks this entity. For the behaviors a game has one
of - a director, a menu - `findEntityWithBehavior<T>(scene())` finds the entity
carrying it:

```cpp
if (Health* health = findBehavior<Health>(hit.other)) health->damage(10.0f);
```

Ask each time rather than keeping the pointer: the other entity can be
destroyed between two hooks.

**Listening for an event.** `subscribe` reads the event type off the lambda,
and drops the listener when the behavior goes:

```cpp
subscribe([this](const UIClickEvent& click) { if (click.eventId == "crate:reset") reset(); });
```

What remains is when each hook runs, which [Time and pause](#time-and-pause)
states in full - read it before writing a second behavior.

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

    protected:
        // This entity: what it is, and what it carries.
        EntityId entity() const;                    // to pass to anything taking one
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

        bool                isSimulated();  // does this end decide this entity
        bool                isMine();       // is this the player sitting here
        bool                isReplaying();  // is this tick being run again
        const InputCommand& command();      // the input driving this entity, this tick

        EntityId spawn();                               // an empty entity
        EntityId spawn(const char* name);               // ... carrying a Name
        EntityId spawn(const char* name, EntityId parent);  // ... under a parent
        void     destroy();             // this entity, after the hook pass
        void     destroy(EntityId);     // that one, same deferral
        void     loadScene(const std::string& scenePath);  // deferred to the end of the frame's hooks
        template<typename T> T* findBehavior();          // its behavior of type T, or null
        template<typename T> T* findBehavior(EntityId);  // another entity's
        template<typename E = void, typename Fn> void subscribe(Fn&&);  // E read off Fn; auto-unsubscribes
};
```

The five at the top are the commonest lines in any behavior, which is why they
exist: a behavior asking about its own entity should not have to name it. Reach
for `tryGet<T>()` by default -

```cpp
if (Transform* body = tryGet<Transform>()) body->position += step;
```

one lookup, one mention of the entity, and the null check is the "does it have
one" question already answered. Writing it as `has<T>()` and then `get<T>()`
looks the component up twice and names the entity twice, so a later edit can
change one and not the other. `get<T>()` is for the components the entity
cannot meaningfully run without - the ones its own `onStart` added.

They are shorthand for `scene()` calls with the entity filled in, so the same
four exist there for reaching *another* entity, and `Scene::tryGet` is total:
`scene().tryGet<Transform>(findActiveCamera(scene()))` is a line you can write,
because every `find` in the engine answers with a null id when there is nothing
to find.

Those eight accessors are the whole engine surface a behavior reaches, and they
are named reads of one `BehaviorContext` the `BehaviorSystem` owns and binds
before `onStart()`. Gameplay never holds that struct. Growing the surface is a
field on `BehaviorContext` and an accessor beside these.

The four below them are questions rather than capabilities - they read the
session through `net()` and answer for *this* entity. All four answer the
single-player way offline (`isSimulated()` and `isMine()` yes, `isReplaying()`
no, `command()` the local player's), so a project that never opens a session is
written the same as one that does. [Time and pause](#time-and-pause) below says
when to ask each, and
[networking.md](networking.md#what-a-project-writes) says why.

`window()` is the one that hands back a pointer, and that is the nullability
made visible - a dedicated server runs the same behaviors with no window at all,
so anything reading the cursor or the framebuffer size checks first.

`Behavior` is **non-copyable and non-movable** - instances are owned by
`unique_ptr` inside the `ScriptComponent`. `BehaviorSystem` binds its
session-stable `BehaviorContext` (`bindContext`) before `onStart()`, and because
that context outlives every frame (unlike `FrameContext`), the accessors are
safe from a `subscribe()` callback as well as from inside a hook - and none of
them is worth caching in a member.

- `spawn()` / `destroy()` are the safe structural-edit helpers. `destroy()` is
  deferred to after the current hook pass, so a behavior may destroy its own
  entity from a hook. The two named `spawn` overloads create, name and parent
  in one call; `spawn(name, parent)` is `HierarchyOperations::setParent`, so a UI
  element is parented exactly like anything else. Every one is local to this
  end: on a connected client the new entity takes a slot the server may give to
  something it spawns, and the first such call says so in the log. What every
  end should see is `NetSession::spawn`, on the server
  ([networking.md](networking.md#spawning)).
- **Spawning a prefab** is `Prefab::instantiate(scene(), resources(),
  "prefabs/enemy.json")` (`io/scene/prefab.h`), not a `Behavior` helper: it
  builds the whole subtree and returns its root. It is in `vkm_core`, so a
  gameplay module reaches it like anything else - the networking spawn path
  calls the same function.
- `loadScene()` requests a scene transition. Like `destroy()` it only records the
  request; `BehaviorSystem::update` drains it at the end of the frame's hooks,
  after `onRealtimeUpdate` - a request from `onFixedUpdate` waits for that too -
  swapping the request out before loading. A behavior may therefore ask for the scene that
  will destroy it, from one of its own hooks. The path is project-relative.
- `subscribe()` registers an `EventBus` listener bound to the behavior's
  lifetime - it auto-unsubscribes on destroy, so there is nothing to clean up by
  hand. It is the only way a behavior can listen: `events()` hands out the
  bus's sending half, `EventSender`, because a listener subscribed on the bus
  itself would dangle once the instance dies.

Which hook runs when, and what its `dt` means, is
[Time and pause](#time-and-pause) below - read it before writing the first one.

### ReflectedBehavior - the no-boilerplate path

Most behaviors derive from `ReflectedBehavior<Derived>` (CRTP) instead of
`Behavior` directly. Declare the tunable fields once with the `VKM_REFLECT`
markup and `typeName()`, `visitFields()`, and `clone()` are all generated from
them; you only override the lifecycle hooks. `typeName()` is the class name the
block was opened with, without its namespace (`Reflect::Traits<T>::NAME`), so
the block is required even for a behavior with no fields - empty, it compiles,
and missing, the compile error says what to write. The reflected fields are the
single source of authoring state - they drive the inspector, serialization, and
duplication uniformly.

```cpp
namespace Game {

using namespace Vkm::Engine;

class CubeSpinner : public ReflectedBehavior<CubeSpinner> {
    public:
        void onUpdate(float dt) override;
        float degreesPerSecond = 90.0f;   // authored, reflected
};

} // namespace Game

VKM_REFLECT_BEGIN(::Game::CubeSpinner)
    VKM_F(degreesPerSecond)
VKM_REFLECT_END()
```

### Where your types live

**In your own namespace, never in `Vkm::Engine`.** That namespace is the
engine's, and a project putting types in it can collide with an engine type
added later, or with a second project loaded into the same editor - silently,
because `BehaviorRegistry` keys on the class name rather than on the C++
type.

The one line that makes it cost nothing is the `using namespace Vkm::Engine;`
above: written once inside your namespace, the engine's vocabulary is reachable
unqualified from every declaration in the file, and the leak stops at your own
namespace. The three example projects are `Potion`, `Arena` and `Lab`, and each
does exactly this.

Module entry points are the exception, and they have no choice: `VKM_MODULE_ENTRY`
makes them `extern "C"` at global scope, so they either qualify or open with a
function-scope `using namespace Vkm::Engine;`. `examples/physics_lab/src/module.cpp`
does the latter.

`BehaviorFieldVisitor` is the type-erased bridge that lets code holding only a
`Behavior*` (the inspector, the serializer) read/write a concrete behavior's
fields without knowing its type. The leaf types it supports are `float`, `int`,
`bool`, `std::string`, `glm::vec2`, `glm::vec3`, `glm::vec4` and `glm::quat`,
plus any `VKM_ENUM_NAMES` enum, any `AssetRef<Asset>`, and any `VKM_REFLECT`-ed
struct (descended into). Reflecting anything else is a compile error that lists
these; a value of another type is runtime state, left out of the block.

The inspector shows a field's name in words (`fieldLabel`: `degreesPerSecond`
is "Degrees Per Second"); the name itself stays the key the field is saved
under. A `glm::vec3` or `glm::vec4` whose name ends in `color` or `colour`, in
any case, edits as a colour (`namesAColor`) - the name is all a field carries,
so it is the rule. A `glm::quat` edits as Euler angles in degrees and saves as
the quaternion.

A `std::string` field is free text - a label, a tag, a bone name. It serializes
as a JSON string and edits as a text box. Like every leaf it keeps its current
value when the key is missing, and keeps it with a warning when the key holds
the wrong type, so one bad value does not cost the whole load.

### Naming an asset

A field that points at an asset declares an `AssetRef<Asset>`
(`resource/asset_ref.h`) - never a bare string:

```cpp
class Footsteps : public ReflectedBehavior<Footsteps> {
    public:
        void onStart() override {
            m_clip = resources().find(step);
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
Resolving is the behavior's own job, once, as above -
`ResourceManager::find` takes the reference and answers its handle; nothing
resolves it for you. An empty name means "none".

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
    std::vector<UnknownBehavior> unknown;  // types no module registered, held as text
};
```

The one ECS component that is **not** a plain aggregate: it owns `unique_ptr`s,
so it is move-only (the documented exception in the code-style guide). `SparseSet`
stores it through its `std::move` path; per-behavior deep copy for entity
duplication goes through `Behavior::clone()`. See [ecs.md](ecs.md).

### Authored fields inside a prefab instance

A behavior's authored values - the asset references included - are the prefab's,
identically, on every instance of it: edit them in the prefab, not in an
instance. The inspector says so on the card while it is open, and
`PrefabOverrides::record` refuses the component outright - a `static_assert`,
so the refusal is a build error at whatever call site tried rather than an empty
override list at run time.

The mechanical reason is that the component serializes as a single field holding
the whole behavior list, so a per-field delta on it would be the whole list.
Making one behavior field overridable is therefore not a change to the reference
encoding; it is an override *address* that reaches inside a list - (uid,
component, behavior index or type, field) - which the prefab format does not
have. The case that would want one (this barrel, that door) also wants an
authored entity reference, which the format does not have either.

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
   the failure mode where it silently could is invisible.
2. **`onFixedUpdate` is simulation time too**, and needs no gate of its own - the
   accumulator behind it is filled from the sim delta, so pause and time-scale
   already reach it. It is also on a *different clock from input*: actions are
   sampled once per render frame, and a fixed step runs zero or many times per
   frame. So a fixed update reads `command()` - the per-tick `InputCommand`,
   built from the axes as they stand plus the edges latched since the previous
   tick - and never `held()` / `pressed()` / `axis()`, which answer for the
   frame. `command()` is the input driving *this* entity, which offline is the
   local player's and on a server is the one that entity's player sent; a
   single-player project can read `input().command()` for the same
   thing. Asking the frame queries from a fixed update drops a tap taken
   between two ticks and repeats a press across every tick of a slow frame.

   Two more questions come with it, and a behavior that moves anything asks the
   first: `isSimulated()` - does this end decide what happens to this entity -
   and `isMine()` - is this the player sitting here. Both answer yes offline, so
   a single-player behavior is unchanged by their existence. See
   [networking.md](networking.md#what-a-project-writes).
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
   its canvas's `UICanvas::visible` (or an element's `UIElement::visible`),
   which is what those fields are for.
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
   pause; delivering it late would be worse than losing it. No phase is lost
   to this: a frame that ran a tick advanced simulation time, so the contacts
   that tick reported are dispatched on the same frame.
8. **`onUpdate` runs before `onRealtimeUpdate`** within a frame, so a realtime
   hook reading world state sees what the simulation produced *this* frame. It
   also means **both hooks see the same input edge**: `pressed()` is a fact
   about a frame, not about a hook, so an `onUpdate` that acts on a press hands
   the realtime hook a press it has already spent. A toggle therefore lives in
   one hook, and for anything that has to work while paused - a pause menu
   above all - that hook is `onRealtimeUpdate`. Splitting open and close across
   the two is a menu that opens and shuts inside a single frame, which looks
   exactly like a key that did nothing.
9. **`clock()` is the Clock**, so a game can pause and resume itself: the
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
    if (input().pressed("Cancel")) {
        show(true);
        clock().setPaused(true);
    }
}

void PauseMenu::onRealtimeUpdate(float dt) {
    // Real time: still ticking with the world frozen, which is what lets the
    // panel animate in and what lets this behavior undo the pause it made.
    m_fade = std::min(1.0f, m_fade + dt * 4.0f);
    if (m_resumeClicked) {
        m_resumeClicked = false;
        show(false);
        clock().setPaused(false);
    }
    if (m_quitClicked) {
        // Resuming is not optional: the loaded scene's behaviors start on a
        // simulation tick, and this one is destroyed by the load.
        clock().setPaused(false);
        loadScene("scenes/main_menu.json");                  // drains while paused
    }
}

void PauseMenu::show(bool on) {
    if (UIElement* panel = scene().tryGet<UIElement>(m_root)) panel->visible = on;
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

**Not supported:** per-entity or per-layer time scales, a nested pause stack,
pausing individual systems, and running behaviors in Edit mode without pressing
Play. `EventBus::flush` is unconditional - a paused game can still *receive* a
UI click, and rule 3 gives it a hook to answer one in.

## BehaviorRegistry

A process-wide name -> factory registry. Game code
registers each behavior type at startup; serialization recreates instances by
name.

```cpp
BehaviorRegistry::get().registerBehaviors<CubeSpinner, Door>();   // keyed by class name
auto instance = BehaviorRegistry::get().create("CubeSpinner");
```

`registerBehavior<T>()` keys off `Reflect::Traits<T>::NAME`, the same string
`typeName()` returns - one source of truth shared by registration,
serialization, and the editor's add-behavior menu (`names()`).
`registerBehaviors<A, B, C>()` is that call for each type in turn. `clear()` drops every factory before the
game module is unloaded on hot-reload, since the factories close over module code.

## The gameplay module

The engine ships no gameplay of its own: **the project brings its code**. Each
project builds its sources into `game.dll` / `libgame.so` in its own `bin/`, and
every host loads it the same way through `ScriptModule` - `vkm_runtime` to play
it, `vkm_server` to serve it, `vkm_editor` to edit it. There is no static-linked variant and no
editor-only path; the shipped game and the edited game run the same binary.

The host `dlopen`s the module and calls the entry points it finds. All four are
declared in `system/script/module_entry.h`, which a module includes and marks
each definition with `VKM_MODULE_ENTRY`:

| Entry | Signature | Required? | Purpose |
|-------|-----------|-----------|---------|
| `vkmModuleEngineVersion` | `const char* ()` | Yes | Reports the engine the module was built against; the host refuses a mismatch |
| `vkmRegisterBehaviors` | `void ()` | Yes | Registers the project's behavior types into the engine's `BehaviorRegistry` |
| `vkmBuildScene` | `void (Scene&, ResourceManager&)` | Optional | Builds the project's world in code. Projects whose scene is generated rather than authored use this instead of `entryScene`. It gets the asset graph as well as the scene, because a world made in code needs meshes and materials the same way an authored one does |
| `vkmSetupNetwork` | `void (NetSession&)` | Optional | Says what a joining player is given; a project without it runs offline |

**The signatures matter and the loader cannot check them.** These have C
linkage, so there is no mangling for the linker to disagree about: the host
looks the symbol up by name, `reinterpret_cast`s it to the type above and calls
it. A module that declares an extra parameter would compile, link and load, and
read whatever the calling convention left in that register. Including
`module_entry.h` is what makes that a compile error instead - the declarations
there are the same ones the table names, so a definition that disagrees does not
build.

`VKM_MODULE_ENTRY` also carries the Windows half. An entry has to leave the DLL
under its own name, and MSVC exports nothing by default; written out by hand at
every entry, that `__declspec` is the half a project forgets, and one that wrote
only the `extern "C"` would build a library whose symbols the host cannot find,
on one platform, at run time.

**The version guard is an ABI guard.** The engine ships prebuilt libraries and is
not ABI-stable between versions: struct layouts, inline functions and templates
are all free to change, which is what lets them keep improving. A module built
against a different version therefore disagrees with the host about memory that
both of them read and write, and the symptom is a crash somewhere unrelated
rather than a load failure - so `ScriptModule` refuses the load and says which
version to rebuild against. A module reporting no version at all is refused on
the same terms rather than assumed compatible.

The module **links `vkm_core`** - `vkm_add_gameplay_module()` does it for every
project - and that is not a second copy of the engine. `vkm_core` is a shared
library, so the module and the host reach the same one, with its single typeId
registry and its single set of singletons. A *static* engine could only manage
that by making the module resolve its symbols from whichever host loaded it,
which is why the engine is shared instead (see [Building](building.md)).
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

All three example projects do this.

The module is looked for in the open project's `bin/` and nowhere else: a game
brings its code with it, and that is the one place a project builds it.

`ScriptModule` loads a **copy** of it (`libgame.loaded.<n>.so`, `game.loaded.<n>.dll`), so a rebuild is
free to overwrite the original while the editor still holds it - which is what
`vkm build` does mid-session, and what Windows would otherwise refuse outright.
A directory that refuses writes loads the original in place: an installed
game's `bin/` is read-only, and nothing rebuilds into one of those, so the copy
has nothing left to buy there. Any other failure to copy refuses the load,
because loading in place would lock the very file the next build writes.

`ScriptModule::reload(scene, behaviors, events)` swaps in a freshly built module without
restarting. It first opens the new build beside the running one and checks it -
that it is a library, reports this engine's version and has
`vkmRegisterBehaviors` - and a build that fails any of that is refused, with the
running module and its behaviors left exactly as they were. Only then does it
serialize each entity's behaviors (type + reflected fields), destroy them,
unload the old module, register the new one, and recreate the behaviors from
the saved type + fields. It rebuilds only the behavior C++ objects, and they
start fresh (`onStart` runs again) - so the scene it is handed must hold nothing
else of the old module's; see below.

**The editor notices the rebuild itself.** It stats the built module once a
second - the same interval as the poll that reloads changed shaders - and when `vkm build`
rewrites it, reloads on its own and says so - once the new write time has held
for a whole poll, because a linker writes the file in several passes and a
reload between two of them would open half a library. That includes a module that failed
to load, which keeps its path for exactly this, and a project opened before its
first build: the editor tells the `ScriptModule` where the module will be
(`expect`) rather than leaving it with no path, so the build that creates it is
seen as a change and a reload loads it. With nothing watching the file, a
rebuild would leave the editor running the code it started with until somebody
remembered File > Reload Scripts, and the symptom of forgetting
is a change that simply does not happen, which reads as the change being wrong.

It is not symmetrical with the shader poll, deliberately. A shader reload is
free; this one restarts the play session. So the poll reloads on its own only
while nothing is playing - the alt-tab-and-build case - and during a session it
only says that a rebuild is waiting. A reload mid-session is sanctioned below
because somebody asked for it; a background build finishing is not somebody
asking, and it would discard their session's edits without their having pressed
anything.

**Reloading during a play session is allowed, and is the point** - it is how a
change is seen without replaying up to it. It **restarts** the session: the
editor stops it (putting the authored scene back, exactly as the Stop button
does), swaps the module, and plays again on the new code. Edits made during the
session are discarded, and the toast says so, on the same terms as Stop.

It restarts rather than swapping code under a running game because `onStart`
runs again on every behavior, and a game builds its world there. Left running,
the second `onStart` would build a second world beside the first, with two of
every canvas drawn over each other.

Building assets in `onStart` is fine and needs no guard: `resources().add` is a
declaration of identity, so adding `potion:rig` again replaces what stands under
that name rather than making a `potion:rig (2)` beside it. See
[Resources](resources.md).

**Nothing may hold module code across the swap.** The behavior factories, the
wire schema's thunks, the session's spawn callbacks and the buses of the
module's own event types all live inside the library being unmapped, and
`ScriptModule::releaseRegistrations` drops all four before the `dlclose`.
A behavior's `subscribe()` is dropped for it by `BehaviorSystem::endSession`
(see [Events](events.md)), and it is the only subscribe a behavior can reach:
`events()` is an `EventSender`, which emits and enqueues and cannot subscribe.

A component set is module code too. `Scene` holds one `SparseSet<T>` per
component type, made by whichever binary first adds a `T`, and its vtable lives
in that binary - so a set the module made outlives the module unless the scene
lets go of it, and the next `destroyEntity` calls through it. On Windows that is
every set a `vkmBuildScene` world or an `onStart` fills first, since nothing
interposes the engine's copy; on every platform it is a component type only the
module declares. Three things keep it from happening:

- **`Scene::clear` destroys the sets rather than emptying them**, which is what
  makes a project switch safe: the scene is cleared while the outgoing module is
  still loaded, and the next module's `load` is what unloads it.
- **The editor puts the scene through its serializer before a reload.** A
  played scene gets that from Stop, which rebuilds the authored scene from its
  snapshot; an edit scene is snapshotted and restored the same way first. The
  sets a load makes are the engine's, and the module's go with the scene they
  were swapped out of, while the module is still mapped. A component type only
  the module declares has no row in the scene format, so it does not survive
  that trip - the same as it does not survive a Stop. The undo history does,
  because the round trip puts every entity back in the slot it held, a prefab
  instance's own entities included ([editor.md](editor.md)).
- **`ResourceManager` makes every engine asset type's slot when it is
  constructed or cleared**, in vkm_core, so the textures, meshes and materials a
  `vkmBuildScene` or an `onStart` adds land in storage the engine made. The
  hazard is the same shape - one `SparseSet<T>` per asset type, carrying the
  vtable of whichever binary made it - and nothing swaps the asset graph out
  before a reload, so no slot is left to be made on first use. An asset type a
  project declares for itself would be the module's; the engine's asset types
  are the ones a project uses.

A sound's samples are the same hazard one level down. `AudioClipAsset::samples`
is shared between the clip and its voices, and a shared buffer's control block
carries the code that frees it, so one a module built would be freed by the
module - by whichever voice or scene load drops the last reference, which can be
after a reload has unmapped it. So the field is a `ClipSamples`
(`resource/asset/audio_clip_asset.h`), whose only filling constructor is defined
in vkm_core: there is no way for a module to build the buffer itself.
`potion_runner`'s generated sounds are the example. The editor also stops every
voice before it unmaps a module, on a reload and on a project switch.

A reload loads a fresh copy of the module beside the old one, and on both
platforms the fresh copy's statics start over. On Linux that rests on one flag:
GCC gives every static inside an inline function or a template, and every
inline or template static data member, an `STB_GNU_UNIQUE` symbol, which the
dynamic linker binds across every copy of a library whatever the `dlopen` flags,
and glibc never unmaps a library that defines one. So a module is compiled with
`-fno-gnu-unique` - `vkm_gameplay_module_options` in
`cmake/gameplay_module.cmake` applies it to every module, the engine's test
module included - and `dlclose` unmaps the old copy. The exception is a build
with the profiler: Tracy replaces `dlclose` with a no-op so the zone names a
module recorded stay readable, and there each reload leaves the previous copy
mapped until exit. Its statics are still its own.

So **a module's statics do not survive a reload.** State a behavior needs across
one belongs in its reflected fields, which the reload carries; anything else
belongs in a component or an asset, never in a `static`.

**A reload that fails goes through `reportError`, not `LOG_ERROR`.** A build
that will not open is refused before anything is torn down, so the module
already running stays, its behaviors running, and the Errors panel names the
build that was refused - the log line beside it says why. Past that check the
swap cannot fail. A project whose module never loaded has its behaviors held as
text: nothing is lost, including on save, and the reload that loads a working
build turns them back into behaviors. Either case is a named entry in **the
Errors panel**, the way an unresolved asset reference is.

---

## How it works inside

Everything above is what a project writes. What follows is how the
engine answers it, for whoever maintains that half.

### BehaviorSystem

Drives the lifecycle of every entity's `ScriptComponent` behaviors. One `update`
does, in this order:

1. If `getSimDelta() > 0`: tick `onUpdate(simDelta)`, starting (context inject +
   `onStart()`) any instance whose first tick this is; then dispatch the physics
   `CollisionEvent` / `TriggerEvent` collected via subscriptions to the involved
   entities' hook for the event's phase - `onCollisionEnter` / `Stay` / `Exit`,
   each side handed its own `Collision`, the event's a -> b normal negated for a,
   `onTriggerEnter` / `Stay` / `Exit` ([physics.md](physics.md#events) says when
   each is sent). An entity that is no longer alive is skipped, so an Ended
   naming a destroyed entity reaches only the survivor. Otherwise: drop those
   queues, because physics did not run either.
2. Tick `onRealtimeUpdate(getDeltaTime())` on the behaviors that have **already**
   started - never starting one, which is what keeps Edit mode inert.
3. Drain the deferred `destroy()` and `loadScene()` requests, after both passes,
   so a self-destroy can't free its own `ScriptComponent` mid-iterate and a
   paused game can still quit to its menu. `endSession()` empties both queues,
   so a request an outgoing `onDestroy` made cannot drain into the scene that
   replaced it - an entity id does not go dead across a scene swap, it re-aims
   at whatever took its slot.

`fixedUpdate` ticks `onFixedUpdate(getFixedStep())`, starts instances too, and
drains the deferred `destroy()` requests after each tick (`loadScene()` waits for
`update`); its accumulator is fed from the sim delta, so it needs no pause gate.
[Time and pause](#time-and-pause) is the contract those two paragraphs implement.

Every hook runs under a catch net: a behavior that throws is reported via
`reportError()` (logged, and captured by the editor-owned `EngineErrorLog`) and
disabled, never fatal. A listener registered through `subscribe()` runs under
the same net and is reported, not disabled: it reaches the behavior through the
bus, and one bad event is not a broken behavior. `onDestroy` fires on entity
deletion (wired through `Scene::addObserver` /
`ISceneObserver::onEntityDestroyed` in `init`, dropped again in `shutdown`) and
on every path that ends a session through `endSession`: play stop, a scene the
editor opens or replaces, a hot reload, a scene load a behavior asked for, and
engine shutdown. It is a member, because the queues are the system's: they are
emptied whether or not a behavior is left to reach them through. A host gets
the system from `setupEngineApp`'s `AppSystems`, and hands it to the editor and
to `ScriptModule::reload`.

### Serialization

`ScriptComponent` is in the scene save/load set (key `"Script"`). Each behavior
is stored as its registered type name plus a `properties` object holding every
reflected field, walked through `visitFields` - the same walk the inspector
reads through, and the serializer a hot reload saves and restores with, so the
three cannot drift. On load
`BehaviorRegistry` recreates the instance by name and the reader fills the
fields back in, keeping a field's constructed default wherever the file has no
value for it. A type the registry does not know is not dropped: it is kept as an
`UnknownBehavior` and written back unread, so a scene saved with the module
missing still holds it. See
[io.md](io.md).

### Key files

- `src/engine/system/script/behavior.h` - `Behavior` base + lifecycle hooks
- `src/engine/system/script/reflected_behavior.h` - CRTP base that generates the boilerplate from reflected fields
- `src/engine/system/script/behavior_field_visitor.h` - type-erased field visitor (editor + serializer bridge)
- `src/engine/system/script/script_component.h` - `ScriptComponent` (the ECS component holding the behaviors)
- `src/engine/system/script/behavior_registry.h` - name -> factory registry
- `src/engine/system/script/behavior_system.h/.cpp` - `BehaviorSystem` (the driver)
- `src/engine/system/script/script_module.h/.cpp` - `ScriptModule` (hot-reload of the gameplay DLL)
- `src/engine/platform/library/dynamic_library.h/.cpp` - the `.dll` / `.so` loader
- `examples/<project>/src/` - a project's own behaviors + its `vkmRegisterBehaviors` / `vkmBuildScene` entry points. The engine ships none of its own
