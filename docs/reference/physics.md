# Physics

Fixed-step rigid-body dynamics over box, capsule and triangle-mesh colliders,
solved with soft-constraint sequential impulses and written back to `Transform`.
On top of it, a character controller that turns movement input into velocity.

`PhysicsSystem` runs in `SystemStage::Simulation`, after `AnimationSystem` and
before `HierarchySystem`, all of it in `fixedUpdate()`.
`CharacterControllerSystem` runs immediately after it, so it reads that tick's
fresh contacts.

## Components

Plain data; the headers in `src/engine/ecs/component/physics/` give every field
and default, and [ecs.md](ecs.md) lists them all.

- **`Rigidbody`** - mass, velocities, material, collision masks
  (`layer` / `collidesWith`, bits; `Project::physicsLayers` names them for the
  editor's boxes) and a `motion`: `Dynamic`, `Kinematic` or `Static`.
  Static and kinematic bodies have infinite mass and are never integrated, so a
  kinematic body's pose is the project's to write - but its velocities still
  reach a contact, which is how a kinematic platform pushes what stands on it -
  so a kinematic body that stops being driven should zero its velocities.
  `freezeRotation` stops contacts torquing a dynamic body (the character case).
  Mass properties are re-derived from `mass` and `Collider` every tick, so edits
  need no "apply". A dynamic body turns about its centre of mass and gravity
  pulls there, wherever its origin is (`MassModel`, `solverCentre`), and its
  linear velocity is that point's: a model pivoted at its base topples as its
  shape says.

  It also carries per-tick **outputs**, runtime only: `supported`,
  `supportNormal` (the most upward contact normal), `supportBody`, and
  `blockNormal` (the most horizontal one). Two normals because a character
  pressed against a wall is *held* by the floor and *blocked* by the wall. A
  body that touched nothing reports world up for both.
- **`Collider`** - one or more `ColliderPart`s in the entity's `Transform` frame,
  each a `Box`, `Capsule`, `Mesh` or `Terrain`. The solver ignores `Transform`
  scale; "Fit to Mesh" bakes it into the parts. A capsule runs along local
  **+Y**; capsules are what characters wear, because a box catches on every seam
  between floor boxes.
  A mesh part names its mesh by handle and `syncMeshCollider` builds the
  triangles when needed; **one mesh part per collider** (extra ones collide as
  the first, and the log says so). A `Terrain` part collides the entity's terrain
  the same way, its triangles rebuilt as a brush moves it
  ([terrain.md](terrain.md#how-it-works-inside)). `isTrigger` reports without
  pushing; `enabled = false` takes the collider out entirely, for pooled
  objects.
- **`CharacterController`** - `moveInput` (world-space horizontal velocity),
  `jumpRequested`, `climbing` and `crouching` in; tuning; `grounded`,
  `jumping`, `groundNormal`, `groundVelocity`, `crouched` out. Only the tuning
  is serialized.
- **`Joint`** and **`Ragdoll`** - a joint ties two bodies and can hold its angle
  as a muscle (`holdTorque`): a limb sags under a weight it cannot carry.
  `buildRagdoll` makes capsule bodies per bone, jointed (see
  [Ragdolls](#ragdolls)).
- **`PhysicsSettings`** - gravity and solver iterations, per scene, reached
  through `Scene::physics()` and saved with the scene.
- **`Buoyancy`** - floats a dynamic body on the water it is in
  ([rendering.md](rendering.md#water)).

## The character controller

Each tick, for each `Dynamic` controller this end simulates, the controller
decides `grounded` (a support no steeper than `maxSlopeAngle` - touching a wall
is not standing), steers the body's velocity toward `moveInput` at no more than
`acceleration`, and jumps when asked while grounded. It writes velocity, **never
position**: the solver owns the pose, so a character cannot be pushed through a
wall and friction, sleeping and penetration recovery keep working underneath.
The steps, and the knockback and air-control rules, are in
`character_controller_system.cpp`.

It logs two silent misconfigurations once each: no enabled capsule collider (it
will not climb steps) and `freezeRotation` off (contacts will topple it). It is
a System driving a component rather than a `Behavior`, so whatever writes
`moveInput` can change without the controller changing.

**Climbing.** Gameplay sets `climbing` and `climbNormal` every tick it holds a
ladder or ledge; pushing into the face climbs, a jump leaps off, and the flag
clears after each tick so a character whose hold is gone falls. There is no
ladder component: what counts as reaching one is the game's.

### Staying on the ground

A contact is real overlap, and the narrowphase meets the pose the tick began
at, so contact alone flickers: over a crest the body keeps its climb and flies
off by millimetres, down a slope it leaves the ground faster than steering
turns it. So grounded is held, in three parts:

- **Steering follows the ground plane**, down a slope as well as up it.
- **Speed off the ground is taken back** while grounded: a landing's bounce, a
  crest's throw.
- **Ground falling away holds it** (`grounded`): a character grounded last tick
  sweeps its foot down to walkable ground and closes the gap by velocity - the
  controller never writes position. A taller drop is a ledge, and it falls.

**A throw is `jumping`**: a jump, or ground left faster than a contact bounces -
a pad, a blast - is never grounded until it stops rising, so the floor the tick
after cannot take the jump back; a landing's bounce never throws.

### Sliding along walls

The controller removes the part of its target that pushes into `blockNormal`,
so a character walking into a wall slides instead of snagging. Left to friction,
slide or snag would be decided by material numbers nobody picked for that. Only
a surface steeper than `maxSlopeAngle` deflects (a walkable one is ground), and
only horizontally (deflecting upward would climb any wall).

### Steps

The capsule's round bottom rolls over a step up to
`radius * (1 - cos(maxSlopeAngle))`. A taller edge is a wall; if it is under
`stepHeight`, `findStep` probes with two spherecasts (clearance, then a walkable
landing) and the controller mounts it. The probes use the body's own collision
mask, so a character's ragdoll bones never block its own stairs.

### Crouching

Holding `crouching` shortens the first capsule part to `crouchHeight`, feet kept
in place (`fitCapsule`). Released, the character stands only when `roomToStand`
finds space overhead; otherwise it stays `crouched`, which is what a game reads
to pose and slow it. `Collider` is not replicated, so in networked play a replay
can mispredict a crouch changed within a round trip; a lag-compensated hit meets
the current crouch ([networking.md](networking.md#lag-compensation)).

### Riding what it stands on

Grounded on a body that is not static, the character is carried at
`groundVelocity` - the velocity of that surface at its feet, spin included - and
steers relative to it: walking on a moving deck is walking, a released stick
stands still on it, a jump keeps the carry. A kinematic platform carries by the
velocities it is driven with; one moved by its Transform alone carries nothing.
An `Animation` clip moving a kinematic body gives it those velocities itself,
the clip's own speed across a lap's wrap or a restart, and only for the channels
it has tracks for; once the clip stops, run out or stopped by gameplay, they are
zero.
`groundSpin` reports the deck's turn about up; turning the view with it is
gameplay's call.

## Sleeping

A body at rest for `SLEEP_DELAY` sleeps, together with its whole island (every
dynamic body it touches or is jointed to) - and only while something outside
the island holds it. Anything that could push it - an awake dynamic body, a
driven kinematic one - wakes the whole island; a static one never does. Sleeping
bodies still generate contacts, so stacks stay supported. Code that wakes a
body calls `Rigidbody::wake`; one given a velocity while asleep wakes by itself.

"At rest" is one bound on the fastest point of the body (`SLEEP_SPEED`), not a
linear and an angular threshold apart: a tall box tipping slowly about an edge
would pass both.

## Events

```cpp
struct CollisionEvent { EntityId a, b; ContactPhase phase; glm::vec3 point, normal; float speed; };  // normal a -> b
struct TriggerEvent   { EntityId trigger, other; ContactPhase phase; };
```

A pair reports **Began** on its first touching tick, **Stayed** on each later
one, **Ended** on the first tick it does not. A collision's `a` is the body with
the lower entity slot, so a pair is named - and its normal points - the same way
for its whole life. `speed` on Began is how hard it struck.

- **Ended is sent however the contact ended** - separation, destruction, a
  disabled collider, a changed layer - so either id may be dead.
- **A resting pair is silent but still touching**: no Stayed and no Ended while
  both bodies are asleep or static.
- **Only a live tick reports**; a client's replayed tick does not report again.
- **The order is deterministic** (by entity slot pair), the same on every machine.
- A trigger senses what moves, never a static body - or it would report the
  floor for as long as the level lasted.

Both are enqueued, so listeners run on the next `EventBus` flush, never
mid-solve. Behaviors can use `onCollisionEnter` / `Stay` / `Exit` and
`onTriggerEnter` / `Stay` / `Exit` instead ([scripting.md](scripting.md)).

## Queries

`raycast` and `spherecast` (`query/query.h`) return the nearest body a ray or a
swept sphere strikes, as a `RayHit`, filtered by a `QueryFilter`. They read the
scene directly, so they answer where bodies are now, walking every body per
call. A `Collider` without a `Rigidbody` is in no query.

Many rays in one moment go through `QueryParts`: `gather` places the parts a
filter sees around where the rays will run, once, and its `raycast` answers each
ray as `raycast` would, testing a part's world bound before its shape. It
answers as of `gather`: no `Collider` may change before the last cast.

## Ragdolls

`buildRagdoll` / `clearRagdoll` make and remove capsule bodies per bone, on
their own collision layer, each joint holding its angle
(`RagdollSettings::muscle`). While a clip drives the rig, `RagdollSystem` poses
the bones as kinematic **hitboxes** that make no contacts - the character's
capsule meets the world - but queries find them. Gone live, the bodies drive the
pose ([animation.md](animation.md)), and the group node stays where it stood in
the world, so gameplay moving the character on does not drag the body along.
Either way, a query ignoring the ragdoll's owner (`QueryFilter::ignore`) skips
its bones, so a character casting from its own eye sees past them.

## Editor

- The World inspector's **Physics** card edits `PhysicsSettings`.
- **View > Show > Colliders and Joints** (`DebugCategory::Colliders`, drawn by
  `DebugViewSystem`) draws each part where the solver places it (no scale), and
  joints with it.
- **View > Show > Contacts** (`DebugCategory::Contacts`, which a game can show
  too) draws the last tick's contacts (`PhysicsSystem::drawContacts`): a dot
  where two bodies touch, and an arrow along the contact's normal into a body it
  pushes, longer the harder the solver pushed there and closing on a fixed
  length, so a heavy body's stays readable. Drawn on the tick, they stay while
  the game is paused ([rendering.md](rendering.md#debug-drawing)).
- **Fit to Mesh** (`fitBoxesToMesh`) approximates the mesh with a grid of boxes;
  `detail == 1` is the bounds box. It never fits capsules.
- The inspector names a `Collider` with no `Rigidbody` (in no broadphase) and a
  dynamic `Rigidbody` with no `Collider` (falls through everything) - see
  [the editor](editor.md#a-card-names-what-its-component-is-waiting-for).
- Entity references in joints travel as slots, as every entity reference does; a
  prefab rewrites them as local indices ([io.md](io.md#componentserializer)).

---

## How it works inside

### The tick

`PhysicsSystem::fixedUpdate` works on a per-tick copy of the world, never the
components directly:

1. **Gather** every `Rigidbody` + `Transform` into `PhysicsBody` solver state,
   with a `ColliderProxy` per body with a `Collider`, and the joints resolved to
   body indices. A parented body's world pose comes from `worldPoseOf`, walking
   its `Transform` chain - `WorldTransform` is written after physics.
2. **Broadphase**: sort-and-sweep on X.
3. **Narrowphase**: contact manifolds per pair, in parallel chunks.
4. **Islands**: wake what was disturbed.
5. **Integrate** gravity and damping into velocities, then **solve**
   (`solveStep`). A body the solve moved further than its thinnest part's
   half-thickness sweeps a sphere of that radius about its solver centre - a
   one-part body's inner sphere; a compound's need not lie inside its parts -
   along the path against the static and kinematic parts it would collide with,
   by the pairs' own rules (`stopFastBodies`), and stops at a face its centre
   would pass through: a small, fast body cannot cross a thin mesh or terrain
   floor untouched, and one sliding along a floor slides on.
6. **Write back** poses through the parent's frame into the local `Transform`,
   publish each body's contact outputs, put rested islands to sleep.
7. **Report** contact events (after sleep, which decides Stayed), and on a
   client lease the bodies its character touches.

### The solver

Soft-constraint projected Gauss-Seidel in the shape of Box2D v3's soft step,
taking the whole tick as one step: each pass solves every joint and then every
contact, so a surface has the last word over a joint. Penetration is recovered
by a stiff damped spring in the contact impulse; poses are integrated with that
velocity and then relax passes take it back out, so recovery adds no energy.
There is no position solver.

The spring stiffnesses and thresholds are file-local constants in `solver.cpp`
and `joint_solver.cpp`, not settings: every end of a session must agree about
the solve, and a contact stiffness set wrong looks like a physics bug.
`SolverParams` carries only `iterations` and `dt`.

A stack needs three things, and loses any one by settling into a lean:

- **Warm starting.** `ContactCache` carries each pair's impulses (normal, and
  friction as a world vector) to the next tick; joints do the same.
- **Integrate between the phases.** `solveStep` runs solve, integrate, relax in
  one call so no caller can reorder them.
- **Friction on a fixed tangent basis**, clamped as a cone - one impulse along
  the sliding direction would flip every pass and shake a tower apart.

#### Determinism

The same world run twice ends bit-identical; replay and the network rely on it.
Every order the solve depends on is keyed on entity slot, not storage order, and
`vkm_core` builds with `-ffp-contract=off` so fused multiply-add does not change
rounding. `testTheSameWorldRunTwiceEndsBitIdentical` and
`testABusyPileStepsTheSameOnAnyThread` hold it.

### The narrowphase

Pairs run in parallel chunks (`NARROWPHASE_CHUNK_PAIRS`), each writing only its
own `NarrowphaseChunk`, and are merged in pair order - so the result is the
serial one to the bit on any core count.

Every routine returns **normal A -> B, positive penetration, at most
`MAX_CONTACTS_PER_MANIFOLD` points**, so everything downstream is shape-blind:
`contactBoxes` (SAT, preferring a face over an edge unless the edge clearly
wins, so resting boxes keep a four-point manifold), `contactCapsuleBox` (two
points for a capsule lying flat, or it rolls forever), `contactCapsuleCapsule`.
A mesh part is culled to nearby triangles and `contactTriangle` meets each with
the other part's shape, using `gjkOverlap`. Two meshes never collide - a mesh is
level geometry, and a dynamic body wearing one falls through a mesh floor.

### Inertia

About the centre of mass: a solid box of the collider's extent (`mass_model.h`),
centred on its middle, except a **single capsule**, centred on itself, which
gets `capsuleInertiaLocal` - an upright capsule spins about its axis far
more freely than its box, and a character grazing a wall shows it.

### Key files

Under `src/engine/system/physics/`:

- `physics_system.h/.cpp` - the system and the tick
- `physics_events.h` - `ContactPhase`, `CollisionEvent`, `TriggerEvent`
- `physics_internal.h` - `ColliderProxy`, `BodyFrame`, `BodyContacts`, `BodyIslands`,
  `NarrowphaseChunk`, `TrackedPair`: the tick's own view
- `body_pose.h/.cpp` - `worldPoseOf`
- `mass_model.h` - `MassModel` (a body's centre and inertia from its collider), `inverseMassOf`,
  `solverCentre`
- `tolerance.h` - the thresholds collision is decided by
- `collision/` - `contact.h`, `narrowphase.h/.cpp`, `support.h/.cpp`,
  `gjk.h/.cpp`, `mesh_bvh.h/.cpp`, `triangle.h/.cpp` (`rayTriangle`),
  `part_bounds.h` (`partLocalBounds`)
- `solver/` - `solver.h/.cpp` (`PhysicsBody`, `SolverParams`, `solveStep`),
  `solver_math.h`, `joint_solver.h/.cpp`, `contact_cache.h/.cpp`
- `query/query.h/.cpp` - `raycast`, `spherecast`, `QueryParts`, `RayHit`,
  `QueryFilter`
- `character/character_controller_system.h/.cpp` - `CharacterControllerSystem`
- `authoring/` - `collider_fit.h/.cpp` (`fitBoxesToMesh`),
  `mesh_collider.h/.cpp` (`addMeshCollider`, `syncMeshCollider`),
  `ragdoll_build.h/.cpp` (`buildRagdoll`, `clearRagdoll`)
- `ragdoll_system.h/.cpp` - poses bones while a clip drives the rig

Components: `src/engine/ecs/component/physics/` - `rigidbody.h`, `collider.h`,
`character_controller.h`, `joint.h`, `ragdoll.h`.
