# Entity Component System

Everything a game puts in a world is an entity carrying components. `Scene` is
the registry that holds them: an entity is a handle, a component is a plain
struct, and any type can be a component - its storage needs no registration and
no base class. Surviving a save is a step of its own
([io.md](io.md#adding-a-component-to-the-round-trip)), and so is reaching the other
players ([networking.md](networking.md#what-replicates)).

## Entities

```cpp
EntityId entity = scene.createEntity();
bool     alive  = scene.isAlive(entity);
scene.destroyEntity(entity);              // removes all components, recycles slot
```

An `EntityId` is a slot and the generation the slot had, so a stale id is caught
rather than answered by the slot's next entity. It is scene-local, and only for this
session: anything durable - a file, a prefab, an undo snapshot, a connection - names
an entity in its own terms and resolves it back through an `EntityNamer` /
`EntityResolver` ([IO and serialization](io.md#componentserializer)).

## Components

```cpp
Transform placed;
placed.position = {1.0f, 2.0f, 3.0f};
scene.add(entity, std::move(placed));

scene.add(entity, Mesh{meshHandle, materialHandle});

auto& transform = scene.get<Transform>(entity);
bool  hasMesh   = scene.has<Mesh>(entity);
scene.remove<Mesh>(entity);
```

### Built-in components

Each one's header, under `src/engine/ecs/component/` by subject, says what it is and
what each field means. A field's reflect row carries what the inspector says of it -
`VKM_F(intensity, 0.0f, 100000.0f, "How bright: ...")`, its range and what it is for -
as a behavior's does ([Scripting](scripting.md#fields)), so the two read alike and the
card names no number.

- **`scene/`**: `Transform`, `WorldTransform` and `Hierarchy` ([below](#hierarchy)),
  `Name`, `MissingAssets` ([IO](io.md#a-reference-that-did-not-resolve-is-kept)).
- **`net/`**: `NetLocal` ([Networking](networking.md#what-does-not-replicate)), `NetSpawn`
  ([IO](io.md#prefabs)).
- **`render/`**: `Camera`, `Mesh`, `LOD` ([Visibility](visibility.md)), `Outline`
  ([Rendering](rendering.md)).
- **`light/`**: `Light`, `ReflectionProbe` and `IrradianceVolume` ([Lighting](lighting.md)).
- **`effect/`**: `Decal`, `ParticleEmitter` ([Rendering](rendering.md)).
- **`landscape/`**: `Water` ([Water](water.md)), `Terrain` ([Terrain](terrain.md)).
- **`animation/`**: `Animation`, `Animator`, `BoneSocket` ([Animation](animation.md)).
- **`audio/`**: `AudioSource`, `AudioListener` ([Audio](audio.md)).
- **`physics/`**: `Rigidbody`, `Collider` with its mesh triangles (`mesh_collider`),
  `CharacterController`, `Joint`, `Ragdoll` ([Physics](physics.md)), `Buoyancy`
  ([Water](water.md#floating-things)).
- **`ui/`**: `UICanvas`, `UIElement`, `UIImage`, `UIText`, `UIButton`, `UIScroll`,
  `UIStack`, `UISlider`, `UIToggle`, `UIChoice`, and the `UI::` builders that make them
  (`ui_build.h`) ([UI](ui.md)).
- **`prefab/`**: `PrefabInstance`, `PrefabEntity` ([IO](io.md#prefabs)).
- **`script/`**: `ScriptComponent`, the entity's behaviors, the one move-only component
  ([architecture.md](architecture.md#known-exceptions), [Scripting](scripting.md)).

The scene format's own list is `VKM_SCENE_COMPONENTS` (`io/scene/scene_components.h`);
a component off it is session state or written its own way.

- **Skinning has no component.** A mesh is skinned when its asset carries skin
  weights, and the rig driving it is the nearest `Animator` at or above it in the
  hierarchy ([Animation](animation.md)).
- **A UI element is one entity.** `UIImage`, `UIText`, `UIButton`, `UIScroll` and `UIStack`
  go on the same entity as its `UIElement`, not under it; nesting is the entity
  hierarchy ([UI](ui.md)).
- **Scene-wide settings are not components:** `Scene::physics()` and
  `Scene::environment()`, saved with the scene, and `Scene::water()`, the water as it
  stands, built from its `Water`s and waves and never saved ([Water](water.md)).

## Queries

```cpp
scene.forEach<Transform>([](EntityId id, Transform& t) {
    // every entity with a Transform
});

scene.forEach<Mesh, Transform>([](EntityId id, Mesh& mesh, Transform& t) {
    // only entities with BOTH Mesh and Transform
});
```

- **Put the rarest component first.** The walk iterates the first type densely
  and tests the rest for membership.
- **Do not change what is being walked.** Adding or removing the first type, or
  creating or destroying entities, inside the walk is not allowed; collect what
  to do and act after.
- **A hot loop walks the storage** (`Scene::storage`, null until some entity has added
  that type), densely or under `parallelFor`, rather than looking each entity up by id.

## Hierarchy

Parenting is the `Hierarchy` component, changed only through
`HierarchyOperations` ([Hierarchy](hierarchy.md)). `Scene::destroyEntity`
destroys one entity: its children move up to its parent, keeping their world pose.
A behavior's `destroy()` takes the whole subtree.

## How it works inside

Each component type has a `SparseSet<T>`, kept in a `TypeRegistry` by `typeId<T>()`, so
a new type needs no change to `Scene`; entity slots come from a `SlotAllocator`. The scene
loader recreates each entity at its saved slot (`Scene::createEntityAt`), so a saved
reference resolves with no remap. A file holds slots, not generations, so a load starts every
slot at the first generation; a world rebuilt under the ids it had - the editor's Stop - first
starts each slot at the generation it had there (`Scene::slotGenerations`,
`Scene::startSlotsAt`).

## Key files

- `src/engine/ecs/scene.h` - `Scene`, the registry, and `ISceneObserver`, told before an entity is
  torn down; `src/engine/ecs/entity.h` - `EntityId`
- `src/engine/ecs/component/` - the components, by subject
- `src/engine/ecs/hierarchy_operations.h` - parenting
- `src/engine/io/scene/entity_mapping.h` - `EntityResolver` / `EntityNamer`
- `src/engine/ecs/prefab_instances.h` - where prefab instances stand: `Prefab::instanceRootOf` and the built slots
- `src/engine/base/container/` - `SparseSet` (component storage), `SlotAllocator` (entity
  slots), `StorageIndex` (`storage_index.h`), `TypeId` (`type_id.h`), and `TypeRegistry`, which also holds
  the ResourceManager's assets and the EventBus's buses
