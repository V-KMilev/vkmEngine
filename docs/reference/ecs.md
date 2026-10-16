# Entity Component System

Everything a game puts in a world is an entity carrying components. `Scene` is
the registry that holds them: an entity is a handle, a component is a plain
struct, and any type can be a component - no registration, no base class.

## Entities

```cpp
EntityId entity = scene.createEntity();
bool     alive  = scene.isAlive(entity);
scene.destroyEntity(entity);              // removes all components, recycles slot
```

An `EntityId` wraps a `StorageIndex` (a slot and a generation):

- **Generational.** A destroyed slot is reused, and a stale handle to it is
  detected by its generation.
- **Null is slot 0**; `operator bool()` is `slot() != 0`.
- **Its own type**, so a file's slot, a prefab's index or any other bare integer
  cannot be written into an entity field.
- **Scene-local, and only for this session.** Anything durable - a file, a
  prefab, an undo snapshot, a connection - names an entity in its own terms and
  resolves it back through an `EntityNamer` / `EntityResolver` (see
  [IO and serialization](io.md#componentserializer)).

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

Each header documents its fields.

| Component             | File                                       | What it is                                                                                            |
|-----------------------|--------------------------------------------|-------------------------------------------------------------------------------------------------------|
| `Transform`           | `component/core/transform.h`               | Local position, rotation and scale                                                                    |
| `WorldTransform`      | `component/core/world_transform.h`         | The world matrix, resolved each frame by `HierarchySystem` |
| `Camera`              | `component/render/camera.h`                | Projection, depth of field, viewmodel field of view; `findActiveCamera` finds the one in use |
| `Mesh`                | `component/render/mesh.h`                  | A mesh and the material it is drawn with, shadow casting, which views see it (`SeenBy`), and whether it ever moves (`movable` - [Visibility](visibility.md#fixed-meshes)) |
| `Light`               | `component/render/light.h`                 | Directional, Point, Spot, Rect or Disk light ([Lighting](lighting.md))                                |
| `Animation`           | `component/animation/animation.h`          | Position, rotation and scale tracks for this entity's own `Transform`                                 |
| `Animator`            | `component/animation/animator.h`           | Plays a clip on a skeleton - one per rigged character, not per mesh                                   |
| `BoneSocket`          | `component/animation/bone_socket.h`        | What rides a joint of the rig it is parented to, at an `offset`                                       |
| `Hierarchy`           | `component/core/hierarchy.h`               | Parent, first/last child and sibling links ([below](#hierarchy))                                      |
| `Name`                | `component/core/name.h`                    | `char value[64]` - the entity's display name, in the editor and in log lines                          |
| `NetLocal`            | `component/core/net_local.h`               | Nothing - its presence keeps the entity and everything below it off the wire ([Networking](networking.md#what-does-not-replicate)) |
| `Collider`            | `component/physics/collider.h`             | One or more `ColliderPart`s (Box, Capsule or Mesh) + `isTrigger` + `enabled`                          |
| `CharacterController` | `component/physics/character_controller.h` | Move, jump, climb and crouch input in; ground contact out ([Physics](physics.md))                     |
| `Rigidbody`           | `component/physics/rigidbody.h`            | A Dynamic, Kinematic or Static body, plus the support and blocking outputs                            |
| `ReflectionProbe`     | `component/render/reflection_probe.h`      | Local IBL probe over an influence box                                                                 |
| `PlanarReflector`     | `component/render/planar_reflector.h`      | A flat mirror: glossy surfaces on its rectangle reflect the scene drawn again from the mirrored camera |
| `AudioSource`         | `component/audio/audio_source.h`           | A clip to play, spatial or not; `playing` is runtime state ([Audio](audio.md#starting-and-stopping-a-sound-from-gameplay)) |
| `AudioListener`       | `component/audio/audio_listener.h`         | The ear every spatial source is heard relative to                                                     |
| `Decal`               | `component/render/decal.h`                 | A material projected onto whatever its box covers                                                     |
| `LOD`                 | `component/render/lod.h`                   | Mesh levels, each with a `maxDistance`, chosen by the camera's distance and `bias`                    |
| `IrradianceVolume`    | `component/render/irradiance_volume.h`     | The baked SH-L1 grid indirect diffuse comes from                                                      |
| `Ocean`               | `component/render/ocean.h`                 | The spectrum a `waves` material's surface moves by. One a scene (`findOcean`); see [rendering.md](rendering.md#the-ocean) |
| `ParticleEmitter`     | `component/render/particle_emitter.h`      | How particles are emitted; the live particles are `ParticleSystem`'s                                  |
| `ParticleTrigger`     | `component/render/particle_emitter.h`      | Makes a child's emitter spawn on its parent's particles' births, deaths or strikes                    |
| `Joint`               | `component/physics/joint.h`                | A `JointType` constraint to the `connected` entity                                                    |
| `Ragdoll`             | `component/physics/ragdoll.h`              | One `RagdollBone` per simulated bone, each naming the body entity that simulates it                   |
| `ScriptComponent`     | `system/script/script_component.h`         | The behaviors attached to this entity, owned as `unique_ptr<Behavior>`; see [Scripting](scripting.md) |
| `PrefabInstance`      | `component/prefab/prefab_instance.h`       | Marks an instance root: the `source` prefab and its `overrides`                |
| `PrefabEntity`        | `component/prefab/prefab_entity.h`         | `uid` - what an override addresses                                   |
| `NetSpawn`            | `component/prefab/net_spawn.h`             | Runtime only: what `NetSession::spawn` built ([Networking](networking.md#spawning)) |
| `MissingAssets`       | `component/core/missing_assets.h`          | Asset names a load could not resolve, kept for the next save ([IO](io.md#a-reference-that-did-not-resolve-is-kept)) |
| `UICanvas`            | `component/ui/ui_canvas.h`                 | The root of one screen-space UI tree                                                                  |
| `UIElement`           | `component/ui/ui_element.h`                | An element's rect - anchor, pivot, position, size - and the resolved `screenRect`                     |
| `UIImage`             | `component/ui/ui_image.h`                  | Fills its element's rect with a tint, a shape and an optional texture                                 |
| `UIText`              | `component/ui/ui_text.h`                   | Text in a named font                                                                                  |
| `UIButton`            | `component/ui/ui_button.h`                 | A pressable element: four state colours, a shape, an `eventId`                                        |
| `UIScroll`            | `component/ui/ui_scroll.h`                 | Scrolls its content by `offset`                                                                       |
| `UISlider`            | `component/ui/ui_slider.h`                 | A value between `minValue` and `maxValue`, laid out on its `fill` and `knob` entities                 |
| `UIToggle`            | `component/ui/ui_toggle.h`                 | An on/off switch that slides its `knob` entity                                                        |
| `UIChoice`            | `component/ui/ui_choice.h`                 | One of its `options`, stepped by its `back` and `next` buttons and shown in its `text`                |

Notes the table cannot hold:

- **Skinning has no component.** A mesh is skinned when its asset carries skin
  weights, and the rig driving it is the nearest `Animator` above it in the
  hierarchy ([Animation](animation.md)).
- **A UI element is one entity.** `UIImage`, `UIText`, `UIButton` and `UIScroll`
  go on the same entity as its `UIElement`, not under it; nesting is the entity
  hierarchy ([UI](ui.md)).
- **Scene-wide settings are not components:** `PhysicsSettings`
  ([Physics](physics.md#components)) and the `Environment` (`Scene::environment()`).

Components carry static helpers where the math belongs to them:

```cpp
glm::mat4 model = Transform::computeModelMatrix(transform);
glm::mat4 view  = Transform::computeView(transform);
glm::mat4 proj  = Camera::computeProjection(camera, viewportAspect);
```

`camera.aspect <= 0` (the default) means "track the viewport I render into";
a positive `aspect` wins over `viewportAspect`.

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

For index-based or parallel iteration, take the storage, which is null until
some entity has added that type:

```cpp
if (auto* meshStorage = scene.storage<Mesh>()) {
    for (uint32_t i = 0; i < meshStorage->size(); ++i) {
        uint32_t entityIdx = meshStorage->keyAt(i);
        Mesh&    mesh      = meshStorage->dataAt(i);
    }
}
```

## Hierarchy

Parenting is the `Hierarchy` component, changed only through
`HierarchyOperations` ([Hierarchy](hierarchy.md)). `Scene::destroyEntity`
destroys one entity: its children move up to its parent (or become roots), each
keeping its world pose.

## How it works inside

Each component type gets a `SparseSet<T>`: densely packed data, a sparse array
from entity slot to dense index, and swap-and-pop removal. The scene keeps its
sets in a `TypeRegistry` indexed by `typeId<T>()`, so a new component type needs
no change to `Scene`. Entity slots come from a `SlotAllocator` with a free list.
`Scene::createEntityAt(slotIndex)` exists for the scene loader, which recreates
entities at their saved slots so saved references resolve directly.

## Key files

- `src/engine/ecs/scene.h` - the Scene registry
- `src/engine/ecs/entity.h` - `EntityId`
- `src/engine/ecs/component/` - the components, by subject: `core/`, `render/`,
  `animation/`, `audio/`, `physics/`, `ui/`, `prefab/`
- `src/engine/ecs/hierarchy_operations.h` - parenting
- `src/engine/ecs/entity_mapping.h` - `EntityResolver` / `EntityNamer`
- `src/engine/ecs/scene_observer.h` - `ISceneObserver`, told before an entity
  is torn down
- `src/engine/core/memory/sparse_set.h` - component storage
- `src/engine/core/memory/slot_allocator.h` - entity slots
- `src/engine/core/memory/types.h` - `StorageIndex` and `TypeId`
- `src/engine/core/memory/type_registry.h` - one slot per type; also holds the
  ResourceManager's assets and the EventBus's buses
