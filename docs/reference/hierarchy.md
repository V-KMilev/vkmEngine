# Hierarchy System

`HierarchySystem` resolves parent/child relationships into per-entity
world matrices once per frame. Downstream systems
(`VisibilitySystem`, `RenderSystem`) read the resulting `WorldTransform`
where an entity has one and fall back to its local `Transform` where it has
none - an entity outside any hierarchy.

It runs in `SystemStage::Transform`, between `Simulation` and
`Visibility`.

## Key files

- `src/engine/system/hierarchy/hierarchy_system.h` for the System.
- `src/engine/ecs/hierarchy_operations.h` for the free-function mutation API.
- `src/engine/ecs/component/core/hierarchy.h` for the `Hierarchy` component
  (`parent`, `firstChild`, `lastChild`, `nextSibling`, `prevSibling`).
- `src/engine/ecs/component/core/world_transform.h` for the resolved
  `WorldTransform` (a single `glm::mat4 model`).

## Mutation API

`HierarchyOperations` is a namespace of free functions; no class state.
This is the only sanctioned way to mutate the hierarchy graph; direct
component writes are not.

| Function                                  | Purpose                                                                                |
|-------------------------------------------|----------------------------------------------------------------------------------------|
| `setParent(scene, child, parent, before)` | Link `child` under `parent`, as its last child or, given `before`, in front of that child. Pre-seeds `Hierarchy` + `WorldTransform` on both ends; refuses a ring and a dead entity at either end. |
| `removeFromParent(scene, entity)`         | Unlink from its current parent; `entity` becomes a root. A leaf left with no relatives loses its `Hierarchy` and `WorldTransform`. |
| `destroyHierarchy(scene, entity)`         | Destroy `entity` plus every descendant, deepest first.                                 |

The rest of the namespace reads the graph rather than writing it, and is where
every walk over it belongs. Every walk is bounded - one up a parent chain by
`MAX_DEPTH`, one down a subtree or across a sibling list by the scene's entity
count - so a cycle ends the walk rather than looping:

| Function                                          | Purpose                                                                              |
|---------------------------------------------------|--------------------------------------------------------------------------------------|
| `forEachChild(scene, entity, fn)`                 | Walk the direct children of `entity` and call `fn(childId)`. The one owner of the walk across a sibling list and its bound, the live entity count: every walk that goes down, `HierarchySystem`'s included, crosses a list through it. |
| `forSelfAndAncestors(scene, entity, fn)`          | Call `fn` on `entity` and then each ancestor, nearest first, until it returns false. The one owner of the upward walk's `MAX_DEPTH` bound and its warning: `computeWorldMatrix` and `findInSelfOrAncestors` climb through it. |
| `computeWorldMatrix(scene, entity)`               | Walk the parent chain and compose the world matrix without writing `WorldTransform`.  |
| `findInSelfOrDescendants<T>(scene, root)`         | The nearest entity at or below `root` carrying a `T`, breadth first. A model import puts what a component needs on a different entity than the one an author selects - the rig on the node the bones are composed in, the physics on the root - so the code looks in the direction the answer is. |
| `findInSelfOrDescendantsIf(scene, root, match)`   | The same walk against a predicate rather than a component type - what the prefab override machinery asks to find the entity carrying a given uid. `findInSelfOrDescendants<T>` is one call of it. |
| `findInSelfOrAncestors<T>(scene, leaf)`           | The same question upward: the nearest entity at or above `leaf` carrying a `T`. What `Prefab::instanceRootOf` asks, among others. |
| `isAncestorOf(scene, ancestor, node)`             | Whether `ancestor` is a *strict* ancestor of `node` - the cycle check a reparent runs before it links anything. Bounded by the live entity count, not `MAX_DEPTH`: a chain deeper than that may exist, and a check cut short on it would let a reparent close a ring. |
| `collectSubtree(scene, root)`                     | Every entity at or below `root`, breadth first with parents before children, bounded by the live entity count rather than by depth. It is what a subtree *is*: `destroyHierarchy` destroys this set, deepest first; the editor's undo snapshot of a subtree captures it; Save as Prefab walks it to drop the history entries that address the subtree it just wrote. So the set deleted, the set restored and the set that reached the file cannot disagree. |
| `warnWalkBound(walk, bound)`                      | What a bounded walk calls when it reaches its bound (`collectSubtree` reports its own refusal): the hierarchy's links form a ring or run deeper than the walk follows. Said once for the process, from `vkm_core`, naming the walk and the bound it reached - the ancestor walk's `MAX_DEPTH`, or the entity count of a sibling list or a subtree search. |

