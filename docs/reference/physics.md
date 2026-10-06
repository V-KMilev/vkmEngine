# Physics

Fixed-step rigid-body dynamics over box, capsule and triangle-mesh colliders: integrate
velocities, detect and resolve pairwise collisions with a sequential-impulse
(PGS) solver, and write the resulting poses back to `Transform`.

`PhysicsSystem` runs in `SystemStage::Simulation`, **after** `AnimationSystem`
and **before** `HierarchySystem`, so physics-updated transforms propagate into
`WorldTransform` the same frame. All work happens in `fixedUpdate()` against
`ctx.clock.getFixedStep()`; `update()` is a no-op. It opts into `fixedUpdate`.

`CharacterControllerSystem` runs in the same stage, **immediately after**
`PhysicsSystem`, so it reads that tick's freshly written support outputs.

## Components

All of them are plain data structs; see [ecs.md](ecs.md) for the field tables.

- **`Rigidbody`** - dynamics state: `linearVelocity`, `angularVelocity`, its
  `motion` (`RigidbodyMotion::Dynamic`, `Kinematic` or `Static`), `mass`,
  `linearDamping` / `angularDamping`, `restitution`, `friction`, `gravityScale`,
  the `layer` / `collidesWith` collision masks, and the `freezeRotation` /
  `canSleep` / `sleeping` flags. A static or kinematic body has infinite mass:
  forces never move it, but it is an immovable wall in collisions. Neither is
  integrated from its velocities at all - position or orientation - so a
  kinematic body's pose is the project's to write. The velocities still matter
  to a contact, which is how a kinematic platform pushes what stands on it, so a
  body that stops being driven should zero them. `mass` is read only on a
  dynamic body and must be positive: the gather reports one that is not and
  holds it still. `freezeRotation` zeroes the inverse inertia of a
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

  Two reductions, because a character on flat ground pressed against a wall is
  *held* by the floor and *blocked* by the wall, and one normal cannot say both.
  Neither knows who asks: "am I standing on something, and how steep is it" is a
  controller's, a footstep sound's or a landing animation's question; "what am I
  pressed against, and which way does it push" is a wall slide's, a scrape
  effect's or a ledge grab's. `PhysicsSystem` never learns what a character is.

  A body that touched nothing reports world up for both. For `blockNormal` that
  doubles as "nothing is in the way" without a second flag: world up has no
  horizontal component, and a normal with no horizontal direction cannot deflect
  anything, so the reader that projects against it is already the reader that
  ignores it. Runtime-only, like `sleeping`: not serialized.
- **`Collider`** - one or more `ColliderPart`s, evaluated in the entity's
  `Transform` frame. Each part carries a `shape` tag (`ColliderShape::Box`,
  `Capsule` or `Mesh`) and the fields for each: `center` + `halfExtents` for a
  box, `center` + `radius` + `halfHeight` for a capsule, and `center` + `mesh` +
  `meshScale` for a mesh part - the mesh by handle, saved by name, so a scene
  file never holds the triangles. Those are built from the mesh as it is now
  into the collider's `meshPoints` and the `meshNodes` tree over them, derived
  session state that `syncMeshCollider` rebuilds on load, on an inspector edit
  and on any tick the mesh's uid or version or the part's scale differs from
  what they were built from (`meshBuiltFrom`). They live on the collider
  because a query is handed the scene alone. One mesh part per collider. `isTrigger` makes it generate
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
  `acceleration`, `airControl`, `maxSlopeAngle`, `stepHeight`), the read-only
  `grounded` / `groundNormal`, and the state of a step climb in progress
  (`stepping`, `stepTargetY`, `stepTime`). Only the tuning is serialized: the rest
  is per-tick traffic, and a scene row holding a half-consumed jump request would
  replay it on load.
- **Scene physics settings** - `gravity` and `solverIterations` live in the
  scene-global `PhysicsSettings` (`ecs/physics_settings.h`), reached through
  `Scene::physics()` and serialized with the scene in a block of its own. It sits
  *beside* the `Environment` rather than inside it: what a world is lit by and
  what it falls at are unrelated. `PhysicsSystem` reads them each tick, so
  gravity persists with the scene and can differ per scene.

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
   Derived only for a character this end simulates, like everything after it.
   One the authority decides keeps the `grounded` the authority sent - the
   `CharacterController` codec replicates it - so a character this end is only
   told about still reports standing, and every game driving an animation off it
   shows it walking rather than falling.
