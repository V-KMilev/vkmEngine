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
        virtual void onStart()                   {}  // first tick in play mode
        virtual void onUpdate(float dt)          {}  // variable step; dt = simDeltaTime
        virtual void onFixedUpdate(float dt)     {}  // fixed step; dt = fixedDeltaTime
        virtual void onCollision(EntityId other) {}  // non-trigger contact this tick
        virtual void onTrigger(EntityId other)   {}  // trigger overlap this tick
        virtual void onDestroy()                 {}  // teardown

        virtual const char*               typeName() const = 0;   // == BehaviorRegistry key
        virtual void                      visitFields(BehaviorFieldVisitor&) {}
        virtual std::unique_ptr<Behavior> clone() const = 0;      // deep copy for duplication

    protected:
        BehaviorContext& context();     // scene / resources / window / events
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

### ReflectedBehavior - the no-boilerplate path

Most behaviors derive from `ReflectedBehavior<Derived>` (CRTP) instead of
`Behavior` directly. Declare the tunable fields once with the `VKM_REFLECT`
markup and `typeName()`, `visitFields()`, and `clone()` are all generated from
them; you only override the lifecycle hooks. The reflected fields are the
single source of authoring state - they drive the inspector, serialization, and
duplication uniformly.

```cpp
namespace Engine {

class CubeSpinner : public ReflectedBehavior<CubeSpinner> {
    public:
        static constexpr const char* TYPE_NAME = "CubeSpinner";
        void onUpdate(float dt) override;
        float degreesPerSecond = 90.0f;   // authored, reflected
};

} // namespace Engine

VKM_REFLECT_BEGIN(::Engine::CubeSpinner)
    VKM_F(degreesPerSecond)
VKM_REFLECT_END()
```

`BehaviorFieldVisitor` is the type-erased bridge that lets code holding only a
`Behavior*` (the inspector, the serializer) read/write a concrete behavior's
fields without knowing its type. It supports `float`, `int`, `bool`, and
`glm::vec3`; reflecting an unsupported type is a compile error.

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

Drives the lifecycle of every entity's `ScriptComponent` behaviors. It ticks
only while the `Clock` is running (`ctx.clock.getSimDelta() > 0`), so pause /
step / Stop apply uniformly:

1. On an instance's first tick, inject context and call `onStart()`.
2. `onUpdate(getSimDelta())` every frame; `onFixedUpdate(getFixedStep())` every
   fixed tick.
3. Dispatch physics `CollisionEvent` / `TriggerEvent` (collected via
   subscriptions) to the involved entities' `onCollision` / `onTrigger` hooks.
4. Drain the deferred-`destroy()` queue after the hook pass (so a self-destroy
   can't free its own `ScriptComponent` mid-iterate).

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

| Entry | Required? | Purpose |
|-------|-----------|---------|
| `vkmRegisterBehaviors` | Yes | Registers the project's behavior types into the engine's `BehaviorRegistry` |
| `vkmBuildScene` | Optional | Builds the project's world in code. Projects whose scene is generated rather than authored use this instead of `entryScene` |

The module resolves engine symbols from the host that loaded it (Windows: an
import lib; Linux: `ENABLE_EXPORTS`), so there is no second copy of the
engine inside it. It must **not** link `vkm_core` - a second copy would
duplicate the typeId registry and the singletons, and types registered against
one copy are invisible to the other.

The module is looked for in the open project's `bin/` and nowhere else: a game
brings its code with it, and that is the one place a project builds it.

`ScriptModule::reload(scene)` swaps in a freshly built module without
restarting: it serializes each entity's behaviors (type + reflected fields),
destroys them, unloads the old module, loads the new one, and recreates the
behaviors from the saved type + fields. Entities and all other components are
untouched - only the behavior C++ objects are rebuilt, and they start fresh
(`onStart` runs again).

## Serialization

`ScriptComponent` is in the scene save/load set (key `"Script"`). The scene
serializer stores each behavior by its registered type name and recreates it
through `BehaviorRegistry` on load, dropping any behavior whose type is not
registered. Note: the **scene** path persists type names only - per-behavior
field values are not yet written to the scene file (the in-memory hot-reload
path above does round-trip fields). See [io.md](io.md).
