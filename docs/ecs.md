# Entity-Component-System

The ECS is the core data model. `Scene` is an open type-erased registry -- any type can be a component without modifying Scene.

## Key Files

- `src/engine/ecs/scene.h` -- Scene registry
- `src/engine/ecs/entity.h` -- Entity wrapper
- `src/engine/ecs/component/` -- All component types
- `src/engine/core/memory/slot_allocator.h` -- Entity handle allocator
- `src/engine/core/memory/sparse_set.h` -- Component storage
- `src/engine/core/memory/types.h` -- StorageIndex, TypeId

## Entities

Entities are lightweight handles: `using EntityId = StorageIndex` where `StorageIndex = { uint32_t index, uint32_t generation }`.

- **Generational**: Generation counter prevents use-after-free. A stale handle with wrong generation is detected.
- **Recycled**: Destroyed entity slots go onto a LIFO free list for reuse.
- **Null sentinel**: Index 0 is reserved as null. `operator bool()` returns `index != 0`.

```cpp
Entity entity = scene.createEntity();
bool alive = scene.isAlive(entity);
scene.destroyEntity(entity);  // removes all components, recycles slot
```

## Components

Components are plain structs stored in `SparseSet<T>` containers. Any type can be a component -- no registration or base class needed.

```cpp
scene.add(entity, Transform{.position = {1, 2, 3}});
scene.add(entity, Mesh{.mesh = meshHandle, .material = matHandle});

auto& transform = scene.get<Transform>(entity);
bool hasMesh = scene.has<Mesh>(entity);
scene.remove<Mesh>(entity);
```

### Built-in Components

| Component | File | Fields |
|-----------|------|--------|
| **Transform** | `component/transform.h` | `vec3 position, quat rotation, vec3 scale` |
| **Camera** | `component/camera.h` | `ProjectionType, fovY, aspect, zNear, zFar, exposure, active` |
| **Mesh** | `component/mesh.h` | `MeshHandle mesh, MaterialHandle material, bool visible` |
| **Light** | `component/light.h` | `LightType type, vec3 color, float intensity, radius, coneAngles` |
| **Animation** | `component/animation.h` | `AnimationTrack<vec3/quat> tracks, float duration/time/speed, bool playing/looping` |
| **Hierarchy** | `component/hierarchy.h` | `EntityId parent, firstChild, nextSibling, prevSibling` |
| **Name** | `component/name.h` | `char name[64]` |

### Static Helpers

Components are data-only structs with static helper methods:

```cpp
glm::mat4 model = Transform::computeModelMatrix(transform);
glm::mat4 view  = Transform::computeView(transform);
glm::mat4 proj  = Camera::computeProjection(camera);
```

## Queries

### Single Component

```cpp
scene.forEach<Transform>([](EntityId id, Transform& t) {
    // iterate all entities with Transform
});
```

### Multi-Component

```cpp
scene.forEach<Mesh, Transform>([](EntityId id, Mesh& mesh, Transform& t) {
    // only entities with BOTH Mesh and Transform
});
```

Multi-component queries iterate the first type densely, then check remaining types via `SparseSet::contains()`. Put the rarest component first for best performance.

### Direct Storage Access

For index-based parallel iteration:

```cpp
auto* meshStorage = scene.storage<Mesh>();
for (uint32_t i = 0; i < meshStorage->size(); ++i) {
    uint32_t entityIdx = meshStorage->keyAt(i);
    Mesh& mesh = meshStorage->dataAt(i);
}
```

## Component Storage

Each component type gets a `SparseSet<T>`:

- **Dense array**: Packed component data, no holes. O(n) iteration.
- **Sparse array**: Maps entity index to dense index. O(1) lookup.
- **Swap-and-pop removal**: Keeps dense array packed. O(1).
- **Type erasure**: `ISparseSet` base allows Scene to store heterogeneous sets in a single vector indexed by `typeId<T>()`.

## Hierarchy

Parent-child relationships via the `Hierarchy` component. Utility functions in `ecs/hierarchy_utils.h`:

```cpp
HierarchyUtils::setParent(scene, child, parent);
glm::mat4 world = HierarchyUtils::computeWorldMatrix(scene, entity);
HierarchyUtils::destroyHierarchy(scene, entity);  // destroys entity and all descendants
```

On entity destruction, children are automatically reparented to the grandparent.