2. Wake the body if there is input and it fell asleep - a sleeping body has its
   velocity zeroed by writeback, so anything asking it to move must wake it.
3. Unless a step climb is in progress, deflect the target along `blockNormal` if
   that surface is steeper than `maxSlopeAngle`, so it runs along the obstacle
   instead of into it (below).
4. Steer toward the deflected target. Grounded, it is projected onto the ground
   plane, so a ramp neither launches at the top nor drags back down; airborne,
   only the horizontal is steered (at `airControl` of the rate) and the vertical
   is gravity's alone. Either way the step is **capped** at `acceleration * dt`
   rather than approached exponentially, so the number means m/s^2 at every
   speed.
5. `jumpRequested && grounded` sets `linearVelocity.y = jumpSpeed`, clears
   `grounded` on the spot and ends any step climb, which would otherwise set the
   vertical back to its own rate next tick; the request is consumed either way.

It writes velocity, **never position**: the solver owns the pose, so a controller
cannot teleport a character through a wall, and friction, restitution,
penetration recovery and sleeping all keep working underneath it. Two silent
misconfigurations are named once each - no enabled capsule collider (it will
not climb steps) and `freezeRotation` off (contacts will topple it).

### Sliding along walls

Step 3 is the one thing the controller deliberately does **not** leave to the
solver. Aim a walk into a wall and the solver resolves it perfectly well: the
normal impulse removes the motion into the face, and Coulomb friction decides
what happens to the rest. That is the problem. Friction is capped at the
combined friction (`combineFriction`, the geometric mean of the two bodies')
times the normal impulse, so a character glides only while the combined
friction is below `tan(incidence)`. Left to the solver, whether a character
slides or snags would be decided by two material numbers nobody picked for that
purpose. With the engine's defaults on both bodies (`sqrt(0.5*0.5) = 0.5`,
against `tan 25 = 0.466`), the solver alone gives a walk 25 degrees off the wall
normal **0.000 m/s** along the face. Not a slow slide: a dead stop.

So the target is turned before it is steered toward. `deflectAlongBlock` removes
whatever part of it pushes into `blockNormal`, and a character that no longer
presses on the wall generates no normal force for friction to scale against.
Measured at a 1.9 m/s walk, the solver alone against the deflected target, beside
the speed the input actually commands along the face:

| Incidence off the wall normal | 10 deg | 25 deg | 45 deg | 60 deg | 80 deg |
|---|---|---|---|---|---|
| Commanded along the face | 0.330 | 0.803 | 1.344 | 1.645 | 1.871 |
| Slide, solver alone | 0.000 | 0.000 | 0.563 | 1.069 | 1.790 |
| Slide, deflected    | 0.248 | 0.721 | 1.262 | 1.563 | 1.788 |

The remaining shortfall is the *floor's* drag, which acts on any walk; at 80
degrees, where friction is not the gate, the two agree. And with the deflection
the **wall's** own `friction` across 0 -> 1 moves the slide by **0.00%**.
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
climbs it like the ramp it geometrically is. Above it the edge is a wall - and if
it is lower than `stepHeight`, the controller mounts it instead of stopping (see
step-up below).

What a step must never do is strand the character. A step above the roll-over
limit that the controller does not mount - taller than `stepHeight`, or any step
while `stepHeight` is zero - is a wall. Riding its edge would lift the capsule
off the floor, make `supportNormal` the edge's and hold `grounded` false for as
long as forward is held. The wall deflection prevents it: the character stops
pushing into the edge and settles back onto the floor, stopped by the step
rather than hovering on it.

**A System driving a component**, not a `Behavior`, because the thing that writes
`moveInput` changes and the thing that reads it should not: gameplay writes it,
so may any other driver, and an engine system cannot address a hot-reloadable
game behavior.

**Partial by design:** velocity-driven, with no crouch, no moving platforms
and no runtime capsule resize.

Step-up is built on the sweep queries: `findStep` probes with two spherecasts - clearance at step height, then straight
down for a walkable landing - through the body's own collision mask, so a
character's ragdoll bones never block their own stairs. A climb ends by arriving
at the measured height, by the character no longer asking, or by its deadline;
never because the step stopped blocking, since rising is exactly what un-blocks
a riser.

## Sleeping

