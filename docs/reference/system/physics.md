# Physics

Fixed-step rigid-body dynamics over box colliders: integrate velocities, detect
and resolve pairwise collisions with a sequential-impulse (PGS) solver, and write
the resulting poses back to `Transform`.

`PhysicsSystem` runs in `SystemStage::Simulation`, **after** `AnimationSystem`
and **before** `HierarchySystem`, so physics-updated transforms propagate into
`WorldTransform` the same frame. All work happens in `fixedUpdate()` against
`ctx.fixedDeltaTime`; `update()` is a no-op. It opts into `fixedUpdate`.

## Key files

- `src/engine/system/physics/physics_system.h/.cpp` - the system (gather, broadphase, narrowphase, solve, integrate)
- `src/engine/system/physics/collision/contact.h` - `Contact`, `ContactManifold`, `MAX_CONTACTS_PER_MANIFOLD`
- `src/engine/system/physics/collision/narrowphase.h/.cpp` - `contactBoxes` (oriented box-vs-box)
- `src/engine/system/physics/collision/solver.h/.cpp` - `PhysicsBody`, `SolverParams`, `solveContacts`
- `src/engine/system/physics/inertia.h` - box inverse-inertia + world-space rotation helpers
- `src/engine/system/physics/collider_fit.h/.cpp` - `fitBoxesToMesh` ("Fit to Mesh")
- `src/engine/system/physics/physics_events.h` - `CollisionEvent`, `TriggerEvent`
- `src/engine/ecs/component/rigidbody.h`, `collider.h`, `physics_world.h` - the components

## Components

All three are plain data structs; see [ecs.md](../ecs.md) for the field tables.

- **`Rigidbody`** - dynamics state: `linearVelocity`, `angularVelocity`, `mass`
  (cached `inverseMass`), `linearDamping` / `angularDamping`, `restitution`,
  `friction`, `gravityScale`, and the `isKinematic` / `isStatic` /
  `freezeRotation` / `sleeping` flags. `inverseMass == 0` means static/kinematic
  (infinite mass): forces never move it, but it is an immovable wall in
  collisions. `freezeRotation` zeroes the inverse inertia of a dynamic body so
  contacts can never torque it - the character-controller case: the body
  translates under the solver while its orientation stays script-owned.
  `invInertiaLocal` is re-derived from `mass` + `Collider` every tick, so
  editing either takes effect without an "apply" step.
- **`Collider`** - one or more `ColliderBox` parts (`center`, `halfExtents`),
  evaluated in the entity's `Transform` frame. The narrowphase is **box-vs-box
  only**, run per child-box pair. `isTrigger` makes it generate events without an
  impulse response. `enabled = false` makes the collider inert - it gets no
  broadphase entry, produces no contacts or events, and the editor's collider
  overlay skips it (for pooled/phased objects that toggle collision without
  component churn). The solver ignores `Transform` scale - "Fit to Mesh" bakes
  scale into the box centres and half-extents.
- **Scene physics settings** - `gravity` and `solverIterations` live on the
  scene-global `Environment` (`ecs/environment.h`), serialized with the scene.
  `PhysicsSystem` reads them each tick, so gravity persists with the scene and
  can differ per scene.

## Per-tick flow (`fixedUpdate`)

```
PhysicsSystem::fixedUpdate(ctx)
  1. Read the physics settings off the scene's Environment.
  2. Gather: snapshot every live Rigidbody + Transform into PhysicsBody solver
     state; build a ColliderProxy (world AABB + sub-boxes) per body with a Collider.
     Re-derive inverseMass + invInertiaLocal. Sleeping / immovable bodies enter
     the solver with invMass 0.
  3. Integrate forces -> velocities: gravity * gravityScale, then damping
     (skips sleeping / static / kinematic).
  4. Broadphase: sort-and-sweep on X; AABB-overlap surviving pairs (static-vs-static culled).
  5. Narrowphase: contactBoxes per child-box pair -> ContactManifolds (up to
     MAX_CONTACTS_PER_MANIFOLD = 4 points each). Enqueue CollisionEvent / TriggerEvent.
  6. Wake sleepers struck by a faster body.
  7. solveContacts: PGS iterations of normal + friction impulses with restitution,
     then a split-impulse pass for penetration correction.
  8. Integrate velocities -> pose, write Transform back, update sleep state,
     markDirty so HierarchySystem re-resolves WorldTransform.
```

Bodies are assumed to be **hierarchy roots**, so an entity's local `Transform`
equals its world pose. Parenting a physics body is unsupported in this pass;
children parented *to* a body still follow it (the `markDirty` cascade carries
into the subtree).

## The solver

`solveContacts` works against `PhysicsBody` (per-tick state decoupled from the
Scene, addressed by index from each manifold), not the components directly. It
runs `SolverParams::iterations` passes of normal + Coulomb-friction impulses with
restitution, then a separate **split-impulse** pass that fills
`pseudoLinear` / `pseudoAngular`. Those pseudo-velocities are integrated into the
pose alongside the real velocities but never persisted, so penetration is removed
without injecting energy.

`SolverParams`: `iterations` (PGS passes, from `Environment::solverIterations`),
`dt`, `baumgarte` (position-correction stiffness), `penetrationSlop` (allowed
overlap before correction), `restitutionThreshold` (below this approach speed,
ignore bounce). `Contact` accumulates `normalImpulse` / `tangentImpulse` across a
tick's iterations so successive passes converge instead of fighting.

Inertia is approximated as a solid box of the collider's overall local extent -
exact per-part inertia isn't worth it for gameplay (`inertia.h`).

## Sleeping

A body that rests (low linear + angular speed while in contact) for
`SLEEP_DELAY` (0.5 s) goes to sleep and stops simulating; a sleeping body wakes
when struck by a body above `WAKE_SPEED_SQ`. Sleeping dynamic bodies still
generate contacts (they are not treated as permanently static), so stacks stay
supported. `sleeping` / `sleepTimer` are runtime-only and not serialized.

## Events

```cpp
struct CollisionEvent { EntityId a, b; glm::vec3 point, normal; };  // normal points a -> b
struct TriggerEvent   { EntityId trigger, other; };
```

Both are **enqueued** (not emitted), so listeners fire on the next `EventBus`
flush, never mid-solve. They fire once per overlapping pair per fixed tick *while
the overlap lasts* - there is no enter/exit edge detection yet. Triggers are
queried, not resolved, so they only produce events. Gameplay reacts either by
subscribing to the events directly or through the behavior `onCollision` /
`onTrigger` hooks (`BehaviorSystem` subscribes and forwards them; see
[scripting.md](scripting.md)).

## Editor integration

- The World inspector's **Physics** card edits the Environment's gravity and
  solver iterations (undoable like the other World cards).
- The inspector's Collider section offers **Fit to Mesh**, which calls
  `fitBoxesToMesh` to approximate the entity's mesh with a grid of boxes
  (`detail` clamped to `[1, COLLIDER_FIT_MAX_DETAIL]`; `detail == 1` is the
  scaled bounds box). It never returns empty - a degenerate or non-watertight
  mesh falls back to a single bounds-sized box.
- `Rigidbody`, `Collider`, and `PhysicsWorld` round-trip with the scene; see
  [io.md](io.md).
