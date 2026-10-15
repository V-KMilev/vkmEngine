# Hierarchy System

Parent/child links between entities, and the world matrix each child ends up
with. `HierarchySystem` resolves them once a frame in `SystemStage::Transform`
(between `Simulation` and `Visibility`) into each entity's `WorldTransform`;
an entity outside any hierarchy has none and is read by its local `Transform`.

## Changing the graph

`HierarchyOperations` is a namespace of free functions and the only sanctioned
way to change the graph - never write the components directly.

| Function                                  | Purpose                                                                                |
|-------------------------------------------|----------------------------------------------------------------------------------------|
| `setParent(scene, child, parent, before)` | Link `child` under `parent`, last or in front of `before`. Refuses a ring and a dead entity. |
| `removeFromParent(scene, entity)`         | Make `entity` a root.                                                                  |
| `destroyHierarchy(scene, entity)`         | Destroy `entity` and every descendant.                                                 |

`setParent` adds `Hierarchy` and a placeholder `WorldTransform` to both ends
before the resolve runs, so the parallel per-frame loop never adds a component -
a structural change that is not thread-safe. Linking entities any other way
brings that hazard back.

Children keep the order they were attached in: the last parented is walked
last, listed last in the editor's hierarchy panel and, on a UI canvas, drawn on
top. Saving and prefabs keep the order.

## Walking the graph

Every walk over the graph belongs in the namespace, and every one is bounded, so
a cycle ends the walk instead of hanging it:

| Function                                          | Purpose                                                                              |
|---------------------------------------------------|--------------------------------------------------------------------------------------|
| `forEachChild(scene, entity, fn)`                 | Call `fn(childId)` for each direct child, in order.                                  |
| `forSelfAndAncestors(scene, entity, fn)`          | Call `fn` on `entity`, then each ancestor nearest first, until it returns false.     |
| `computeWorldMatrix(scene, entity)`               | The world matrix as the scene stands now, composed up the chain; writes nothing.     |
| `findInSelfOrDescendants<T>(scene, root)`         | The nearest entity at or below `root` with a `T`, breadth first - an imported model puts the rig or physics below the entity an author selects. |
| `findInSelfOrDescendantsIf(scene, root, match)`   | The same walk against a predicate.                                                   |
| `findNamed(scene, root, name)`                    | The same walk for a `Name`; of two, the shallower wins.                              |
| `findInSelfOrAncestors<T>(scene, leaf)`           | The nearest entity at or above `leaf` with a `T`.                                    |
| `isAncestorOf(scene, ancestor, node)`             | Whether `ancestor` is a strict ancestor of `node` - the check a reparent runs.       |
| `collectSubtree(scene, root)`                     | Every entity at or below `root`, parents first. What a subtree *is*: destroy, undo and Save as Prefab all use it, so they agree. |
| `warnWalkBound(walk, bound)`                      | Called by a walk that hit its bound (a ring, or a chain too deep); warns once.        |

## Reading a world pose

`ecs/component/core/world_transform.h` reads what this frame's Transform stage
resolved, falling back to the local `Transform` when there is no
`WorldTransform`:

| Function                                          | Returns                                                        |
|---------------------------------------------------|----------------------------------------------------------------|
| `resolvedWorldMatrix(scene, entity, local)`       | `WorldTransform::model` if present, else the local model matrix |
| `resolvedWorldPosition(scene, entity, local)`     | The same rule, translation only                                 |
| `resolvedWorldRotation(scene, entity, local)`     | The same rule, rotation only                                    |
| `resolvedWorldScale(scene, entity, local)`        | The same rule, scale only                                       |

A `Transform` written after the Transform stage (the editor's gizmo, say) shows
in `computeWorldMatrix` at once and in these only next frame.

## How it works inside

`HierarchySystem::update` resolves level by level from the roots, each level
under `parallelFor`: compose the local `Transform` onto the parent's
`WorldTransform`. An entity with no `Transform` is descended into but not
written. There is no dirty flag - every hierarchical entity resolves every frame,
because a flag would be a second statement of whether a matrix is current
([engine.md](../guides/engine.md#4-what-has-already-been-decided)).

In the editor, drag-to-reparent goes through a `ReparentCommand` and delete
through `DestroySubtreeCommand`, which snapshots the subtree first so undo
restores it exactly ([editor.md](editor.md)).

## Key files

- `src/engine/ecs/hierarchy_operations.h` - the API above
- `src/engine/system/hierarchy/hierarchy_system.h` - the per-frame resolve
- `src/engine/ecs/component/core/hierarchy.h` - `Hierarchy`
- `src/engine/ecs/component/core/world_transform.h` - `WorldTransform` and the readers