A body that rests for `SLEEP_DELAY` (0.5 s) goes to sleep and stops simulating -
together with every dynamic body it touches or is jointed to, since an island
sleeps whole or not at all, and only while something outside the island holds
it: a contact with a body that is not in it, or a joint to one. That is the
island's question rather than each body's, because a lamp hanging from a post
touches nothing, and a ragdoll's hips are held off the floor by its legs.

It wakes whole, too. A sleeper is immovable to the solver, so an island wakes
when anything that could push it touches any member: an awake dynamic body at
any speed, or a kinematic one being driven. A static body never wakes what
rests on it. A sleeping island that nothing outside holds any more wakes as
well - whatever it rested on is gone - and falls on that same tick. Every
member wakes on the same tick, before the forces and the solve: a ball landing on a sleeping
tower moves the whole tower, where waking only what the ball touched would leave
the box under it a wall for that tick. Sleeping dynamic bodies still generate
contacts (they are not treated as permanently static), so stacks stay
supported. `sleeping` / `sleepTimer` are runtime-only and not serialized, and a
replayed tick adds nothing to `sleepTimer`: it was counted when it was lived. Code
that wakes a body calls `Rigidbody::wake`, which clears both: a cleared flag over
a timer that still reads a long rest falls asleep again at the end of the tick.

A body rests while no point of it moves faster than
`SLEEP_SPEED` (5 cm/s): the centre's speed plus the spin times the body's reach,
the farthest its collider extends from its origin. One bound on the point speed
rather than a linear and an angular threshold apart, because a tall box tipped
just past balance starts to fall by turning slowly about an edge - each half
would read as rest, and the box would sleep leaning fifteen degrees while its
top edge moved at the speed the thresholds were meant to catch.

## Events

```cpp
enum class ContactPhase : uint8_t { Began, Stayed, Ended };

struct CollisionEvent { EntityId a, b; ContactPhase phase; glm::vec3 point, normal; };  // normal points a -> b
struct TriggerEvent   { EntityId trigger, other; ContactPhase phase; };
```

A contact is reported as it changes - Unity's enter / stay / exit, where Box2D
and Godot report the two edges alone: **Began** on the first tick a pair
touches, **Stayed** on each later tick it still does, **Ended** on the first
tick it does not. A collision's `a` is the
body with the lower entity slot, so one pair is named the same way round - and
its normal runs the same way - on every tick it lasts. `point` and `normal` are
the first contact found, on Began and Stayed; an Ended contact touches nowhere
and carries the pair alone. A trigger pair sends one `TriggerEvent` per trigger
in it, so with two triggers each learns who entered it.

- **Ended is sent however the contact ended**: the two separating, either one
  destroyed, its collider disabled, its layer changed, its `isTrigger` flipped (a
  pair whose trigger flags change is a different contact - the old one ends and
  the new one begins). So on Ended either id may name an entity that is no
  longer alive, and a new entity that takes a freed slot begins a contact of its
  own. A behavior on the destroyed entity hears nothing - it has had
  `onDestroy` - and the survivor hears it leave.
- **A resting pair is silent but still touching.** While both bodies rest -
  asleep, or static - the pair sends no Stayed: nothing about it can change until
  something wakes it, and a settled pile would otherwise report every contact
  in it every tick. It sends no Ended either, so waking it does not begin it
  again. A kinematic body is driven, so it never rests.
- **Only a live tick reports.** A tick a client replays reported its contacts
  the first time it ran, so a replay neither sends events nor moves what the
  next live tick is compared with - a contact the replay briefly lost is not
  reported beginning again. A replaced world (`Scene::epoch`) drops what touched
  in the old one unreported: its ids name the new world's entities now.
- **The order is the world's**: by entity slot pair, then generation, so every
  machine reports one tick's contacts in the same order wherever the bodies
  stand.

Both are **enqueued** (not emitted), so listeners fire on the next `EventBus`
flush, never mid-solve. Triggers are queried, not resolved, so they only produce
events, and a trigger pair stops at its first contact. A trigger senses what
moves - dynamic and kinematic bodies, another trigger that moves - and never a
static body: paired with the level it sits in, it would report the floor under
it as long as the level lasted. Gameplay reacts either by subscribing to the
events directly or through the behavior hooks - `onCollisionEnter` /
`onCollisionStay` / `onCollisionExit` and `onTriggerEnter` / `onTriggerStay` /
`onTriggerExit` (`BehaviorSystem` subscribes and forwards them by phase; see
[scripting.md](scripting.md)).

