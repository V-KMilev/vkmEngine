# Hierarchy System

Parent/child links between entities, and the world matrix each child ends up
with. `HierarchySystem` resolves them once a frame in `SystemStage::Transform`
(between `Simulation` and `Visibility`) into each entity's `WorldTransform`;
an entity outside any hierarchy has none and is read by its local `Transform`.

## Changing the graph

`HierarchyOperations` (`src/engine/ecs/hierarchy_operations.h`) is a namespace of free
functions and the only sanctioned way to change the graph - never write the components
directly; `setParent` says why. It links a child (last, or in front of a sibling), makes a
root, and destroys an entity with every descendant. A chain is at most
`HierarchyOperations::MAX_DEPTH` entities deep: `setParent` refuses one deeper, a cycle or
a dead entity, with a warning.

Children keep their order - the last attached, or where `setParent`'s `before` put it:
they are walked in it, listed in it in the editor's hierarchy panel and, on a UI canvas,
drawn in it, the last on top. Saving and prefabs keep the order.

## Walking the graph

The namespace also walks the graph: the children, the ancestors, the nearest component or
name at or below or above an entity, a whole subtree, the world matrix now - each a
function in its header. Every walk is bounded, so a cycle ends it instead of hanging it:
the namespace's are, and one written elsewhere (`UISystem`'s layout, a recursive one)
stops at `HierarchyOperations::MAX_DEPTH`.

## Reading a world pose

`computeWorldMatrix` composes the chain as the scene stands now. `resolvedWorldMatrix`
and its position, rotation and scale siblings (`ecs/component/scene/world_transform.h`)
read what this frame's Transform stage resolved. A `Transform` written after that stage
(the editor's gizmo, say) shows in the first at once and, on a parented entity, in the
others only next frame.

## How it works inside

`HierarchySystem` resolves a level at a time from the roots, each level under
`parallelFor`; an entity with no `Transform` keeps its matrix, and its children compose
onto it. There is no dirty flag - every hierarchical entity resolves every frame,
because a flag would be a second statement of whether a matrix is current
([decisions.md](decisions.md#what-has-been-decided)).

## Key files

- `src/engine/ecs/hierarchy_operations.h` - `HierarchyOperations`, every link and walk
- `src/engine/system/hierarchy/hierarchy_system.h` - the per-frame resolve
- `src/engine/ecs/component/scene/hierarchy.h` - `Hierarchy`
- `src/engine/ecs/component/scene/world_transform.h` - `WorldTransform` and the readers
