# Hierarchy System

`HierarchySystem` resolves parent/child relationships into per-entity
world matrices once per frame. Downstream systems
(`VisibilitySystem`, `RenderSystem`) consume the resulting
`WorldTransform` component when present and fall back to local
`Transform` for roots.

It runs in `SystemStage::Transform`, between `Simulation` and
`Visibility`.

## Key files

- `src/engine/system/hierarchy/hierarchy_system.h` for the System.
- `src/engine/system/hierarchy/hierarchy_operations.h` for the
  free-function mutation API.
- `src/engine/ecs/component/hierarchy.h` for the `Hierarchy` component
  (`parent`, `firstChild`, `nextSibling`, `prevSibling`, `dirty`).
- `src/engine/ecs/component/world_transform.h` for the resolved
  `WorldTransform` (a single `glm::mat4 model`).

## Mutation API

`HierarchyOperations` is a namespace of free functions; no class state.
This is the only sanctioned way to mutate the hierarchy graph; direct
component writes are not.

| Function                                  | Purpose                                                                                |
|-------------------------------------------|----------------------------------------------------------------------------------------|
| `setParent(scene, child, parent)`         | Link `child` under `parent`. Pre-seeds `Hierarchy` + `WorldTransform` on both ends.    |
| `removeFromParent(scene, entity)`         | Unlink from its current parent; `entity` becomes a root.                               |
| `detachAndReparentChildren(scene, entity)`| Promote each child to a top-level entity, then leave `entity` itself a root.           |
| `destroyHierarchy(scene, entity)`         | Destroy `entity` plus every descendant. Used by editor `DestroySubtreeCommand`.        |
| `forEachChild(scene, entity, fn)`         | Walk the direct children of `entity` and call `fn(childId)`.                           |
| `computeWorldMatrix(scene, entity)`       | Walk the parent chain and compose the world matrix without writing `WorldTransform`.   |
| `markDirty(scene, entity)`                | Flag the subtree rooted at `entity` so the per-frame resolve recomputes it.            |

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

1. Bucket all dirty parented entities by hierarchy depth.
2. For each depth bucket from 0 (roots) outward, `parallelFor` over
   the bucket: read the local `Transform` and the parent's
   `WorldTransform`, compose, write into `WorldTransform`.
3. Clear the dirty flag.

By processing depth 0, then depth 1, then depth 2, ..., each level
sees its parent's `WorldTransform` already up to date. Within a depth
the entities are siblings; they share no parent/child dependency, so
they can be resolved concurrently.

For roots (entities without a `Hierarchy` parent), the resolve is a
no-op: downstream consumers fall back to local `Transform` directly.

`SystemAccess` declared by `HierarchySystem`:

```cpp
SystemAccess access;
access.reads  = { typeId<Transform>(), typeId<Hierarchy>() };
access.writes = { typeId<WorldTransform>() };
return access;
```

This lets the scheduler pack `HierarchySystem` on a parallel layer with
any other system that only reads `Transform`/`Hierarchy` and doesn't
touch `WorldTransform`.

## Editor integration

The hierarchy panel uses `forEachChild` to walk the tree at draw time.
Drag-to-reparent ultimately calls `setParent` through a
`ReparentCommand` so the change is undoable. Subtree destruction goes
through `DestroySubtreeCommand` which snapshots the whole subtree
(including parent/child links) before calling `destroyHierarchy`, so
undo can resurrect the structure exactly.

See [Editor](../editor.md) for the command shapes.