What touched is kept apart from the solver's `ContactCache`, as `TrackedPair`
lists on `PhysicsSystem` - the pairs touching this live tick and the last. They
answer a different question about different pairs: a trigger pair makes no
manifold and is never cached, and a contact that ended because an entity was
destroyed must still name it by its full id, where the cache's slot key would
name whatever took the slot. They are event state, not simulation state, which
is why a replay leaves them alone.

## Editor integration

- The World inspector's **Physics** card edits `PhysicsSettings`' gravity and
  solver iterations (undoable like the other World cards).
- The viewport's collider overlay draws each part as the shape it is: a wire
  box, a wire capsule, or a mesh's own triangles (decimated past a cap - a
  wireframe says where a surface is, not every edge), placed the way the solver
  places it - world position + rotation, no scale. Joints draw under the same
  toggle: both anchors, the line between, and the link glyph at the midpoint.
- The inspector's Collider section offers **Fit to Mesh**, which calls
  `fitBoxesToMesh` to approximate the entity's mesh with a grid of boxes
  (`detail` clamped to `[1, COLLIDER_FIT_MAX_DETAIL]`; `detail == 1` is the
  scaled bounds box). It never returns empty - a mesh with flat bounds, no
  whole triangle, an index past its vertices or no inside span at all falls back
  to a single bounds-sized box. Every part it produces is a box:
  fitting capsules to a mesh is a medial-axis problem, not a scanline, and is not
  attempted.
- `Rigidbody`, `Collider`, `CharacterController`, `Joint` and `Ragdoll`
  round-trip with the scene as components - authored fields only, since every
  runtime output on them (`sleeping`, the two contact normals, `grounded`) is
  rebuilt each tick. Entity references travel as slots and are recovered when
  the scene is whole; a prefab rewrites them as local indices instead, so an
  instance's joints tie to its own entities wherever it lands. The
  scene-global `PhysicsSettings` round-trips beside them as the file's own
  `physics` block rather than as a component on anything. See [io.md](io.md).

---

## How it works inside

Everything above is what a project writes. What follows is how the
engine answers it, for whoever maintains that half.

### Per-tick flow (`fixedUpdate`)

```
PhysicsSystem::fixedUpdate(ctx)
  1. Read the physics settings off the scene's PhysicsSettings.
  2. Gather: snapshot every live Rigidbody + Transform into PhysicsBody solver
     state; build a ColliderProxy (world AABB + parts span) per body with a Collider.
     The walk starts from the Rigidbody storage, so the pairing is required in
     both directions: a Collider without a Rigidbody is in no broadphase at all,
     and a dynamic Rigidbody without a Collider integrates gravity with nothing
     to land on. The inspector names each on its own card - see
     [the editor](editor.md#a-card-names-what-its-component-is-waiting-for).
     Re-derive the tick's inverse mass + local inverse inertia onto the BodyFrame.
     Sleeping / immovable bodies enter the solver with invMass 0.
  3. Gather joints: resolve each Joint to two body indices and world lever arms,
     seeded with the impulses it held last tick, and resolve the angle a hold
     keeps. Before the pairing, because a pair a joint holds together makes no
     contact unless the joint says so.
  4. Broadphase: sort-and-sweep on X; AABB-overlap surviving pairs (two immovable
     non-triggers culled, and a trigger against a static body).
  5. Narrowphase: one contact routine per part pair -> ContactManifolds (up to
     MAX_CONTACTS_PER_MANIFOLD = 4 points each), the pairs collided in chunks
     across the pool. Then, on the calling thread and in pair order: gather the
     manifolds, reduce each body's normals into its BodyContacts (most upward,
     most horizontal), and on a live tick track the touching pair.
  6. Build the islands - the dynamic bodies this end decides, joined by the
     tick's contacts and joints - and wake every sleeping island something
     disturbs, or that nothing outside holds any more.
  7. Integrate forces -> velocities: gravity * gravityScale, then damping
     (skips every body the solver cannot move: sleeping, static, kinematic,
     or not this end's to decide). After the islands, so what woke this tick
     falls this tick.
  8. Seed each contact with the impulse its pair carried last tick, then
     solveStep: PGS iterations, each solving every joint and then every
     contact - normal + friction impulses, carrying the restitution target and
     the soft recovery terms - then the poses integrated, then relax passes
     taking the recovery velocity back out. The contact and joint impulses are
     recorded for next tick.
  9. Write the solved pose back through the body's frame, publish the
     BodyContacts onto the Rigidbody, then put to sleep every island that has
     rested long enough, reading the islands step 6 built.
     HierarchySystem re-resolves WorldTransform later in the same frame.
 10. On a live tick, report contacts: sort the tracked pairs by slot pair,
     walk them against the last live tick's, and enqueue Began / Stayed / Ended
     CollisionEvent and TriggerEvent - after the sleep pass, because whether a
     pair rests is what decides its Stayed. A tick that gathers no bodies at
     all still reports, so the last body going ends what it touched.
 11. On a client with a character of its own, lease the bodies it is touching
     (leaseContacts): the island reached from it across this tick's contacts
     and joints, never through an immovable body or another player.
```

