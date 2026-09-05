# The Engine

What vkmEngine is, what it values when two good things conflict, and what has
already been settled. This guide outranks the others: they describe how to build
well, and this one describes what you are building and inside which limits.

---

## Absolutes

Nothing here is a matter of judgment. Breaking one of these is a design problem
to raise, not a tradeoff to make.

- **Engine code never includes a `gl_*` header.** The GPU is reached only
  through the abstract interfaces in `system/render/` - `RenderBackend` for the
  frame, `EditorRenderHooks` for authoring-only work (section 5).
- **`Scene` is never edited to "support" a component.** Any plain struct is a
  component already; `scene.add<T>(entity, ...)` is the whole mechanism.
- **Components hold `Handle<T>`, never asset pointers or copies.** Serialization
  stores asset *names*, resolved back to handles on load.
- **Systems never call each other.** `FrameContext` is the only per-frame channel
  between them. `EditorSystem` is the one sanctioned exception and is not a
  precedent - section 5.
- **A serialized or cooked format never grows a back-compatibility read path.**
  No migration step, no `if (version == 1)` branch, no two live readers. What
  each format's version gate does about an old file is section 5.1 - it is not
  the same answer in all four.
- **Windows and Linux only.** No macOS branches, no macOS in the docs.
- **Nothing in [section 4](#4-what-has-already-been-decided) is reopened inside a
  task.** Reopening a settled decision is a conversation with the owner.

---

## 1. What this is

A C++17 engine with an OpenGL 4.3 backend, for Windows and Linux, that **runs
projects**. It holds no game of its own. A game is a directory - a
`project.json`, its scenes and assets, and gameplay code built into its own
module - and the same four executables (`vkm_editor`, `vkm_runtime`, `vkm_cook`,
`vkm_server`) run any of them. Each finds a project by the same rule: *the project
beside the executable, unless an argument names a different one.*

That separation is the engine's central idea, and most of the absolutes above
exist to protect it.

**What a process is, is which executable was run.** Serving is a host the same
way baking is - `vkm_cook` was never `vkm_runtime --cook` - so hosting a game is
`vkm_server`, not a flag. What a process should *do* is what arguments are for,
which is why the address a client joins is one: a client is a runtime playing
the game, and an address is data that changes between runs while what the
process is does not. The runtime therefore has no `--host`: hosting is
`vkm_server`, and somebody wanting to host and play serves the project and
joins it.

It is built to be **finished rather than rewritten**. Success is not how many
features ship this year; it is whether the thing still holds up in three years
without a subsystem being torn out. Every trade below follows from that sentence.

---

## 2. What it optimises for

Most decisions are easy because only one thing is at stake. This is the order for
when two good things genuinely conflict, and it is the tiebreaker the other
guides defer to:

1. **Correctness that survives load.** A design that stays right as things pile
   onto it beats one that is right today. Wins over everything below.
2. **Predictability.** A reader should be able to guess the shape of the next
   file. A change that invents its own convention costs more than it saves even
   when it is locally better.
3. **Simplicity.** Fewer moving parts, fewer concepts, less surface. But
   *simple*, not merely *small*: if the explicit version is the one that stays
   correct under load, it beats the compact one.
4. **Measured performance.** Data layout over cleverness. The frame is GPU-bound,
   so a CPU-side optimisation is not a win until a profile says so.
5. **Features.** Last, deliberately. A feature that compromises the four above is
   one the engine pays for every year it exists.

When you cannot decide, ask which choice you would rather be living with in three
years. That resolves most of them.

---

## 3. What the engine refuses

Positions, not preferences. Each with its reason, so "why don't we just..." has
an answer:

- **No back-compatibility for old formats.** Migration paths are permanent weight
  carried for a transitional problem, and two live formats mean every future
  reader must know both. The cost lands elsewhere: a format has to be right the
  first time, so scrutinise a format change harder than almost anything else.
- **No temporal antialiasing, ever.** Not "not yet". Techniques needing a
  temporal filter are therefore off the table too, and one has already been
  removed for it (section 4).
- **No speculative abstraction.** No flag, virtual or interface without a
  concrete second user you can name today.
- **No engine-level render pass abstraction.** Passes are an OpenGL
  implementation detail and stay inside the backend. The engine's view of
  rendering is `RenderBackend`, the backend-agnostic `RenderView` it is handed,
  and `EditorRenderHooks` (section 5). Nothing else, and no fourth one without
  the owner.
- **No bulk recovery of pre-refactor code.** When a subsystem is rewritten, the
  old sources are not mined for the missing feature; it is written fresh against
  the new shape. Recovered code carries the assumptions of the design it came
  from.
- **No macOS.**

---

## 4. What has already been decided

So that nobody spends a day re-proposing something settled. Each entry is the
outcome and the reason.

| Decided                  | Outcome and reason                                    |
|--------------------------|-------------------------------------------------------|
| Temporal AA              | Rejected permanently. AA is MSAA plus `specularAA()` in the PBR shader. |
| Contact shadows          | Removed in 1.3. Needs a temporal filter, which is off the table; GTAO covers its role. |
| Screen-space reflections | Removed. Reflection probes serve the case.            |
| SDFGI / voxel GI         | Never built, not planned. Indirect diffuse is a baked SH-L1 irradiance volume plus reflection probes plus GTAO. |
| Auto-exposure            | Proposed and rejected outright.                       |
| Shader permutations      | Built, measured, reverted. No win on a GPU-bound frame. |
| Draw-sort hoisting       | Built, measured, reverted. Same reason.               |
| A glm precompiled header | Built, measured, reverted - and the *reason* matters more than the outcome, because the first measurement of it was wrong. glm is in 150 of 180 translation units, so a PCH looks like the obvious win, and with `ccache` disabled it is one: clean build 4m22s -> 3m08s, one-file rebuild 16.6s -> 15.0s. But this machine has `ccache` active, and ccache does not cache a PCH compile well. Measured with it on: one-file rebuild 10.8s without the PCH against 15.4s with it. **The PCH and ccache are substitutes, and ccache wins by a wider margin than the PCH does.** Keep ccache; do not add the PCH. If ccache ever goes away, this becomes a ~25% win and is worth revisiting. (The first pass here compared a real PCH compile against a ccache *hit* and concluded the PCH was simply slower. It is not - the measurement was contaminated. Recorded because the trap is easy to fall into twice.) |
| Buffer-stall fixes       | Built, measured, reverted. Same reason.               |
| TCP for the wire         | Rejected. Its guarantees are the wrong ones and cannot be declined: in-order delivery means a lost segment stalls the snapshot describing the present behind one describing a moment already past, and retransmission re-sends state the next packet supersedes anyway. The design survives loss instead - see [../reference/system/networking.md](../reference/system/networking.md). Reliability exists where it is genuinely needed, for spawns, and rides the same datagrams. |
| GJK replacing the narrowphase | Rejected as a replacement, adopted as an addition. The hand-written pair routines return up to 4 contact points; GJK+EPA returns one, and resting stability needs the manifold. Mesh triangles route through one GJK path instead of growing the pair matrix: a triangle is a convex point cloud, so the support machinery serves it without a routine of its own. |
| A render graph            | Rejected, and this is the one worth stating loudest because it is the biggest structural idea in modern renderer architecture. A render graph is a compiler for two problems: barriers and transient-memory aliasing. OpenGL has neither - the driver tracks hazards, and there is no API to alias one allocation onto another - so a graph here would buy scheduling flexibility nobody has asked for at the cost of an indirection between every pass and the frame. The fixed, hand-ordered pass list *is* the design. Revisit only alongside a second backend, which is its own decision. |
| A second graphics backend | Not planned. OpenGL 4.3 is frozen rather than dying: it still ships on both target platforms and its tooling (RenderDoc, Nsight) is mature. What a second backend really costs is not the port - it is that every simplification the single backend allows becomes an abstraction: the pass list, the state model, the shader pipeline, `RenderView` itself. Stated as an exit trigger rather than a position, so it stays falsifiable: revisit if a target platform drops GL, if a needed feature exists only in Vulkan, or if driver divergence starts costing more time than the port would. |
| Raising the GL floor to 4.5/4.6 | Deferred, with a stated trigger rather than refused. The case is real: direct state access, `glClipControl` for reverse-Z, and SPIR-V in the cooker would each delete code an engine with a depth prepass and a Hi-Z pyramid currently writes by hand, and every GPU either target platform still ships has driven 4.6 for a decade. It is deferred because the value is in *using* 4.5, not in requesting it - raising the version alone narrows the hardware and deletes nothing - and using it is a backend rewrite whose only honest verification is looking at a rendered frame. Do it at a keyboard with a display, in its own release, in one pass rather than a feature at a time; a half-converted backend is two state models live at once. |
| A scripting language      | Rejected. Gameplay is a native module built against the SDK, hot-reloaded by swapping the shared library. Embedding Lua or C# adds a second language, a second toolchain, a marshalling layer and a second debugger story, to serve authors this engine does not have. The one-person cost is the whole argument: the boundary that exists is already the industry's convergent answer. |
| A fiber or DAG job system | Rejected. The frame is a single-threaded spine with fork-join `parallelFor` on four hot loops, which is where the field converged after a decade of fiber fashion. A dependency graph buys nothing until there are dependencies to express, and the one real defect here was scheduling rather than structure - see the queue split in 2.0. |
| Archetype ECS storage     | Rejected. Sparse sets are slower than archetypes for bulk multi-component iteration at 10^5-10^6 entities and always will be. This engine's benchmark scene is 13.7k entities, the value order puts predictability and simplicity above measured performance, and the open no-registration registry is what makes `scene.add<T>` the whole mechanism. Same design as EnTT, which ships in Minecraft: Bedrock. |
| A spatial structure for rendering | Rejected. At this entity count a flat parallel frustum sweep beats a BVH that has to be rebuilt or refitted as things move, and it has no build cost, no staleness and no second representation of where things are. Queries are a different question and physics answers it separately. |
| Dirty flags on transforms | Rejected. Bevy spent nine pull requests on this and ended up removing them. An unconditional world-transform pass over a flat array is predictable, branch-free and already fast enough; a dirty flag is a second source of truth about whether a matrix is current, and the bugs it causes are invisible-object bugs nobody can reproduce. |
| Generated inspector UI    | Not adopted, deliberately, though the reflection to do it exists. A generated row cannot say what a field means, and the editor's cards carry units, ranges, warnings and the sentence explaining why a value is refused. The repetition is real and 2.0 collapses the *dispatch* rather than the rows. |
| Converging the "three field descriptions" | Examined and rejected: there are not three. The 2.0 survey's headline structural finding was that a component's fields are described three times - `Reflect::Traits`, `BehaviorFieldVisitor` and the `NetSchema` codecs - and that collapsing them would delete the most code of any single change. Read against the source it does not hold. `Reflect::Traits` is the field list. `BehaviorFieldVisitor` is *type erasure over* that list, not a second copy of it: the inspector and the serializer hold a `Behavior*` and cannot instantiate a template on a type the runtime was not built against, so `ReflectedBehavior::visitFields` walks the reflection and dispatches into the visitor. `NetSchema`'s codecs answer a different question - which fields are worth sending and at what precision - and quantisation is exactly where this engine's networking bugs have lived. Three mechanisms, one description, two further questions. |
| Reflecting Ragdoll, Animation and ScriptComponent | Left hand-written, and the line is stated so it stops being re-asked. The serializer driver carries what reflection can describe: leaves, enums, nested reflected structs, `std::vector`, and `Handle<T>` - which is why Mesh, Decal, AudioSource, Animator, Collider and LOD are one-liners. It stops at three. `Ragdoll`'s bones carry `EntityId` references, which need the scene's namer and resolver threaded through the walk to collapse one component; `Animation`'s tracks sit behind private accessors; `ScriptComponent` holds polymorphic behaviours whose fields are reached through `BehaviorFieldVisitor` instead. "Every component reflected" was never the target - "reflection carries what it can describe" is. |
| Speculative contacts     | Examined against the narrowphase and declined, which is a change from "deferred": the blocker is a settled decision rather than an amount of work. Speculative contacts need a *separation* where there is currently only a penetration, and the primitive routines can give one - `overlapOnAxis` already returns a signed overlap and the capsule pair already computes `reach - dist`, so box, capsule and sphere pairs would only stop early-returning. Mesh triangles cannot: they route through GJK+EPA (section 4, the row above), which yields nothing at all when two shapes are apart. Getting a separation there means a closest-point distance query - `gjkDistance` - which was deleted with the convex hull collider and is not coming back. So the half that could be built is the half that matters least: a fast body tunnelling through *level geometry* is the mesh case. Building it for primitives alone would leave two collision behaviours live at once, which is worse than either. Revisit only alongside the distance query, i.e. not at all unless that decision changes. |
| Inertialized transitions | Examined and rejected for this engine, which is not the same as rejecting the technique. Inertialization replaces a two-clip crossfade with one clip plus a pose difference decaying to zero, and its three advertised prizes do not survive contact with `composePose`. **Cost:** the second sample and its three interpolations already sit inside `if (blending)`, so they cost exactly nothing on every frame that is not mid-transition - there is no per-frame saving to win. **State:** it would delete four scalars from `Animator` and add a per-bone offset buffer, which is more state, not less. **Structure:** its real argument is that transitions compose, so no blend graph is ever needed - but there is no blend graph and none planned, which makes that a defence against a problem this engine does not have. What it genuinely buys is C1 continuity - a transition interrupted mid-way stays smooth where a crossfade snaps - and that is a visual property nobody here can judge from a test. Revisit if transitions start being interrupted often enough to look wrong, which is a thing you notice by watching, not by reading. |
| Folding `SkySystem` into `RenderView` | Rejected. It is sixty lines and one system, and the saving is real, but it writes the key light's `Transform` and `Light` *in the scene* - which is what makes the inspector, the light gizmo, the shadow pass and the drawn sun disc agree about where the sun is. Applying the override while building the view instead would leave the scene holding an authored rotation nothing uses and an editor gizmo pointing somewhere the render does not. One source of truth is worth more than one fewer system. |
| Encoding a snapshot once per peer | Deferred with a trigger, not refused. `buildCandidate` encodes every replicated component of every entity once **per connection**, and the bytes it produces do not depend on the connection - only the comparison against that peer's baseline does. Caching the encode per snapshot would save (peers - 1) encodes of the whole world. It is not done because the shape it would take - an encode cache keyed by (entity, type), threaded through the candidate builder - is more machinery than the code has now, for a win nobody has measured, in the subsystem where this engine's worst bugs have lived. **Trigger:** a profile showing snapshot encode above a few percent of a server frame, or `maxPlayers` above eight. At two to four players it is two to four encodes of a world that is mostly asleep. |
| Authored state and session state on one component | Kept as separate fields, and the rule is here so the components stop restating it. Three components play something - `Animation`, `Animator`, `AudioSource` - and each carries the same trio: `playOnStart` (authored, serialized), `playing` (is it advancing right now) and `started` (has `playOnStart` been honoured yet). Only the first is reflected, because **a scene file is what was authored, not what was happening**: a scene that came back from disk mid-sound would resume a noise whose beginning nobody heard, and a preview left running in the editor is not a decision about what a shipped scene does. `started` exists because `playing` falling back to false is exactly what "it finished" looks like, so without it a one-shot would restart every frame after it ended. Folding the trio into a shared `Playback` sub-struct was considered and declined: it would prefix fifty-four call sites and change the scene format to save nine lines of declaration. |

The pattern in the last three is the lesson: **the frame is GPU-bound**, so a
CPU-side optimisation needs a profile before it needs a patch. Assume any obvious
render idea has been tried, and read
[../reference/system/rendering.md](../reference/system/rendering.md) before
proposing one.

---

## 5. What everything else stands on

These carry the rest of the engine, so a mistake in one is never local. The
absolutes at the top state the rules; this is which pieces they protect:

- **`Scene` and the ECS** - open, type-erased, no registration.
- **`FrameContext`** - references are services, pointers are per-frame stage
  products.
- **`NetSession`** - one object, three states. `simulates(entity)` is the
  predicate that decides which end moves what, and offline it answers yes to
  everything, so single-player is unchanged by its existence. Owned by `Engine`
  by value like `Clock` and `EventBus`, and it brackets the frame rather than
  taking part in it - see
  [../reference/system/networking.md](../reference/system/networking.md).
- **`Handle<T>` and `ResourceManager`** - assets owned once, referenced by
  handle, serialized by name.
- **The scene, prefab, project and cooked asset formats** - see the clean-break
  rule above and 5.1 below.
- **`RenderBackend`** - the frame-path seam between engine and GPU. There is a
  second, deliberately: `EditorRenderHooks`
  (`system/render/editor_render_hooks.h`) is offscreen rendering for authoring
  tools - asset thumbnails, material previews - which a shipped game never asks
  for. `GLBackend` inherits both (`backend/opengl/gl_backend.h`) and opts in by
  overriding `RenderBackend::editorHooks()` to return itself; the editor asks
  through `editorRenderHooks()` and shows placeholders when the answer is null.
  It lives under `system/render` rather than in the editor because the backend
  has to implement it and cannot see editor code. Two seams, both named here; an
  authoring-side GPU need extends this one rather than inventing a third.
- **Stage order** - Input, Simulation, Transform, Visibility, Render, UI. A
  system is placed by responsibility and relies on ordering, never on manual
  sequencing.

`EditorSystem` is the one system that holds references to others - the five in
the `AppSystems` bundle (camera, UI, visibility, render, audio) plus the
`ScriptModule` that owns the gameplay library, taken by constructor
(`editor/editor_system.h`) and wired at
`app/editor/main.cpp`. That is allowed because it is not participating in
the frame's data flow: it is the authoring tool *driving* the engine, at the UI
stage, after every producer has already run. Nothing else gets to do this. A
system that finds itself wanting a reference to another system has a
`FrameContext` product it has not defined yet.

### 5.1 What each format's version gate actually does

The rule above is one rule; the enforcement is four different mechanisms, and an
agent bumping a constant needs to know which it is holding.

| Format         | Constant                                              | What the reader refuses                       |
|----------------|-------------------------------------------------------|-----------------------------------------------|
| Cooked asset   | `io/asset/asset_cook.h` - five, one per asset kind | Anything but an exact match. A stale artifact is refused and re-cooked. |
| Scene          | `io/scene/scene_serializer.cpp` (`= 2`)            | Missing, zero, or *newer* than this build. Nothing else. |
| Prefab         | `io/scene/prefab.cpp` (`= 3`)                      | The same shape.                   |
| `project.json` | `io/project.cpp` `engineVersion`                      | Nothing. It warns, deliberately - "refusing to open would be worse". It is provenance, not a format version. |
| Asset manifest | `io/asset/asset_library.cpp` `MANIFEST_VERSION`       | Anything but an exact match, in **either** direction. A manifest this build cannot read starts the library empty, so *every* asset then looks uncooked and the whole project re-cooks. That is the intended recovery and it is total: derived data, refused whole rather than half-understood. |
| Cooker         | `tools/cook/asset_cooker.cpp` `COOKER_VERSION`        | Nothing directly - it is mixed into each asset's `recipeHash`, so bumping it makes every recorded hash stale and every asset re-cook. It is how "the cooker itself changed" reaches the staleness check. |

The consequence for scene and prefab: **bumping the constant does not by itself
stop an old file loading.** The gate refuses the future, not the past; an older
file is read by the current reader and a removed field simply comes back as its
default. So say in the commit what you expect an old file to do. If the answer
needs to be "refuse it", that is a change to what the gate does and belongs with
the owner, not inside the task.

And when you change one, round-trip against what ships:
`examples/physics_lab/scenes/obstacle_course.json` is a saved scene of record -
the other examples still build their worlds in code. A format change that scene
cannot survive is a format change that did not happen.

---

## 6. Facts that have cost real time

Not rules. Things that are true, are not obvious, and have each produced a wrong
result somebody had to chase down.

- **The frame is GPU-bound.** See section 4.
- **Forward is -Z and screen-right is +X**, right-handed with +Y up - glm's
  own convention. It was +Z forward once, which put right at -X and left
  `glm::quatLookAt` aiming 180 degrees off. Screen-right is `cross(forward,
  up)` under either convention; `cross(up, forward)` is the mirrored one and
  always was. The convention lives in `Math::computeForward` and friends;
  every place that broke during the flip was a place that had hand-rolled it.
