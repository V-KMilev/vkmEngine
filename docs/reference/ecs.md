# Entity Component System

The ECS is the core data model. `Scene` is an open type-erased registry:
any type can be a component without modifying Scene.

## Entities

Entities are lightweight handles:

```cpp
struct EntityId { StorageIndex key; };     // slot() and generation()
```

- **Its own type**, wrapping a `StorageIndex` rather than aliasing one. A
  resource handle is built on the same pair, and a function taking an asset
  slot must not silently accept an entity - but the reason that matters most is
  that an `EntityId` cannot be built from two loose numbers. A file's slot, a
  prefab's index and a live handle are all bare integers that a field like
  `Joint::connected` cannot tell apart; writing one into an entity field does
  not compile.
- **Generational.** A generation counter prevents use after free; a stale
  handle with the wrong generation is detected.
- **Recycled.** Destroyed entity slots go onto a LIFO free list for reuse.
- **Null sentinel.** Index 0 is reserved as null; `operator bool()` is
  `slot() != 0`.
- **Scene-local, and only for this session.** It names a slot in one `Scene` in one
  session. Anything durable - a file, a prefab, an undo snapshot, a
  connection - names an entity in its own terms and resolves it back.

```cpp
EntityId entity = scene.createEntity();
bool     alive  = scene.isAlive(entity);
scene.destroyEntity(entity);              // removes all components, recycles slot
```

`Scene::createEntityAt(slotIndex)` exists for the scene loader, which
recreates entities at their saved slot so a saved reference resolves directly.
A slot becomes an entity in three lines of `Scene` and nowhere else.

A component that references other entities takes the carrier that knows what
it calls one - an `EntityNamer` to save, an `EntityResolver` to load - exactly
as a component referencing assets takes the `ResourceManager`. So a scene names
an entity by its slot and a prefab by its place in the file, without either
writing that number where a handle belongs. See [IO and serialization](io.md) for the round-trip rules.

## Components

Components are plain data structs stored in `SparseSet<T>` containers.
Any type can be a component; no registration, no base class.

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