A **hierarchy root**'s local `Transform` is already its world pose, so
`gatherBodies` reads it straight. A **parented** body's world pose is resolved by
`worldPoseOf`, which walks its chain of `Transform`s rather than reading
`WorldTransform` - that is written by the Transform stage after physics, so it
would be a frame stale, and absent on a body parented this tick. The parent's
frame is recorded alongside, and writeback maps the solved world pose back
through it into the local `Transform`. Either
way, children parented *to* a body follow it, because HierarchySystem rebuilds
the whole subtree in the Transform stage that follows.

### The solver

`solveStep` works against `PhysicsBody` (per-tick state decoupled from the
Scene, addressed by index from each manifold and joint), not the components
directly. It is soft-constraint projected Gauss-Seidel, shaped like Box2D v3's
soft step but taking the whole tick as one step where Box2D takes several
substeps. It runs `SolverParams::iterations` passes, each solving every joint
and then every contact, so a surface has the last word over a joint. Each
contact's normal impulse carries two things at once: the restitution target, and
a **soft penetration-recovery term**; each joint's impulse carries the same kind
of term for its drift.

A soft term treats the error as a stiff, damped spring. Three coefficients
derived from (hertz, damping, `dt`) convert the error into a target velocity,
soften the impulse that velocity asks for, and leak the accumulated impulse, so
the correction settles at a rate the step can carry instead of overshooting.
What keeps the recovery from adding kinetic energy is the order around it: the
poses are integrated with the recovery velocity still on them, and then
unbiased relax passes take it back out (below). So there
is no position solver, no shadow velocity and no penetration slop.