## Sibling order

Children are kept in the order they were attached: `setParent` appends, through
the `lastChild` link, so attaching stays O(1) however many siblings there are.
The child parented last is walked last by `forEachChild`, listed last in the
editor's hierarchy panel, and - since a UI canvas draws in walk order - drawn
on top of its earlier siblings.

The order survives a save. The scene saver writes each root's subtree in walk
order and the loader re-attaches in file order, so a reload attaches the
children in the order they had; a prefab does the same through
`collectSubtree`. Slots are restored as saved too, but the order is the file's
and not the slots'.

## Reading a world pose

The read side of the same rule lives beside the component, in
`ecs/component/core/world_transform.h`, so no consumer has to open-code it:

| Function                                          | Returns                                                        |
|---------------------------------------------------|----------------------------------------------------------------|
| `resolvedWorldMatrix(scene, entity, local)`       | `WorldTransform::model` if present, else the local model matrix |
| `resolvedWorldPosition(scene, entity, local)`     | The same rule, translation only                                 |
| `resolvedWorldRotation(scene, entity, local)`     | The same rule, rotation only (via `Math::worldRotationOf`)      |
| `resolvedWorldScale(scene, entity, local)`        | The same rule, scale only - the length of each basis column      |

The `resolved` prefix is the distinction from
`HierarchyOperations::computeWorldMatrix`: these read what the Transform
stage resolved this frame, while `computeWorldMatrix` walks the ancestor
chain and answers for the scene as it stands right now. A Transform
written *after* the Transform stage (the editor's gizmo, for instance) is
visible to the second and not to the first until the next frame.

### Why pre-seeding matters

`setParent` adds both the `Hierarchy` component and a placeholder
`WorldTransform` to every entity in the link, **before** the resolve
loop runs. That guarantees the per-frame loop never needs to call
`Scene::add<T>()` on a parented entity, which would be a structural
mutation that is not safe to do in parallel.

This is what makes the parallel resolve possible. Any code that creates
parent/child relationships outside `setParent` would re-introduce the
hazard, so do not bypass it.

## Per-frame resolve

`HierarchySystem::update` does the following:

1. Gather the roots: every entity with a `Hierarchy` and a null
   `parent`.
2. For each level from the roots outward, `parallelFor` over the
   level: read the local `Transform` and the parent's
   `WorldTransform`, compose, write into `WorldTransform`. Then gather
   the next level by walking each entity's child list, taking a child
   only when its own `parent` names that entity.

An entity whose `WorldTransform` is missing is neither resolved nor
descended into: pre-seeding makes that unreachable, and step 2 indexes
both an entity's slot and its parent's without a live check. An entity
with no `Transform` is descended into but not written, so its children
compose onto the matrix it already holds - the identity `setParent`
seeded. A cycle is never reached from a root, so it is never visited;
a chain deeper than `MAX_DEPTH` stops there with a warning, the bound
the ancestor walks stop at too.

Every hierarchical entity resolves every frame; there is no dirty flag. A
flag is a second statement of whether a matrix is current, kept true only by
every writer of a `Transform` remembering to set it
([engine.md](../guides/engine.md#4-what-has-already-been-decided)).

By processing depth 0, then depth 1, then depth 2, ..., each level
sees its parent's `WorldTransform` already up to date. Within a level
no entity is another's parent, so they can be resolved concurrently.

A hierarchy root - an entity that has a `Hierarchy` component but a null
`parent` - is level 0, where its `WorldTransform` is set
straight from its local `Transform` (no parent matrix to compose). Entities
with no `Hierarchy` component at all are never visited here; downstream
consumers fall back to the local `Transform` directly.

## Editor integration

The hierarchy panel uses `forEachChild` to walk the tree at draw time.
Drag-to-reparent ultimately calls `setParent` through a
`ReparentCommand` so the change is undoable. Subtree destruction goes
through `DestroySubtreeCommand` which snapshots the whole subtree
(including parent/child links) before calling `destroyHierarchy`, so
undo can resurrect the structure exactly.

See [Editor](editor.md) for the command shapes.
