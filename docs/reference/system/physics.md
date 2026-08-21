# Physics

Fixed-step rigid-body dynamics over box and capsule colliders: integrate
velocities, detect and resolve pairwise collisions with a sequential-impulse
(PGS) solver, and write the resulting poses back to `Transform`.

`PhysicsSystem` runs in `SystemStage::Simulation`, **after** `AnimationSystem`
and **before** `HierarchySystem`, so physics-updated transforms propagate into
`WorldTransform` the same frame. All work happens in `fixedUpdate()` against
`ctx.clock.getFixedStep()`; `update()` is a no-op. It opts into `fixedUpdate`.

`CharacterControllerSystem` runs in the same stage, **immediately after**
`PhysicsSystem`, so it reads that tick's freshly written support outputs.

## Key files

- `src/engine/system/physics/physics_system.h/.cpp` - the system (gather, broadphase, narrowphase, solve, integrate)
- `src/engine/system/physics/character_controller_system.h/.cpp` - `CharacterControllerSystem`
- `src/engine/system/physics/collision/contact.h` - `Contact`, `ContactManifold`, `MAX_CONTACTS_PER_MANIFOLD`
- `src/engine/system/physics/collision/narrowphase.h/.cpp` - `BoxShape`, `CapsuleShape`, and the three contact routines
- `src/engine/system/physics/collision/solver.h/.cpp` - `PhysicsBody`, `SolverParams`, `solveContacts`
- `src/engine/system/physics/inertia.h` - box / capsule inertia + world-space rotation helpers
- `src/engine/system/physics/collider_fit.h/.cpp` - `fitBoxesToMesh` ("Fit to Mesh")
- `src/engine/system/physics/physics_events.h` - `CollisionEvent`, `TriggerEvent`
- `src/engine/ecs/component/physics/rigidbody.h`, `collider.h`, `character_controller.h` - the components

## Components

All of them are plain data structs; see [ecs.md](../ecs.md) for the field tables.

- **`Rigidbody`** - dynamics state: `linearVelocity`, `angularVelocity`, `mass`,
  `linearDamping` / `angularDamping`, `restitution`, `friction`, `gravityScale`,
  and the `isKinematic` / `isStatic` / `freezeRotation` / `sleeping` flags. A
  static or kinematic body has infinite mass: forces never move it, but it is an
  immovable wall in collisions. `freezeRotation` zeroes the inverse inertia of a
  dynamic body so contacts can never torque it - the character-controller case:
  the body translates under the solver while its orientation stays script-owned.
  The derived mass properties (inverse mass, body-local inverse inertia) are not
  stored on the component: `PhysicsSystem` re-derives them from `mass` +
  `Collider` into its per-tick `BodyFrame`, so editing either takes effect
  without an "apply" step.

  It also carries three **outputs**, written by `writeback` and never read by the
  system that writes them: `supported` (a resolved, non-trigger contact reached
  this body this tick), `supportNormal` (the most **upward** of those normals)
  and `blockNormal` (the most **horizontal** of them). Both normals are the
  surface's as it acts on *this* body, so the two bodies of one contact see
  opposite directions.

  Two reductions rather than one because they answer different questions in the
  same tick: a character on flat ground pressed against a wall is *held* by the
  floor and *blocked* by the wall, and one normal cannot say both. They are
  generic on purpose. "Am I standing on something, and how steep is it" is what a
  controller, a footstep sound and a landing animation ask; "what am I pressed
  against, and which way does it push" is what a wall slide, a scrape effect and
  a ledge grab ask. Answering both here is what keeps `PhysicsSystem` from ever
  learning what a character is.

  A body that touched nothing reports world up for both. For `blockNormal` that
  doubles as "nothing is in the way" without a second flag: world up has no
  horizontal component, and a normal with no horizontal direction cannot deflect
  anything, so the reader that projects against it is already the reader that
  ignores it. Runtime-only, like `sleeping`: not serialized.