`SolverParams` carries two fields and no more: `iterations` (PGS passes, from
`PhysicsSettings::solverIterations`) and `dt`. Everything else the contact solve is
tuned by is a file-local `constexpr` in `solver.cpp`, and deliberately not
authorable - `RESTITUTION_THRESHOLD` (below this approach speed, ignore bounce),
`CONTACT_HERTZ` (30, clamped to half the tick rate - stiffer than that and the
damping and relax passes can no longer keep it from ringing; the 30 is Box2D's,
the half-rate bound is this engine's own), `CONTACT_DAMPING`
(10, well above 1 so a contact does not ring) and `MAX_RECOVERY_SPEED` (3 m/s,
or a body spawned inside geometry resolves the overlap by launching). The joint
solver keeps its own spring, `JOINT_HERTZ` and `JOINT_DAMPING` in
`joint_solver.cpp`, and a softer one for a joint's hold, `HOLD_HERTZ` and
`HOLD_DAMPING`.

They are constants rather than settings because the solve has to be
deterministic and every end of a session has to agree about it; a per-scene
contact stiffness is a knob whose wrong setting looks like a physics bug. Change
one by editing it and rebuilding, not by authoring it.

#### Determinism

The same world run twice lands in the same place to the bit; replay and the wire
both rest on it. Gauss-Seidel makes the solve order part of the answer, so every
order the tick depends on is keyed on entity slot rather than on storage order:
the sweep's ties, which body of a pair is A, the joint sort, the contact cache's
keys, and which of two bodies a query names when it strikes both at one
distance - the character controller's step-up reads that one's normal in a
replayed tick. `vkm_core` is built with `-ffp-contract=off`, so a machine whose target has
fused multiply-add rounds the same expression the same way as one without.
`testTheSameWorldRunTwiceEndsBitIdentical` holds it, and prints a hash of every
body's state: a change meant to leave the answer alone is checked by running it
before and after. The narrowphase is the one phase that runs on several threads
(below), and `testABusyPileStepsTheSameOnAnyThread` holds it the same way, on a
pile with enough pairs to fork, against the same pile stepped from a pool task,
where `parallelFor` sweeps serially.

#### What holds a stack up

Three things, and a stack needs all three. Take any one away and five boxes
settle into one another by a centimetre or more, lean, and fall asleep like
that - which is what an author sees as boxes clipped through each other.

**The impulses carry over.** `ContactCache` stores what each contacting pair
was holding when the tick ended, matched next tick by contact position in
either body's own frame - so a pair on a moving or turning platform still finds
itself, and so does a box sliding over a floor, whichever of the two was made
first - and `solveStep` applies it before its first pass. Sequential impulses
converge from wherever they start, and the bottom contact of a stack
carries the weight of everything above it: found from zero, eight passes never
quite arrive. Measured, seeding is worth about four times the iteration count.
A pair neither of whose bodies can move this tick - two sleepers, or a sleeper
on a static floor - is skipped by every pass, so what it held passes through the
cache untouched while it sleeps and the stack wakes carrying its load. Solved,
the spring's leak would take a tenth of it every tick, with nothing to push.

Joints carry theirs the same way. `PhysicsSystem` keeps what each joint held
when the tick ended, by the entity carrying the Joint, and `warmStartJoints`
applies it after `prepareJoints` - kept whole for a point joint, flattened onto
the line for a distance joint. Measured on a ten-link chain with a 50 kg lamp
on the end, the chain hangs 6.4 cm long over its 3 m rather than 12.4 cm; what
is left is the joint spring's own give. A joint neither of whose bodies can
move - every link of a sleeping chain - is skipped like a sleeping pair, and
what it held passes through untouched, so the chain wakes carrying its load.

A joint can also **hold its angle** (`Joint::holdTorque`, newton-metres): a
soft angular constraint, solved in the same pass just before the anchors, that
turns the pair back toward the rotation between them when the joint last began
to move them. While neither body can move the angle is let go, and the first
tick one can it is taken again - so a ragdoll, kinematic until it goes live,
holds the pose it went live in rather than the one it was built in. The
accumulated angular impulse is clamped to the torque times the step, which is
what makes it a muscle and not a weld: a load that needs more turns the joint,
and a limb sags under a weight it cannot carry. It is warm-started and carried
across ticks like the anchors' impulse. `buildRagdoll` sizes each joint's from
`RagdollSettings::muscle` - that fraction of the torque that holds everything
beyond the joint out level - so a felled character falls as one body and folds
as it lands, where free joints would let it crumple where it stood.

**The poses move between the two phases.** The biased passes hand an
overlapping pair a velocity that separates it; `solveStep` integrates the poses
with that velocity still on them, and only then runs its unbiased relax passes
to take it back off - the three in one call, so no caller can order them
otherwise. Relaxing before the integration cancels the recovery instead of
spending it, and the overlap never closes. Both halves of the pose move there - position from the linear velocity, orientation
from the angular - so a box resting a degree into the floor is turned out of it
as well as lifted; writeback only maps the result back into each `Transform`.

**Friction is a cone on a fixed basis.** Each contact builds two tangents from
its normal and accumulates an impulse on each, clamped together against
`friction * normalImpulse` rather than per axis. The alternative - one impulse
along the direction the contact happens to be sliding - re-derives that
direction every pass, so the accumulated scalar means something different each
time it is read; in a stack, where the lateral velocities are near zero and the
direction flips freely, that is a tower shaking itself apart.

What it costs: three extra passes over the contacts and a sort of them, about a
quarter of the contact solve. `solverIterations` at 6 rather than the default 8
recovers it: with the impulses carrying over, six passes land within a
centimetre of eight on an eight-box tower.

`Contact` accumulates `normalImpulse` and the two tangent impulses across a
tick's passes, and across ticks through the cache.

Across ticks the friction travels as a **world vector**, not as the two scalars.
A basis is built from the normal, so a contact whose normal has turned since last
tick would otherwise be handed a pair of numbers measured on an axis it no longer
has. `ContactCache::seed` flattens the remembered vector onto the new contact
plane and leaves it on `Contact::warmFriction`; the solver's pre-solve loop
resolves it onto `tangent1`/`tangent2` in the same pass that builds them. That
placement is the point: the seed runs before the solve, so a seed that did the
decomposition itself would project onto a basis that does not exist yet, and the
friction half of every warm start would come out zero while the normal half
carried.

### The narrowphase

Each proxy's parts are placed in world space once per body, at gather, and sorted
into **two monomorphic arrays**, one of `BoxShape` and one of `CapsuleShape`, each
part's world bound beside it. The pair loops are quadratic, so the shape test
happens once per part rather than inside them. Four loops run: box-box, A's
capsules against B's boxes, B's capsules against A's boxes, and capsule-capsule.
A part is held against the other body's bound before any of its pairings, and
each pairing against its own two bounds, so a compound collider pays only for
the parts near the other body.

The pairs run in parallel, and the answer is still the serial one, to the bit.
The broadphase's pairs are cut into chunks of `NARROWPHASE_CHUNK_PAIRS`
(`physics_internal.h`), and `parallelFor` hands one chunk to each index; a tick with no more pairs than
that is one chunk and never leaves the calling thread. `collidePair` reads only
what the gather and the broadphase built and writes only its chunk's
`NarrowphaseChunk` - the pair's manifolds, a `TouchingPair` naming it and its
first contact, and the triangle scratch of a mesh walk - so nothing it touches
is shared. Everything a pair means beyond its contacts waits for the merge,
which walks the chunks in order and each chunk's pairs in order: the manifold
list, the solver's Gauss-Seidel order, both `BodyContacts` reductions (a strict
`>`, so the first of two equal normals keeps it) and the tracked pairs come out
in pair order, as one thread sweeping every pair leaves them. The chunk size decides who
computes a pair and never what it comes to, so two machines with different core
counts agree.

