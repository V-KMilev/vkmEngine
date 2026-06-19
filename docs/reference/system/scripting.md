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
- `game/` - the concrete gameplay behaviors (`CubeSpinner`, `PlayerController`, `registerGameBehaviors`)

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
        Entity spawn();                 // create a new entity
        void   destroy(EntityId);       // deferred until after the hook pass
        template<typename E> void subscribe(std::function<void(const E&)>);  // auto-unsubscribes

        EntityId           m_entity;
        Scene*             m_scene;
        ResourceManager*   m_resources;
        const InputHandle* m_input;     // read-only keyboard/mouse
        EventSystem*       m_events;    // gameplay pub/sub
};
```

`Behavior` is **non-copyable and non-movable** - instances are owned by
`unique_ptr` inside the `ScriptComponent`. `BehaviorSystem` injects the engine
context (`bindContext`) before `onStart()`, so the hooks reach the scene,
resources, input, and events without globals.

- `spawn()` / `destroy()` are the safe structural-edit helpers. `destroy()` is
  deferred to after the current hook pass, so a behavior may destroy its own
  entity from a hook.
- `subscribe<E>()` registers an `EventSystem` listener bound to the behavior's
  lifetime - it auto-unsubscribes on destroy, so there is nothing to clean up by
  hand (a raw `m_events->subscribe` would dangle once the instance dies).

### ReflectedBehavior - the no-boilerplate path

Most behaviors derive from `ReflectedBehavior<Derived>` (CRTP) instead of
`Behavior` directly. Declare the tunable fields once with the `VKM_REFLECT`
markup and `typeName()`, `visitFields()`, and `clone()` are all generated from
them; you only override the lifecycle hooks. The reflected fields are the
single source of authoring state - they drive the inspector, serialization, and
duplication uniformly.

```cpp
class CubeSpinner : public ReflectedBehavior<CubeSpinner> {
    public:
        static constexpr const char* TYPE_NAME = "CubeSpinner";
        void onUpdate(float dt) override;
        float degreesPerSecond = 90.0f;   // authored, reflected
};

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
only while the `SimulationClock` is running (`ctx.simDeltaTime > 0`), so pause /
step / Stop apply uniformly:

1. On an instance's first tick, inject context and call `onStart()`.
2. `onUpdate(simDeltaTime)` every frame; `onFixedUpdate(fixedDeltaTime)` every
   fixed tick.
3. Dispatch physics `CollisionEvent` / `TriggerEvent` (collected via
   subscriptions) to the involved entities' `onCollision` / `onTrigger` hooks.
4. Drain the deferred-`destroy()` queue after the hook pass (so a self-destroy
   can't free its own `ScriptComponent` mid-iterate).

Every hook runs under a catch net: a behavior that throws is logged to
`BehaviorErrorLog` and disabled, never fatal. `onDestroy` fires on three paths -
entity deletion (wired through `Scene::setOnEntityDestroy` in `init`), play stop,
and shutdown (`endSession`, static so the editor's stop path can call it without
a system handle).

## Hot reload and the two binaries

The gameplay layer lives in `game/` and is built two ways (see
[building.md](../building.md)):

- **`engine_runtime`** static-links `game` and calls `registerGameBehaviors()`
  directly. No hot-reload; this is the shipped-game shape.
- **`engine_editor`** loads the same sources compiled as a shared library
  (`game_module` -> `game.dll` / `libgame.so`) through `ScriptModule`, which
  `dlopen`s it and calls its `vkmRegisterBehaviors` entry. The module resolves
  engine symbols from the host exe (Windows: an import lib; Linux/macOS:
  `-rdynamic`), so there is no second copy of the engine inside it.

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
