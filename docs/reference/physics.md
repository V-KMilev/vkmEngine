# Physics

Fixed-step rigid-body dynamics over box, capsule and triangle-mesh colliders, solved with
soft-constraint sequential impulses and written back to `Transform`. On top of it, a
character controller that turns movement input into velocity.

All of it runs on the fixed tick, in `SystemStage::Simulation`
([architecture.md](architecture.md#stages) has the order). `CharacterControllerSystem` runs
right after `PhysicsSystem`, so it reads that tick's fresh contacts.

## Components

Plain data; the headers in `src/engine/ecs/component/physics/` give every field and default,
and [ecs.md](ecs.md) lists them all. What the fields cannot say:

- **`Rigidbody`** moves as its `motion` says: `Dynamic`, `Kinematic` or `Static`. Static and
  kinematic bodies have infinite mass and are never integrated, so a kinematic body's pose is
  the project's to write - but its velocities still reach a contact, which is how a kinematic
  platform pushes what stands on it. Mass properties are re-derived from `mass` and `Collider`
  every tick, so edits need no "apply". A dynamic body turns about its center of mass and
  gravity pulls there, wherever its origin is (`MassModel`, `solverCenter`): a model pivoted at
  its base topples as its shape says.

  Its per-tick outputs carry two normals, the most upward contact (`supportNormal`) and the
  most horizontal (`blockNormal`), because a character pressed against a wall is *held* by the
  floor and *blocked* by the wall.
- **`Collider`** is one or more parts in the entity's `Transform` frame, each a `Box`,
  `Capsule`, `Mesh` or `Terrain`, and ignores the `Transform`'s scale (a mesh part has its own
  `meshScale`): the editor writes a scale into the parts as the entity is scaled
  ([editor.md](editor.md#what-the-viewport-shows)). `collider.h` says why characters wear capsules and why a
  collider holds one mesh. A `Terrain` part collides the entity's terrain, its triangles rebuilt as a brush moves
  it ([terrain.md](terrain.md#how-it-works-inside)).
- **`CharacterController`** is [below](#the-character-controller).
- **`Joint`** ties two bodies at a point or a distance, with no angle limits and no motor, and
  can hold its angle as a muscle (`holdTorque`), so a limb sags under a weight it cannot carry.
  **`Ragdoll`** turns a rig over to such bodies ([Ragdolls](#ragdolls)).
- **`Buoyancy`** floats a dynamic body on the water it is in
  ([water.md](water.md#floating-things)).

Gravity and the solver's iterations are not a component but the scene's `PhysicsSettings`,
reached through `Scene::physics()` and saved with the scene.

## The character controller

Each tick, for each `Dynamic` controller this end simulates, the controller decides `grounded`
(a support no steeper than `maxSlopeAngle` - touching a wall is not standing), steers the
body's velocity toward `moveInput` at no more than `acceleration`, and jumps when asked while
grounded. It writes velocity, **never position**: the solver owns the pose, so a character
cannot be pushed through a wall, and friction, sleeping and penetration recovery keep working
underneath. It is a System driving a component rather than a `Behavior`, so whatever writes
`moveInput` can change without the controller changing.

It warns of two silent misconfigurations, once while each lasts: no enabled capsule collider
(it will not climb steps) and `freezeRotation` off (contacts will topple it).

**Climbing.** Gameplay sets `climbing` and `climbNormal` every tick it holds a ladder or
ledge, and the flag clears after each tick, so a character whose hold is gone falls. There is
no ladder component: what counts as reaching one is the game's.

### Staying on the ground

A contact is real overlap, and the narrowphase meets the pose the tick began at, so contact
alone flickers: over a crest the body keeps its climb and flies off, down a slope it leaves
the ground faster than steering turns it. So grounded is held:

- **Steering follows the ground plane**, down a slope as well as up it.
- **Speed off the ground is taken back** while grounded: a landing's bounce, a crest's throw.
- **Ground falling away holds it**: a character grounded last tick reaches down to walkable
  ground close below (`grounded` says how close) and closes the gap by velocity. A taller drop
  is a ledge, and it falls.

**A throw is `jumping`**: a jump, or ground left faster than a contact bounces - a pad, a
blast - is never grounded until it stops rising, so the floor the tick after cannot take the
jump back.

### Sliding along walls

The controller removes the part of its target that pushes into `blockNormal`, so a character
walking into a wall slides instead of snagging; left to friction, slide or snag would be
decided by material numbers nobody picked for that. Only a surface steeper than
`maxSlopeAngle` deflects (a walkable one is ground), and only horizontally (deflecting upward
would climb any wall).

### Steps

The capsule's round bottom rolls over a step up to `radius * (1 - cos(maxSlopeAngle))`. A
taller edge under `stepHeight` is probed (`findStep`: clearance, then a walkable landing) and
mounted. The probes use the body's own collision mask, so a character's ragdoll bones never
block its own stairs.

### Crouching

Holding `crouching` shortens the first capsule part to `crouchHeight`, feet kept in place.
Released, the character stands only when there is room overhead; until then it stays
`crouched`, which is what a game reads to pose and slow it. In a networked game a crouch
changed within a round trip can be mispredicted
([networking.md](networking.md#prediction-and-correction)).

### Riding what it stands on

Grounded on a body that is not static, the character is carried at `groundVelocity` - the
velocity of that surface at its feet, spin included - and steers relative to it: walking on a
moving deck is walking, a released stick stands still on it, a jump keeps the carry. A
kinematic platform carries by the velocities it is driven with; one moved by its Transform
alone carries nothing. An `Animation` clip moving a kinematic body gives it those velocities
itself, for the channels it has tracks for, and zero once the clip stops. Turning the view
with the deck (`groundSpin`) is gameplay's call.

## Sleeping

A body at rest for `SLEEP_DELAY` sleeps, together with its whole island (every dynamic body it
touches or is jointed to) - and only while something outside the island holds it. Anything
that could push it - an awake dynamic body, a driven kinematic one - wakes the whole island; a
static one never does. Sleeping bodies still generate contacts, so stacks stay supported. Code
that moves a sleeping body without a velocity calls `Rigidbody::wake`.

"At rest" is one bound on the fastest point of the body (`SLEEP_SPEED`), not a linear and an
angular threshold apart: a tall box tipping slowly about an edge would pass both.

## Events

`CollisionEvent` and `TriggerEvent` (`core/frame/physics_events.h`, which says what each
carries and promises) report **Began**, **Stayed** and **Ended** for each touching pair, on live
ticks only - a client's replayed tick does not report again - in the same order on every
machine, and enqueued, so listeners run on the next `EventBus` flush, never mid-solve. Behaviors
can use `onCollisionEnter` / `Stay` / `Exit` and `onTriggerEnter` / `Stay` / `Exit` instead
([scripting.md](scripting.md)). The tick's contacts draw while `DebugCategory::Contacts` is
shown (`PhysicsSystem::drawContacts`), which a game can turn on too; drawn on the tick, they
stay while the game is paused.

## Queries

`raycast`, `spherecast` and `overlapSphere` (`system/physics/query.h`) read the scene directly, so they
answer where bodies are now; `QueryParts` answers many rays at one moment. A behavior asks its
own `cast` and `overlapSphere`, which pass its entity over.

## Ragdolls

`buildRagdoll` / `clearRagdoll` make and remove capsule bodies per bone, on their own
collision layer, tied by ball joints that hold their angle (`RagdollSettings::muscle`). The
joints have no angle limits, so the muscle is what keeps a limb's shape: with none, a knee folds
either way. While a clip drives the rig, `RagdollSystem` poses the bones as kinematic
**hitboxes** that make no contacts - the character's capsule meets the world - but queries find
them. Gone live, the bodies drive the pose ([animation.md](animation.md)), and the group node
stays where it stood in the world, so gameplay moving the character on does not drag the body
along. Either way, a query ignoring the ragdoll's owner (`QueryFilter::ignore`) skips its bones,
so a character casting from its own eye sees past them.

---

## How it works inside

### The tick

`PhysicsSystem::fixedUpdate` hands vkmPhysics a copy of the world and writes back what it answers;
the step never sees a component. It gathers each body as a `Physics::Body` - its id its entity's
slot, a parented one's world pose walked up its `Transform` chain, as `WorldTransform` is written
after physics - with its collider's parts and its joints, and calls `Physics::Stepper::step`, which
pairs, collides, solves and sleeps them on the engine's pool (`modules/vkmPhysics/docs/how-it-works.md`
says how). The system then writes back each body's velocities, sleep and contacts, a moved one's
pose through its parent's frame and each joint's warm start, and reports the tick's contacts.

What the engine decides stays in the system: a bone a ragdoll poses is handed `Kinematic` and in
no pair, a body another end of a session decides is not `simulated`, and a replay publishes no
support for it.

A body the solve moved further than its thinnest part's half-thickness sweeps its path against
the parts it would collide with that no contact pushes - static, kinematic and massless ones, and
a body another end of a session moves - and stops at a face its center would pass through: a
small, fast body cannot cross a thin mesh or terrain floor untouched, and one sliding along a
floor slides on.

### Drawn between ticks

Bodies move on ticks and frames come at their own rate, so a body drawn as its last tick left it
would step against a camera that moves every frame. `Engine::run` carries each moving body this
end simulates to where the frame falls, by its velocities about its solver center, before the
frame's stages, and puts it back after them (`DrawnAhead`). A position or rotation the frame
wrote over the carried one - a teleport, a drag in the editor - is kept, and the next tick moves
the body on from there. A body hanging from another moves with it, a remote one is the
snapshots' ([networking.md](networking.md)), a rig a clip plays is posed for the frame's moment
too and its bone sockets put back after it ([animation.md](animation.md#bone-sockets)), and
nothing is carried while time stands still or in a host that draws nothing.

### The solver

Soft-constraint Gauss-Seidel over the tick as one step, warm started from the last tick: each pass
solves every joint and then every contact, and relax passes after integration take back what
penetration recovery added, so a stack stands and recovery adds no energy. Its stiffnesses are
constants, not settings: every end of a session must agree about the solve. A body's mass lies as a
solid box over its collider's bound, except a single capsule, which is taken as that capsule. The
library's page has the rest.

#### Determinism

The same world run twice ends bit-identical on any thread count; replay and the network rely on it.
Every order the step depends on is keyed on each body's id - its entity's slot - not on the order
bodies are gathered in, and vkmPhysics and the engine both forbid fused multiply-add
([building.md](building.md#compiler-flags)).

### The narrowphase

Pairs are collided in parallel chunks merged in pair order, so the result is the serial one to the
bit; a pair at rest takes last tick's contacts rather than colliding again. Every pair routine
returns a normal from A to B and at most four points. A mesh part meets boxes and capsules, never
another mesh - a mesh is level geometry, and a dynamic body wearing one falls through a mesh floor.

## Key files

Under `src/engine/system/physics/`:

- `physics_system.h/.cpp` - the system: the scene's bodies and joints handed to the step, its answer
  written back, the contact events and the lease
- `character_controller_system.h/.cpp` - `CharacterControllerSystem`
- `query.h/.cpp` - `raycast`, `spherecast`, `overlapSphere`, `QueryParts`
- `drawn_ahead.h/.cpp` - `DrawnAhead`, moving bodies as a frame draws them
- `authoring/` - `collider_fit.h/.cpp` (`fitBoxesToMesh`), `ragdoll_build.h/.cpp` (`buildRagdoll`,
  `clearRagdoll`)
- `ragdoll_system.h/.cpp` - poses bones while a clip drives the rig

vkmPhysics, `modules/vkmPhysics/src/vkm_physics/` (its `README.md` maps it and
`docs/how-it-works.md` says how a step runs): `stepper.h/.cpp` (`Stepper`, one step), `body.h` (`Body`,
`Motion`, `layersCollide`), `joint.h`, `workers.h`, `part.h` (`Part` and its bounds), `mass_model.h`
(`MassModel`, `solverCenter`), `sweep.h/.cpp` (`sweepPart`), `tick.h` (what one step works in),
`collision/` (the pairs, each pair's routine, GJK, the triangle tree), `solver/` (`solveStep`, the
joints, the contact cache, the islands) and `geometry/` (`rayTriangle`, `rotationBetween`).

Elsewhere: `core/frame/physics_events.h` (`ContactPhase`, `CollisionEvent`, `TriggerEvent`) and
`core/frame/body_pose.h/.cpp` (`worldPoseOf`). Components: `src/engine/ecs/component/physics/` -
`rigidbody.h`, `collider.h` (and `physicsPartOf`, `massModelOf`, a collider in the library's terms),
`character_controller.h`, `joint.h`, `ragdoll.h`, `buoyancy.h` and `mesh_collider.h/.cpp`
(`syncMeshCollider`); the scene's `PhysicsSettings` is `src/engine/ecs/settings/physics_settings.h`.