Every routine honours the same contract - **normal A -> B, positive penetration,
at most `MAX_CONTACTS_PER_MANIFOLD` points** - so the solver, broadphase,
the islands, sleeping, events and writeback are shape-blind. `ContactManifold`
addresses bodies by tick-snapshot index and knows nothing about shapes at all.

- `contactBoxes` - SAT over 15 axes, then a face clip or an edge-edge point. An
  edge axis must beat the best face by a real margin (`EDGE_PREFERENCE_REL/ABS`):
  two horizontal edges of resting boxes cross to the face normal's own direction,
  and taken by float noise that duplicate would turn a four-point face manifold
  into a single corner the position correction then rocked for ever.
- `contactCapsuleBox` - runs in the box's local frame, where the box is
  axis-aligned at the origin, so "closest point on the box" is a clamp and
  clipping to a face is two interval intersections. A positive gap gives the
  normal directly, and a segment that reaches inside leaves through the face
  that frees the whole segment soonest (the same minimum translation the box
  SAT settles on) - measured from the segment's ends, not from one closest
  point, which for a capsule lying through a slab is where it entered. A capsule lying
  **flat** on a face (`CAPSULE_FLAT_DOT`, about three degrees) gets **two**
  points, clipped to the face - resolved from one point it would roll off it
  forever.

  `closestOnSegmentToBox` is **solved, not iterated**: the squared distance from
  the segment to the box is a piecewise quadratic in `t`, its breakpoints are
  where the segment crosses a slab plane - at most six - and each stretch has one
  exact minimum. So the routine has no iteration count and no runaway guard, and
  no input that makes it slow.
- `contactCapsuleCapsule` - closest approach of the two segments, one point
  midway between the two surfaces. Coincident axes fall back to a direction
  perpendicular to the first capsule's own axis, never along it.

Capsule-vs-box is **not symmetric**: when the capsule is body B the routine runs
capsule-first and the caller negates the normals back to A -> B.

A triangle-mesh part joins through the same contract: its hierarchy is culled to
the triangles near the other body, and `contactTriangle` collides each one with
whatever box or capsule the other part is, rather than a routine per pair. Two
mesh parts never collide with each other - neither has a volume to push the
other out of - so a mesh is level geometry, and a dynamic body wearing one
falls through a mesh floor. The triangle
is a three-point `SupportShape`, and `gjkOverlap` decides whether the two touch.
The manifold comes from the feature of the shape that faces the triangle - a
box's face, a capsule's segment when it lies flat, otherwise its deepest point -
clipped to the triangle's three edges with the same `clipToPlane` /
`reduceToManifold` the box pair uses, and kept where it lies below the
triangle's plane. So a box resting on a mesh floor is held at its corners and a
lying capsule at both ends. The winding decides which side is the front and the
depth is measured along the face normal, never along the shallowest way out,
which flips as a body's centre crosses a zero-thickness plane.

That depth is only the right question for a shape over the triangle. One that
reaches in past an edge less far than it reaches below the face is meeting the
triangle from the side - a capsule brushing a ledge's riser touches the top face
only across its edge, half a metre below it - and gets no contact from it: the
way out is through the edge, and the riser's own triangles push it there.