- **`Collider`** - one or more `ColliderPart`s, evaluated in the entity's
  `Transform` frame. Each part carries a `shape` tag (`ColliderShape::Box` or
  `Capsule`) and the fields for both: `center` + `halfExtents` for a box,
  `center` + `radius` + `halfHeight` for a capsule. `isTrigger` makes it generate
  events without an impulse response. `enabled = false` makes the collider inert -
  it gets no broadphase entry, produces no contacts or events, and the editor's
  collider overlay skips it (for pooled/phased objects that toggle collision
  without component churn). The solver ignores `Transform` scale - "Fit to Mesh"
  bakes scale into the box centres and half-extents.

  A capsule's segment runs along the entity's local **+Y** for `halfHeight`
  either side of `center`, swept by `radius`, so its total height is
  `2 * (halfHeight + radius)`. `halfHeight == 0` is a sphere, and every routine
  handles it without a special case. Capsules are what characters wear: a box
  catches on every seam between two floor boxes, and a capsule's round side
  slides over them.
- **`CharacterController`** - `moveInput` (world-space desired horizontal
  velocity, written by gameplay), `jumpRequested`, the tuning (`jumpSpeed`,
  `acceleration`, `airControl`, `maxSlopeAngle`), and the read-only `grounded` /
  `groundNormal`. Only the four tuning fields are serialized: the rest is
  per-tick traffic, and a scene row holding a half-consumed jump request would
  replay it on load.
- **Scene physics settings** - `gravity` and `solverIterations` live on the
  scene-global `Environment` (`ecs/environment.h`), serialized with the scene.
  `PhysicsSystem` reads them each tick, so gravity persists with the scene and
  can differ per scene.

## Per-tick flow (`fixedUpdate`)

```
PhysicsSystem::fixedUpdate(ctx)
  1. Read the physics settings off the scene's Environment.
  2. Gather: snapshot every live Rigidbody + Transform into PhysicsBody solver
     state; build a ColliderProxy (world AABB + parts span) per body with a Collider.
     Re-derive the tick's inverse mass + local inverse inertia onto the BodyFrame.
     Sleeping / immovable bodies enter the solver with invMass 0.
  3. Integrate forces -> velocities: gravity * gravityScale, then damping
     (skips sleeping / static / kinematic).
  4. Broadphase: sort-and-sweep on X; AABB-overlap surviving pairs (static-vs-static culled).
  5. Narrowphase: one contact routine per part pair -> ContactManifolds (up to
     MAX_CONTACTS_PER_MANIFOLD = 4 points each). Reduce each body's normals into
     its BodyContacts (most upward, most horizontal). Enqueue CollisionEvent /
     TriggerEvent.
  6. Wake sleepers struck by a faster body.
  7. solveContacts: PGS iterations of normal + friction impulses with restitution,
     then a split-impulse pass for penetration correction.
  8. Integrate velocities -> pose, write Transform back, publish the BodyContacts
     onto the Rigidbody, update sleep state.
     HierarchySystem re-resolves WorldTransform later in the same frame.
```

A **hierarchy root**'s local `Transform` is already its world pose, so
`gatherBodies` reads it straight. A **parented** body takes its world pose from
its `WorldTransform` and records the parent's frame alongside it; writeback maps
the solved world pose back through that frame into the local `Transform`. Either
way, children parented *to* a body follow it, because HierarchySystem rebuilds
the whole subtree in the Transform stage that follows.

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

## The narrowphase

Each proxy's parts are placed in world space once per pair and sorted into **two
monomorphic arrays**, one of `BoxShape` and one of `CapsuleShape`. The pair loops
are quadratic, so the shape test happens once per part rather than inside them.
Four loops run: box-box, A's capsules against B's boxes, B's capsules against A's
boxes, and capsule-capsule.

Every routine honours the same contract - **normal A -> B, positive penetration,
at most `MAX_CONTACTS_PER_MANIFOLD` points** - so the solver, broadphase,
`wakeOnImpact`, sleeping, events and writeback are shape-blind. `ContactManifold`
addresses bodies by tick-snapshot index and knows nothing about shapes at all.

