# Physics System — Full Rigid-Body Dynamics (Fixed Timestep)

## Context

The engine has every hook a physics system needs but no physics: the `System`
base already declares `fixedUpdate()` / `hasFixedUpdate()`, and the main loop
already runs a spiral-of-death-guarded accumulator at `Config::FIXED_TIME_STEP`
(1/60s) calling opted-in systems (`engine.cpp:86-94`). The `SystemStage` enum
even documents `Physics -> Simulation` as the intended slot (`system.h:30-33`).

This adds a **full** rigid-body subsystem: linear + angular dynamics, inertia
tensors, restitution + friction, pairwise collision detection and impulse
resolution — stepped deterministically in `fixedUpdate`. Scope: **core
simulation + scene serialization + editor authoring + a demo scene.**

Outcome: drop a body into a scene, watch it fall, collide, stack, and rest;
author it in the Inspector; save/load it.

### Known limitation (deliberate, per style guide section 1.2)
Physics integrates in **world** space but writes the entity's **local**
`Transform`. First pass therefore assumes **physics bodies are hierarchy roots**
(local == world). Documented in the `Rigidbody` / `PhysicsSystem` doc comments;
nested-frame physics deferred until a caller needs it. `markDirty` still runs so
children parented *to* a body follow it correctly.

---

## Architecture

Mirrors how `system/visibility/` is laid out: a system pair at the top, a small
reusable math header (`bounds_utils.h` <-> `inertia.h`), and a focused subfolder
of separable algorithms (`culling/` <-> `collision/`). Each `collision/` file is
a genuinely independent, screen-plus algorithm — not speculative abstraction.

### New files

| File | Purpose |
|------|---------|
| `src/engine/ecs/component/rigidbody.h` | `Rigidbody` data struct (dynamics + mass + material) |
| `src/engine/ecs/component/collider.h` | `Collider` data struct + `ColliderShape` enum + names array |
| `src/engine/system/physics/inertia.h` | header-only free fns: sphere/box local inverse inertia + world rotation |
| `src/engine/system/physics/collision/contact.h` | POD `Contact` / `ContactManifold` |
| `src/engine/system/physics/collision/narrowphase.h` / `.cpp` | per-shape-pair contact generation |
| `src/engine/system/physics/collision/solver.h` / `.cpp` | sequential-impulse (PGS) solver |
| `src/engine/system/physics/physics_system.h` / `.cpp` | `PhysicsSystem : System`, per-tick pipeline |

### Components (data-only structs, bare members, aligned — house style)

`Rigidbody`: `linearVelocity`, `angularVelocity` (world), `invInertiaLocal`
(glm::mat3, derived from shape+mass), `mass`, `inverseMass` (cached; 0 ==
static/kinematic), `linearDamping`, `angularDamping`, `restitution`, `friction`,
`gravityScale`, `isKinematic`, `isStatic`, `sleeping`.

`Collider`: `ColliderShape shape` (`Sphere`/`Box`/`Plane`); reused fields
`radius` (sphere), `halfExtents` (box), `planeNormal` + `planeOffset` (plane),
`isTrigger`. `COLLIDER_SHAPE_NAMES[]` for serialization + editor combo (mirrors
`LIGHT_TYPE_NAMES`).

### PhysicsSystem (class, `m_`-members, full Rule-of-5, `override`)
- `update()` -> no-op; `fixedUpdate()` does all work; `hasFixedUpdate()`->`true`;
  `mutatesResources()`->`false`.
- Settings: `m_gravity = {0,-9.81,0}`, `m_solverIterations = 8`.
- Scratch reused across ticks (`clear()` to keep capacity): `m_bodies`, `m_manifolds`.

---

## Per-tick pipeline (`fixedUpdate`, dt = `ctx.fixedDeltaTime`)

Reuse `localToWorldAABB` / `rayIntersectsAABB` from `bounds_utils.h` for
broadphase AABBs. Iterate via `scene.storage<Rigidbody>()` dense array +
`EntityId{keyAt(i), generationOf(...)}`, exactly the `animation_system.cpp` pattern.

1. **Gather** (serial): build `m_bodies` from live `Rigidbody`+`Transform`
   entities. Defensively recompute `inverseMass` / `invInertiaLocal` from
   `mass`+`Collider` so editor edits to `mass` take effect with no "apply" step.
2. **Integrate forces -> velocities** (`parallelFor`, per-body disjoint): gravity
   `*gravityScale`, damping. Skip `sleeping` / `inverseMass==0`.