### Inertia

Inertia is approximated as a solid box of the collider's overall local extent -
exact per-part inertia isn't worth it for gameplay (`inertia.h`). The one
exception is a collider that is a **single capsule**, which gets
`capsuleInertiaLocal` (a cylinder plus two hemispherical caps, volume-weighted).
That is the shape the box approximation is worst about: an upright capsule spins
about its own axis several times more freely than the box around it, and that
difference is exactly what a graze against a character tests. Either way the
tensor is parallel-axis-shifted from the collider centre to the entity origin,
where the solver measures its contact arms.

### Key files

Grouped by the question each folder answers, under `src/engine/system/physics/`.

The system and the vocabulary it shares:

- `physics_system.h/.cpp` - the system (gather, broadphase, narrowphase, solve, integrate)
- `physics_events.h` - `ContactPhase`, `CollisionEvent`, `TriggerEvent`
- `physics_internal.h` - `ColliderProxy`, `BodyFrame`, `BodyContacts`, `BodyIslands`,
  `NarrowphaseChunk`: the tick's own view; and `TrackedPair`, what the events
  carry from one live tick to the next
- `body_pose.h/.cpp` - `worldPoseOf`, the one conversion every entry point needs
- `inertia.h` - box / capsule inertia + world-space rotation helpers
- `tolerance.h` - the thresholds collision is decided by, with their units

`collision/` - does it touch, and where:

- `contact.h` - `Contact`, `ContactManifold`, `MAX_CONTACTS_PER_MANIFOLD`
- `narrowphase.h/.cpp` - `BoxShape`, `CapsuleShape`, the three primitive routines
  and `contactTriangle`, which meets a mesh triangle with any of them
- `support.h/.cpp` - `SupportShape`: any convex shape, as the one thing GJK asks of it
- `gjk.h/.cpp` - `gjkOverlap`, which `contactTriangle` asks before it builds a
  manifold
- `mesh_bvh.h/.cpp` - build and query of the triangle-mesh hierarchy, by box and
  along a segment; the node itself lives on the `Collider`. The median split is
  a stable sort by centroid, so every standard library builds one mesh into one
  tree - its order is the order contacts with the triangles are made in
- `triangle.h/.cpp` - `rayTriangle`, the line-triangle crossing that queries and
  Fit to Mesh share

`solver/` - make it stop touching:

- `solver.h/.cpp` - `PhysicsBody`, `SolverParams`, `solveStep`
- `solver_math.h` - the velocity-at-a-lever-arm and effective-mass helpers both
  solvers share
- `joint_solver.h/.cpp` - `JointConstraint`, `prepareJoints`, `warmStartJoints`,
  `solveJointPass`
- `contact_cache.h/.cpp` - `ContactCache`, what each pair held last tick, matched
  by position and seeded into the next

`query/` - ask the world a question:

- `query.h/.cpp` - `raycast`, `spherecast`, `RayHit`, `QueryFilter`

`character/` - what walks on it:

- `character_controller_system.h/.cpp` - `CharacterControllerSystem`

`authoring/` - what makes a shape, at edit time or on load:

- `collider_fit.h/.cpp` - `fitBoxesToMesh` ("Fit to Mesh")
- `mesh_collider.h/.cpp` - `addMeshCollider`, one mesh part per collider, and
  `syncMeshCollider`, which builds its triangles from the mesh it names - asked
  by the load, the inspector card and every tick
- `ragdoll_build.h/.cpp` - `buildRagdoll` / `clearRagdoll`: capsule bodies per
  bone, grouped under a node inside the character, on their own collision layer,
  each joint holding its angle with the build's `muscle`

And beside the system: `ragdoll_system.h/.cpp` poses the bones while a clip
drives the rig - moving at the pose's own speed, so a character felled
mid-stride falls forward - and destroys them with their owner. A posed bone
(`isPosedByAnimation`) is a hitbox: the gather moves it as kinematic whatever
its authored `motion` says, and gives it no collider proxy, so it makes no
contacts - a kinematic bone would shove a crate of any mass alike, and the
character's capsule is what meets the world until the ragdoll goes live.
Queries find it, and filter it by its authored motion.

The components: `src/engine/ecs/component/physics/` - `rigidbody.h`, `collider.h`,
`character_controller.h`, `joint.h`, `ragdoll.h`