- `contactBoxes` - SAT over 15 axes, then a face clip or an edge-edge point.
- `contactCapsuleBox` - runs in the box's local frame, where "closest point on
  the box" is a clamp. An alternating projection closes the segment onto the box;
  a positive gap gives the normal directly, and a segment that reaches inside
  falls back to the nearest face (the same minimum translation the box SAT
  settles on). A capsule lying **flat** on a face gets **two** points, clipped to
  the face - resolved from one point it would roll off it forever.
  The projection runs until its step stops moving, and `MAX_PROJECTION_PASSES`
  is only a runaway guard. It has to be, and generously: a run cut short answers
  a distance *larger* than the real one, which the radius test reads as no
  contact at all. The average is about seven passes; a segment lying nearly
  tangent to a face wants hundreds.
- `contactCapsuleCapsule` - closest approach of the two segments, one point
  midway between the two surfaces. Coincident axes fall back to a direction
  perpendicular to the first capsule's own axis, never along it.

Capsule-vs-box is **not symmetric**: when the capsule is body B the routine runs
capsule-first and the caller negates the normals back to A -> B.

## Inertia

Inertia is approximated as a solid box of the collider's overall local extent -
exact per-part inertia isn't worth it for gameplay (`inertia.h`). The one
exception is a collider that is a **single capsule**, which gets
`capsuleInertiaLocal` (a cylinder plus two hemispherical caps, volume-weighted).
That is the shape the box approximation is worst about: an upright capsule spins
about its own axis several times more freely than the box around it, and that
difference is exactly what a graze against a character tests. Either way the
tensor is parallel-axis-shifted from the collider centre to the entity origin,
where the solver measures its contact arms.

## The character controller

`CharacterControllerSystem` turns `moveInput` into velocity on the `Rigidbody`,
once per fixed tick, after `PhysicsSystem`. Only the velocity it writes is a tick
late, and a tick of steering lag is imperceptible; fresh contacts are the half
that has to be exact, because landing, stepping off a ledge, refusing a slope and
sliding along a wall all turn on them.

Per tick, per controller:

1. `grounded = rb.supported && dot(rb.supportNormal, up) >= cos(maxSlopeAngle)`,
   and `groundNormal` mirrors the surface (world up when airborne). A wall is a
   resolved contact too, so touching is not the same question as standing.
2. Wake the body if there is input and it fell asleep - a sleeping body has its
   velocity zeroed by writeback, so anything asking it to move must wake it.
3. Deflect the target along `blockNormal` if that surface is steeper than
   `maxSlopeAngle`, so it runs along the obstacle instead of into it (below).
4. Steer toward the deflected target. Grounded, it is projected onto the ground
   plane, so a ramp neither launches at the top nor drags back down; airborne,
   only the horizontal is steered (at `airControl` of the rate) and the vertical
   is gravity's alone. Either way the step is **capped** at `acceleration * dt`
   rather than approached exponentially, so the number means m/s^2 at every
   speed.
5. `jumpRequested && grounded` sets `linearVelocity.y = jumpSpeed` and clears
   `grounded` on the spot; the request is consumed either way.

It writes velocity, **never position**: the solver owns the pose, so a controller
cannot teleport a character through a wall, and friction, restitution,
penetration recovery and sleeping all keep working underneath it. Two silent
misconfigurations are named once each - no enabled capsule collider (it will
never ground) and `freezeRotation` off (contacts will topple it).

### Sliding along walls

Step 3 is the one thing the controller deliberately does **not** leave to the
solver. Aim a walk into a wall and the solver resolves it perfectly well: the
normal impulse removes the motion into the face, and Coulomb friction decides
what happens to the rest. That is the problem. Friction is capped at
`combinedFriction * normalImpulse`, so a character glides only while the combined
friction is below `tan(incidence)`. Whether a character slides or snags was
decided by two material numbers nobody picked for that purpose. With the engine's
defaults on both bodies (`sqrt(0.5*0.5) = 0.5`, against `tan 25 = 0.466`), a walk
25 degrees off the wall normal produced **0.000 m/s** along the face. Not a slow
slide: a dead stop.

So the target is turned before it is steered toward. `deflectAlongBlock` removes
whatever part of it pushes into `blockNormal`, and a character that no longer
presses on the wall generates no normal force for friction to scale against.
Measured at a 1.9 m/s walk, before -> after, against the speed the input actually
commands along the face:

| Incidence off the wall normal | 10 deg | 25 deg | 45 deg | 60 deg | 80 deg |
|---|---|---|---|---|---|
| Commanded along the face | 0.330 | 0.803 | 1.344 | 1.645 | 1.871 |
| Slide, before | 0.000 | 0.000 | 0.563 | 1.069 | 1.790 |
| Slide, after  | 0.248 | 0.721 | 1.262 | 1.563 | 1.788 |

The remaining shortfall is the *floor's* drag, which acts on any walk; at 80
degrees, where friction was never the gate, nothing changes. What does change is
that the **wall's** own `friction` across 0 -> 1 now moves the slide by **0.00%**.
Sliding is geometry, not materials.

Two details carry the design:

- **Only a surface steeper than `maxSlopeAngle` deflects.** A walkable one is
  ground, and step 4 already follows it; deflecting against it as well would
  strip the downhill component and leave the character unable to walk *down* a
  ramp.
- **The deflection is taken flat** - only the horizontal part of `blockNormal`
  turns the target. A wall that deflected the target *upward* would be a ramp,
  and the character would climb whatever it walked into, which is precisely what
  `maxSlopeAngle` exists to forbid. Measured: a 70-degree slope is not climbed.

The deflection is free where there is nothing to deflect against. A body on level
floor gets `blockNormal` == world up, which is walkable at every slope limit, so
the first line of the helper returns the target untouched - no contact test, no
branch on `supported`.

### Steps

A step is met by the capsule's bottom cap, and what happens is decided entirely
by the contact normal that edge produces. The normal's upward component at a step
of height `h` is `1 - h/radius`, so the capsule **rolls over** any step up to:

```
h <= radius * (1 - cos(maxSlopeAngle))      // 0.107 m at radius 0.30, 50 degrees
```

Below that limit the edge is a walkable surface: the character grounds on it and
climbs it like the ramp it geometrically is. Above it the edge is a wall, and the
character is stopped - there is still **no step-up** (see below).

What a step must never do is strand the character. Above the roll-over limit it
used to: riding the edge lifted the capsule off the floor, `supportNormal` became
the edge's, and `grounded` stayed **false for as long as forward was held** -
measured at 369 consecutive ticks (6.1 s, the whole run) into a 0.15 m block, with
no jumping and grounded-driven animation playing "falling" while the character
stood still. The wall deflection ends it as a side effect: the character stops
pushing into the edge, settles back onto the floor, and grounding now lapses for
at most **2 ticks** (0.033 s) at both 0.15 m and 0.20 m. It is stopped by the
step, which is honest, rather than hovering on it, which was not.

A **System driving a component**, not a `Behavior`, because the thing that writes
`moveInput` changes and the thing that reads it should not: gameplay writes it
today, a nav agent writes it in 1.8, and an engine system cannot address a
hot-reloadable game behavior.

**Deliberately partial:** velocity-driven, with no step-up (that needs a
shapecast query the engine does not have), no crouch, no moving platforms and no
runtime capsule resize. Wall projection was never on this list and should not be:
a controller that cannot slide along a wall is one that is not doing its job.
Step-up stays on it - a character stopped by a 0.15 m kerb is a limitation, and
one that is honest about it now that grounding survives the contact.

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
- The viewport's collider overlay draws each part as the shape it is: a wire box
  or a wire capsule (`wireCapsule` in `src/editor/overlays/wire_draw.h`), placed
  the way the solver places it - world position + rotation, no scale.
- The inspector's Collider section offers **Fit to Mesh**, which calls
  `fitBoxesToMesh` to approximate the entity's mesh with a grid of boxes
  (`detail` clamped to `[1, COLLIDER_FIT_MAX_DETAIL]`; `detail == 1` is the
  scaled bounds box). It never returns empty - a degenerate or non-watertight
  mesh falls back to a single bounds-sized box. Every part it produces is a box:
  fitting capsules to a mesh is a medial-axis problem, not a scanline, and is not
  attempted.
- `Rigidbody`, `Collider`, and `PhysicsWorld` round-trip with the scene; see
  [io.md](io.md).