| Component             | File                                       | Fields                                                                                                |
|-----------------------|--------------------------------------------|-------------------------------------------------------------------------------------------------------|
| `Transform`           | `component/core/transform.h`               | `vec3 position`, `quat rotation`, `vec3 scale`                                                        |
| `WorldTransform`      | `component/core/world_transform.h`         | `mat4 model` (resolved each frame by `HierarchySystem`); read it via `resolvedWorld{Matrix,Position,Rotation,Scale}` |
| `Camera`              | `component/render/camera.h`                | `projection` (`ProjectionType`), `fovY`, `aspect`, `orthoHeight`, `zNear`, `zFar`, `focusDistance`, `dofAmount`, `dofMaxBlur`, `active`; `findActiveCamera` picks the one already held, else the lowest-slot active one |
| `Mesh`                | `component/render/mesh.h`                  | `MeshHandle mesh`, `MaterialHandle material`, `bool visible`, `bool castShadows`                      |
| `Light`               | `component/render/light.h`                 | `LightType` (Directional, Point, Spot, Rect, Disk), color, intensity, attenuation, cone, area, shadow |
| `Animation`           | `component/animation/animation.h`          | Three tracks (position vec3, rotation quat, scale vec3) plus playback state and explicit `length`     |
| `Animator`            | `component/animation/animator.h`           | `SkeletonHandle skeleton`, `AnimationClipHandle clip`, `speed`, `looping`, `playOnStart`; `time`, `playing`, `started`, the cross-fade state and `adjust` are runtime only - one per rigged character, not per mesh |
| `BoneSocket`          | `component/animation/bone_socket.h`        | `string bone` + `Transform offset` - what rides a joint of the rig it is parented to                  |
| `Hierarchy`           | `component/core/hierarchy.h`               | `EntityId parent`, `firstChild`, `lastChild`, `nextSibling`, `prevSibling`                            |
| `Name`                | `component/core/name.h`                    | `char value[64]` - the entity's display name, in the editor and in log lines                          |
| `Collider`            | `component/physics/collider.h`             | One or more `ColliderPart`s (a `ColliderShape` tag - Box, Capsule or Mesh - plus the fields for each) + `isTrigger` + `enabled` |
| `CharacterController` | `component/physics/character_controller.h` | `moveInput` + `jumpRequested` in, tuning, `grounded` + `groundNormal` out                    |
| `Rigidbody`           | `component/physics/rigidbody.h`            | Body: linear/angular velocity, `motion` (Dynamic, Kinematic or Static), mass, damping, restitution, friction, gravity scale, plus the `supported` / `supportNormal` / `blockNormal` outputs |
| `ReflectionProbe`     | `component/render/reflection_probe.h`      | Local IBL probe: `halfExtents` influence box, `falloff`, `intensity`, `resolution`                   |
| `AudioSource`         | `component/audio/audio_source.h`           | `AudioClipHandle clip`, `volume`, `pitch`, `loop`, `spatial`, `playOnStart`, `min`/`maxDistance`; `playing` + `started` are runtime only |
| `AudioListener`       | `component/audio/audio_listener.h`         | `active`, `volume` - the ear every spatial source is heard relative to                               |
| `Decal`               | `component/render/decal.h`                 | `MaterialHandle material`, `angleFade`, `opacity`, `enabled` - projected onto whatever its box covers |
| `LOD`                 | `component/render/lod.h`                   | `levels` (a `MeshHandle` + `maxDistance` each) and `bias`; the mesh drawn is chosen by the camera's distance to the world bounds' centre, shortened for a camera zoomed in past the reference field of view (lengthened for a wider one) and divided by `bias` |
| `IrradianceVolume`    | `component/render/irradiance_volume.h`     | `halfExtents`, `resolutionX/Y/Z`, `intensity`, `blendDistance` (metres its light fades in from each face), `bakeVersion` - the baked SH-L1 grid indirect diffuse comes from |
| `ParticleEmitter`     | `component/render/particle_emitter.h`      | `emitting`, `rate`, `lifetime`, `maxParticles`, `velocity`, `spread`; the live particles are `ParticleSystem`'s |
| `Joint`               | `component/physics/joint.h`                | `JointType`, `connected` entity, the two anchors, `distance`, `stiffness`, `collideConnected`        |
| `Ragdoll`             | `component/physics/ragdoll.h`              | `active`, `root`, `boneLayer`, `rigScale`, and one `RagdollBone` per body (bone index, body entity, `bodyFromBone`) |
| `ScriptComponent`     | `system/script/script_component.h`         | The behaviors attached to this entity, owned as `unique_ptr<Behavior>`; see [Scripting](scripting.md) |
| `PrefabInstance`      | `component/prefab/prefab_instance.h`       | `source` prefab path plus the per-instance `overrides`; marks the root of an instance                |
| `PrefabEntity`        | `component/prefab/prefab_entity.h`         | `uid` - what an override addresses, stable across a re-instantiate                                   |
| `NetSpawn`            | `component/prefab/net_spawn.h`             | `prefab` path, the exact `Transform at` it was built at, and `slots` - where the server built each entity below the root; runtime only, put on a spawned root by `NetSession::spawn` so every client builds the same thing ([Networking](networking.md#spawning)) |
| `MissingAssets`       | `component/core/missing_assets.h`          | Asset names a load could not resolve, kept so the next save writes them back rather than erasing them |
| `UICanvas`            | `component/ui/ui_canvas.h`                 | `referenceHeight`, `scaleMode`, `sortOrder`, `visible` - the root of one screen-space UI tree        |
| `UIElement`           | `component/ui/ui_element.h`                | `anchor`, `pivot`, `position`, `size`, `relativeSize`, `visible`, `blocksPointer`, `clipChildren`, plus the resolved `UIRect screenRect` |
| `UIImage`             | `component/ui/ui_image.h`                  | `color`, a `UIShape` `shape` (corners, edge, fade) and an optional `TextureHandle texture` - fills its element's rect, the texture stretched over it and multiplied by the tint |
| `UIText`              | `component/ui/ui_text.h`                   | `text`, `font` name, `pixelSize`, `color`, `align`, `valign`, `wrap`                                 |
| `UIButton`            | `component/ui/ui_button.h`                 | The four state colours, a `UIShape` `shape`, `eventId`, `interactable`; `state`, `held`, `pointer` and `resolvedSize` are runtime only |
| `UIScroll`            | `component/ui/ui_scroll.h`                 | `offset`, `wheelStep`; `contentSize` and `viewSize` are measured by the layout walk and published     |

`Animation` and `Animator` are both covered in [Animation](animation.md):
the first drives one entity's own `Transform`, the second poses a whole rig and
publishes the result on `FrameContext::poses`. There is deliberately no skinned-
mesh component - a mesh is skinned when its asset carries skin weights, and the
rig driving it is the nearest `Animator` above it in the hierarchy. `BoneSocket`
is the third: it names a joint of the rig above it and reads that joint out of
the published pose, which is how a weapon ends up in a hand.

The six `UI*` components are one family and are covered in [UI](ui.md):
a `UICanvas` roots a tree, every node in it carries a `UIElement` for its rect,
and `UIImage`, `UIText`, `UIButton` and `UIScroll` are what an element can *be* -
added to the same entity, not parented under it. Nesting is the entity
hierarchy, so a UI tree is built with `Hierarchy` like anything else.

`AudioSource` and `AudioListener` are covered in [Audio](audio.md). There
is no `play()` call: `AudioSource::playing` is the state the source wants to be
in, gameplay writes it to start or stop a sound and reads it back to learn that
a one-shot finished, and `AudioSystem` reconciles the two.

Light gets a full breakdown in [Lighting](lighting.md), including
the area-light fields (`areaWidth`, `areaHeight`, `areaRadius`, `twoSided`)
that Rect and Disk emitters use. `Rigidbody`, `Collider` and
`CharacterController` are covered in [Physics](physics.md) - including why
"am I standing on something" and "what is in my way" are both answered on the
`Rigidbody` rather than on the controller, which is what keeps `PhysicsSystem`
from knowing characters exist;
the scene-level physics settings (gravity,
solver iterations) are not a component either - they live in `PhysicsSettings`,
reached through `Scene::physics()`. It sits *beside* the `Environment` rather
than inside it: what a world is lit by and what it falls at are unrelated, so
they are owned and serialized separately.

One more component is **not** a plain aggregate: `ScriptComponent`
(`system/script/script_component.h`) holds
`std::vector<std::unique_ptr<Behavior>>`, making it move-only - the documented
exception to the data-struct rule. It attaches native gameplay behaviors to an
entity; see [Scripting](scripting.md).

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

## Hierarchy

Parent and child relationships go through the `Hierarchy` component.
Mutation goes through `HierarchyOperations` (free functions in
`ecs/hierarchy_operations.h`):

```cpp
HierarchyOperations::setParent(scene, child, parent);
HierarchyOperations::removeFromParent(scene, entity);
HierarchyOperations::destroyHierarchy(scene, entity);  // entity + every descendant
glm::mat4 world = HierarchyOperations::computeWorldMatrix(scene, entity);
```

`setParent` pre-seeds both `Hierarchy` and `WorldTransform` on the
involved entities so the per-frame `HierarchySystem::update()` resolve
loop has no structural Scene work to do. That is the precondition that
lets the resolve loop parallelise over each level of the tree. See
[Hierarchy system](hierarchy.md) for the full per-frame flow.

On entity destruction:

- `Scene::destroyEntity` destroys one entity. Its children move up to its
  parent (or become roots), each keeping its world pose - the local
  `Transform` is re-expressed in the new parent's space - and the entity
  is then unlinked with `removeFromParent`.
- The subtree path (`destroyHierarchy`) destroys every descendant, deepest
  first, however deep the subtree goes - the set `collectSubtree` walks.

The editor deletes through the subtree path, undoably, with
`DestroySubtreeCommand` (see [Editor](editor.md)).

---

## How it works inside

Everything above is what a project writes. What follows is how the
engine answers it, for whoever maintains that half.

### Component storage

Each component type gets a `SparseSet<T>`:

- **Dense array.** Packed component data, no holes. O(n) iteration.
- **Sparse array.** Maps entity index to dense index. O(1) lookup.
- **Swap and pop removal.** Keeps the dense array packed. O(1).
- **Type erasure.** `ISparseSet` base lets Scene store heterogeneous
  sets in a single vector indexed by `typeId<T>()`. New component
  types do not require Scene changes.

### Key files

- `src/engine/ecs/scene.h` for the Scene registry
- `src/engine/ecs/entity.h` for the `EntityId` type
- `src/engine/ecs/component/` for all component types, grouped by subject:
  `core/`, `render/`, `animation/`, `audio/`, `physics/`, `ui/`, `prefab/`
- `src/engine/core/memory/slot_allocator.h` for the entity handle allocator
- `src/engine/core/memory/sparse_set.h` for component storage
- `src/engine/core/memory/types.h` for `StorageIndex` and `TypeId`
- `src/engine/core/memory/type_registry.h` for `TypeRegistry<Base>`, the one-slot-per-type
  container the Scene's component storages, the ResourceManager's assets and the
  EventBus's per-event buses are all built on
- `src/engine/ecs/entity_mapping.h` for `EntityResolver` / `EntityNamer` - how a
  cross-entity reference survives a file, a prefab or a connection
- `src/engine/ecs/scene_observer.h` for `ISceneObserver`, notified before an
  entity is torn down