3. **Broadphase** (serial): world AABB per body; **sort-and-sweep on X** for
   dynamic bodies; planes are few and infinite so test every dynamic body
   against every plane directly (planes don't enter the sweep list). **Drop
   static-static pairs** (`invMassA==0 && invMassB==0`).
4. **Narrowphase** (serial): `generateContacts` per pair -> sphere-sphere,
   sphere-box, box-box (SAT), body-plane; emit `ContactManifold{normal,
   penetration, point}`. Triggers generate contacts but are excluded from the
   solver list.
5. **Solve** (serial — cross-entity writes): world inverse inertia
   `R * I_local^-1 * R^T` per body, then N PGS iterations of normal impulse (with
   restitution) + Coulomb friction (clamped to mu*Jn). **Split-impulse** position
   correction (no energy injected into velocity -> stable stacks).
6. **Integrate velocities -> pose** (`parallelFor`): `position += v*dt`;
   quaternion `q = normalize(q + 0.5 * omega_quat * q * dt)`; sleep test.
7. **markDirty** (serial): `HierarchyOperations::markDirty(scene, id)` for moved
   bodies that have a `Hierarchy`.

Static = `isStatic` -> `inverseMass=0`, `invInertia=0`. Kinematic = `isKinematic`
-> impulse-immune (treated as infinite mass by solver) but still integrated in
step 6 (moving platforms keep scripted velocity).

### Inertia helper (`inertia.h`, header-only like bounds_utils)
`sphereInverseInertiaLocal(mass, radius)` (solid `2/5 m r^2`),
`boxInverseInertiaLocal(mass, halfExtents)` (`1/12 m (...)` over full extents),
`inertiaWorld(invLocal, quat)` = `R * invLocal * R^T`, `R = glm::mat3_cast(q)`.
`mass<=0` -> `mat3(0)`. Local tensor computed once at gather/spawn; world tensor
recomputed per tick (rotation changes).

---

## Files outside `physics/` to edit

1. **`src/engine/ecs/components.h`** — add `#include` for `rigidbody.h` and `collider.h`.
2. **`src/engine/CMakeLists.txt`** — add `system/physics/*.cpp` to the
   `ENGINECORE_SOURCES` glob. **Required** — new `.cpp`s won't compile otherwise.
3. **`src/engine_app/engine_app.cpp`** — `#include "system/physics/physics_system.h"`
   and `engine.addSystem<PhysicsSystem>(SystemStage::Simulation);` **between** the
   `AnimationSystem` and `HierarchySystem` registrations.
4. **`src/engine/io/component_serializer.cpp`** — `toJson/fromJson(ColliderShape)`
   enum bridge via `COLLIDER_SHAPE_NAMES` (mirror `LightType`), and
   `VKM_REFLECT_BEGIN/END` blocks for `Rigidbody` + `Collider`. **Omit
   `invInertiaLocal`** (no `mat3` toJson) — re-derive on load from `mass`+`Collider`.
5. **`src/engine/io/scene_serializer.cpp`** — `VKM_SERIALIZER_TRAITS(Rigidbody,
   "Rigidbody");` + `(Collider, "Collider");` and append both to the
   `SerializedComponents` tuple.
6. **`src/editor/panels/inspector_panel.{h,cpp}`** — `drawRigidbodySection` /
   `drawColliderSection`; two `has<>` dispatches in `draw()` and two
   Add-Component menu entries via `AddComponentCommand<T>`. `Collider` uses a
   shape combo from `COLLIDER_SHAPE_NAMES`.
7. **`src/engine_app/default_scene.h`** — demo bodies.

---

## Demo scene (verification target)

Extend `generateDefaultScene`: a static ground (`Collider{Plane,
normal=(0,1,0), offset=0}` + `Rigidbody{isStatic=true}`) plus 3-5 dynamic bodies
at `y~=3..6`, each with a `Mesh` (`generateCube()` / `generateSphere()`), a
matching `Collider`, and `Rigidbody{mass=1}`. All **root entities** (no
`setParent`) to satisfy the hierarchy assumption.

---

## Verification

1. **Build:** `cmake -B build -G Ninja && cmake --build build`.
2. **Run** the editor app: bodies fall, contact the ground, bounce per
   `restitution`, settle into a stable stack (no sinking / jitter), and sleep.
3. **Inspector:** select a falling body; `linearVelocity` climbs then zeroes;
   edit `mass`/`restitution`/shape live.
4. **Stability:** no NaNs (quat renorm), no tunneling at demo speeds.
5. **Round-trip:** save scene -> cold restart -> load -> bodies reappear with
   params intact (`invInertiaLocal` re-derived).

---

## Decisions already made (sensible defaults)

- **Broadphase:** sort-and-sweep on X; uniform grid / BVH is the upgrade path.
- **Planes:** not in the sweep list; tested directly against each dynamic body.
- **`invInertiaLocal`:** not persisted; re-derived on load.
- **Position correction:** split-impulse (stabler stacks than Baumgarte).
- **Kinematic bodies:** integrated by scripted velocity, immune to solver impulse.
- **`mass`->`inverseMass`:** physics recomputes defensively at gather time.
