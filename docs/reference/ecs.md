# Entity Component System

The ECS is the core data model. `Scene` is an open type-erased registry:
any type can be a component without modifying Scene.

## Key files

- `src/engine/ecs/scene.h` for the Scene registry
- `src/engine/ecs/entity.h` for the `EntityId` alias
- `src/engine/ecs/component/` for all component types
- `src/engine/core/memory/slot_allocator.h` for the entity handle allocator
- `src/engine/core/memory/sparse_set.h` for component storage
- `src/engine/core/memory/types.h` for `StorageIndex` and `TypeId`

## Entities

Entities are lightweight handles:

```cpp
using EntityId = StorageIndex;             // { uint32_t index, uint32_t generation }
```

- **Generational.** A generation counter prevents use after free; a stale
  handle with the wrong generation is detected.
- **Recycled.** Destroyed entity slots go onto a LIFO free list for reuse.
- **Null sentinel.** Index 0 is reserved as null; `operator bool()` returns
  `index != 0`.

```cpp
EntityId entity = scene.createEntity();
bool     alive  = scene.isAlive(entity);
scene.destroyEntity(entity);              // removes all components, recycles slot
```

`Scene::createEntityAt(slotIndex)` exists for the scene loader, which
recreates entities at their saved slot so cross-entity references in the
file (e.g. `Hierarchy::parent` indices) resolve directly. See
[IO and serialization](system/io.md) for the round-trip rules.

## Components

Components are plain data structs stored in `SparseSet<T>` containers.
Any type can be a component; no registration, no base class.

```cpp
scene.add(entity, Transform{ .position = {1, 2, 3} });
scene.add(entity, Mesh{ .mesh = meshHandle, .material = matHandle });

auto& transform = scene.get<Transform>(entity);
bool  hasMesh   = scene.has<Mesh>(entity);
scene.remove<Mesh>(entity);
```

### Built-in components

| Component        | File                              | Fields                                                                                                |
|------------------|-----------------------------------|-------------------------------------------------------------------------------------------------------|
| `Transform`      | `component/transform.h`           | `vec3 position`, `quat rotation`, `vec3 scale`                                                        |
| `WorldTransform` | `component/world_transform.h`     | `mat4 model` (resolved each frame by `HierarchySystem`)                                               |
| `Camera`         | `component/camera.h`              | `projection` (`ProjectionType`), `fovY`, `aspect`, `orthoHeight`, `zNear`, `zFar`, `focusDistance`, `dofAmount`, `active` |
| `Mesh`           | `component/mesh.h`                | `MeshHandle mesh`, `MaterialHandle material`, `bool visible`, `bool castShadows`                      |
| `Light`          | `component/light.h`               | `LightType` (Directional, Point, Spot, Rect, Disk), color, intensity, attenuation, cone, area, shadow |
| `Animation`      | `component/animation.h`           | Three tracks (position vec3, rotation quat, scale vec3) plus playback state and explicit `length`     |
| `Hierarchy`      | `component/hierarchy.h`           | `EntityId parent`, `firstChild`, `nextSibling`, `prevSibling`, `bool dirty`                           |
| `Name`           | `component/name.h`                | `char value[64]` for editor display and asset look-up by name                                         |
| `Collider`       | `component/collider.h`            | One or more `ColliderBox` parts (`center`, `halfExtents`) + `isTrigger`                               |
| `Rigidbody`      | `component/rigidbody.h`           | Dynamic body: linear/angular velocity, mass, damping, restitution, friction, gravity scale, kinematic/static flags |
| `ReflectionProbe`| `component/reflection_probe.h`    | Local IBL probe: `halfExtents` influence box, `falloff`, `intensity`, `resolution`                   |

Light gets a full breakdown in [Lighting](system/lighting.md), including
the area-light fields (`areaWidth`, `areaHeight`, `areaRadius`, `twoSided`)
introduced for Rect and Disk emitters. `Rigidbody` and `Collider` are covered in
[Physics](system/physics.md); the scene-level physics settings (gravity,
solver iterations) are not a component either - they live in `PhysicsSettings`,
reached through `Scene::physics()`. It sits *beside* the `Environment` rather
than inside it: what a world is lit by and what it falls at are unrelated, so
they are owned and serialized separately.

One more component is **not** a plain aggregate: `ScriptComponent`
(`system/script/script_component.h`) holds
`std::vector<std::unique_ptr<Behavior>>`, making it move-only - the documented
exception to the data-struct rule. It attaches native gameplay behaviors to an
entity; see [Scripting](system/scripting.md).

### Static helpers

Components are data-only structs with static math helpers when useful:

```cpp
glm::mat4 model = Transform::computeModelMatrix(transform);
glm::mat4 view  = Transform::computeView(transform);
glm::mat4 proj  = Camera::computeProjection(camera, viewportAspect);
```

`computeProjection` takes the viewport's aspect as a fallback: `camera.aspect`
wins when it is positive, and `camera.aspect <= 0` (the default) means "track
whatever viewport I am rendering into".

## Queries

### Single component

```cpp
scene.forEach<Transform>([](EntityId id, Transform& t) {
    // every entity with a Transform
});
```

### Multi-component

```cpp
scene.forEach<Mesh, Transform>([](EntityId id, Mesh& mesh, Transform& t) {
    // only entities with BOTH Mesh and Transform
});
```

Multi-component queries iterate the **first** type densely, then check
remaining types via `SparseSet::contains()`. Put the rarest component
first for best performance.

### Direct storage access

For index-based parallel iteration (used by `VisibilitySystem`):

```cpp
auto* meshStorage = scene.storage<Mesh>();
for (uint32_t i = 0; i < meshStorage->size(); ++i) {
    uint32_t entityIdx = meshStorage->keyAt(i);
    Mesh&    mesh      = meshStorage->dataAt(i);
}
```

## Component storage

Each component type gets a `SparseSet<T>`:

- **Dense array.** Packed component data, no holes. O(n) iteration.
- **Sparse array.** Maps entity index to dense index. O(1) lookup.
- **Swap and pop removal.** Keeps the dense array packed. O(1).
- **Type erasure.** `ISparseSet` base lets Scene store heterogeneous
  sets in a single vector indexed by `typeId<T>()`. New component
  types do not require Scene changes.

## Hierarchy

Parent and child relationships go through the `Hierarchy` component.
Mutation goes through `HierarchyOperations` (free functions in
`system/hierarchy/hierarchy_operations.h`):

```cpp
HierarchyOperations::setParent(scene, child, parent);
HierarchyOperations::removeFromParent(scene, entity);
HierarchyOperations::destroyHierarchy(scene, entity);  // entity + every descendant
glm::mat4 world = HierarchyOperations::computeWorldMatrix(scene, entity);
HierarchyOperations::markDirty(scene, entity);
```

`setParent` pre-seeds both `Hierarchy` and `WorldTransform` on the
involved entities so the per-frame `HierarchySystem::update()` resolve
loop has no structural Scene work to do. That is the precondition that
lets the resolve loop parallelise over depth buckets. See
[Hierarchy system](system/hierarchy.md) for the full per-frame flow.

On entity destruction:

- The leaf path unlinks via `removeFromParent`.
- The subtree path (`destroyHierarchy`) recursively destroys every descendant.

Both paths are undoable from the editor through `DestroySubtreeCommand`
(see [Editor](editor.md)).
