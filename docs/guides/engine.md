# The Engine

What vkmEngine is, what it values when two good things conflict, and what has
already been settled. This guide outranks the others: they say how to build
well, this one says what you are building and inside which limits.

---

## Absolutes

Breaking one of these is a design problem to raise, not a tradeoff to make.

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
  each format's version gate does with an old file differs - section 5.1.
- **Windows and Linux only.** No macOS branches, no macOS in the docs.
- **Nothing in [section 4](#4-what-has-already-been-decided) is reopened inside a
  task.** Reopening a settled decision is a conversation with the owner.

---

## 1. What this is

A C++17 engine with an OpenGL 4.3 backend, for Windows and Linux, that **runs
projects**. It holds no game of its own. A game is a directory - a
`project.json`, its scenes and assets, and gameplay code built into its own
module - and the same four executables (`vkm_editor`, `vkm_runtime`, `vkm_cook`,
`vkm_server`) run any of them. Each finds its project by one rule: *the project
beside the executable, unless an argument names a different one.*

That separation is the engine's central idea, and most of the absolutes exist to
protect it.

**Which executable was run is what the process is; its arguments are what it
does.** Serving is a host the way baking is, so hosting a game is `vkm_server`,
not a flag on the runtime. The address a client joins is an argument, because it
changes between runs while what the process is does not. There is no `--host`:
someone who wants to host and play serves the project and joins it.

It is built to be **finished rather than rewritten**. Success is whether it still
holds up in three years without a subsystem being torn out, not how many
features ship this year. Every trade below follows from that.

---

## 2. What it optimises for

The order for when two good things genuinely conflict. The other guides defer to
it:

1. **Correctness that survives load.** A design that stays right as things pile
   onto it beats one that is right today.
2. **Predictability.** A reader should be able to guess the shape of the next
   file. A change that invents its own convention costs more than it saves, even
   when it is locally better.
3. **Simplicity.** Fewer moving parts, fewer concepts, less surface - *simple*,
   not merely *small*. If the explicit version is the one that stays correct
   under load, it beats the compact one.
4. **Measured performance.** Data layout over cleverness; a capture before a
   patch. Whether the CPU or the GPU binds depends on the scene, so a change wins
   only when a capture of the side it touches says so
   ([../reference/building.md](../reference/building.md#measuring-a-change)).
5. **Features.** Last, deliberately. A feature that compromises the four above
   is paid for every year the engine exists.

When you cannot decide, ask which choice you would rather be living with in three
years.

---

## 3. What the engine refuses

Positions, not preferences, each with its reason:

- **No back-compatibility for old formats.** A migration path is permanent weight
  for a transitional problem, and two live formats mean every reader must know
  both. The cost lands elsewhere: a format has to be right the first time, so
  scrutinise a format change harder than almost anything else.
- **No temporal antialiasing, ever.** Not "not yet". Techniques that need a
  temporal filter are off the table with it (section 4).
- **No speculative abstraction.** No flag, virtual or interface without a
  concrete second user you can name today.
- **No engine-level render pass abstraction.** Passes are an OpenGL detail inside
  the backend. The engine's view of rendering is `RenderBackend`, the
  backend-agnostic `RenderView` it is handed, and `EditorRenderHooks`. No fourth
  without the owner.
- **No bulk recovery of pre-refactor code.** A rewritten subsystem is not mined
  for the missing feature; it is written fresh against the new shape. Recovered
  code carries the assumptions of the design it came from.

---

## 4. What has already been decided

So nobody spends a day re-proposing something settled. Each row is the outcome,
the reason, and - where there is one - what would reopen it.

| Decided | Outcome and reason |
|---|---|
| Temporal AA | Rejected permanently. AA is MSAA plus `specularAA()` in the PBR shader. |
| Contact shadows | Removed. The technique needs a temporal filter; GTAO covers its role. |
| Screen-space reflections | Adopted, without a temporal filter: probes cannot show what moves or what lies between them. Coherent rays and a neighbour-averaged resolve stand in for TAA ([rendering.md](../reference/rendering.md)). |
| A hierarchical (Hi-Z) walk for the reflection trace | Built, measured, reverted. A nearest-depth pyramid walked as McGuire and Mara and Uludag describe found the thin side faces and contact lines the 16-48-step march skips in the mirror goldens, at about the march's GPU time there (-0.06 to +0.10 ms on a 0.3-1.0 ms pass at 1080p). But building the pyramid cost 0.07 ms of CPU every frame, and on physics_lab the pass took 0.11 ms more GPU (0.40 -> 0.51 ms) with nothing different to see: a ray grazing a receding floor descends at almost every cell, because the floor nearer the eye shares it. Revisit when a project's glossy surfaces visibly lose thin geometry. |
| SDFGI / voxel GI | Never built, not planned. Indirect diffuse is a baked SH-L1 irradiance volume, reflection probes and GTAO. |
| Auto-exposure | Rejected. The tonemap curve is fixed and chosen by the project. |
| Shader permutations | Built, measured, reverted: no win. One ubershader branches on material flags at run time. |
| Draw-sort hoisting | Built, measured, reverted: no win. |
| Buffer-stall fixes | Built, measured, reverted: no win. |
| Multi-draw indirect | Adopted. Every mesh lives in one `GLMeshPool`, so a run of different meshes goes out as one `glMultiDrawElementsIndirect` (core 4.3) per change of program or material. Six interleaved runs a side: on project_alpha the shadow pass's CPU fell 0.71 -> 0.34 ms, the depth prepass 0.40 -> 0.18 and the forward pass 0.98 -> 0.75, the backend's whole frame 3.78 -> 2.88 ms, with GPU time within 1% either way; on stress_arena, whose few meshes were already instanced, 0.02-0.06 ms a pass. The forward pass binds a material per run, for its textures; the prepass binds none. |
| A glm precompiled header | Rejected while `ccache` is active: a one-file rebuild takes 10.8 s without the PCH and 15.4 s with it, because ccache caches a PCH compile badly. Without ccache it is a ~25% win - revisit if ccache goes. |
| TCP for the wire | Rejected. In-order delivery stalls the snapshot describing the present behind one describing the past, and retransmission resends state the next packet supersedes. The design survives loss instead ([networking.md](../reference/networking.md)): a snapshot says again whatever the receiver has not confirmed, so what must arrive - an entity gone, a prefab spawned - is state the next snapshot repeats, not a message a channel resends. |
| Replicating an inactive ragdoll's bones | Rejected: replicate causes, not effects. Inactive, the bones are posed from a clip both ends choose from the replicated velocity; sent, four characters of twenty bones were 96% of every snapshot. An active ragdoll's bones are the answer rather than a copy of it, and all of them travel ([networking.md](../reference/networking.md)). |
| Fixed gains for client tick pacing | Rejected. Gains tuned at 30 ms overshot on every round trip of a 200 ms link and swung between the two bounds about a hundred times a minute; the gains scale with the measured round trip and the tick rate. |
| GJK replacing the narrowphase | Rejected. The hand-written primitive pairs return up to four contact points, which resting stability needs; GJK+EPA returns one. GJK answers overlap alone, for a mesh triangle before its manifold is built, and there is no EPA. |
| A render graph | Rejected. A render graph solves barriers and transient-memory aliasing; OpenGL has neither problem to solve - the driver tracks hazards and there is no aliasing API. The fixed, hand-ordered pass list *is* the design. Revisit only alongside a second backend. |
| A second graphics backend | Not planned. Its real cost is not the port but that every simplification a single backend allows - the pass list, the state model, `RenderView` itself - becomes an abstraction. Revisit if a target platform drops GL, a needed feature exists only in Vulkan, or driver divergence costs more than the port would. |
| Raising the GL floor to 4.5/4.6 | Deferred with a trigger. Direct state access, `glClipControl` and SPIR-V would each delete code, but the value is in *using* 4.5, not requesting it, and using it is a backend rewrite verified only by looking at frames. Do it in one pass, in its own release, at a display - a half-converted backend is two state models at once. |
| A scripting language | Rejected. Gameplay is a native module built against the SDK and hot-reloaded by swapping the library. A second language brings a second toolchain, a marshalling layer and a second debugger, for authors this engine does not have. |
| A fiber or DAG job system | Rejected. The engines that run a task graph - Unreal, Bevy, Naughty Dog's fibers - have a render thread or frames in flight to order against the game. This frame is one thread with fork-join `parallelFor` on its hot loops, and a graph buys nothing until there are dependencies to express. |
| Archetype ECS storage | Rejected. Sparse sets lose to archetypes on bulk multi-component iteration at 10^5-10^6 entities; this engine's benchmark scene is 13.7k, and the open, registration-free registry is what makes `scene.add<T>` the whole mechanism. The same design as EnTT. |
| A spatial structure for rendering | Rejected. At this entity count a flat parallel frustum sweep beats a BVH that must be rebuilt or refitted as things move, and has no second representation of where things are to go stale. Physics answers queries separately. |
| GPU occlusion culling | Built, measured, removed. After a depth prepass the forward pass already rejects hidden fragments at the depth test, so a Hi-Z cull could save only the vertex work of hidden instances - 0.13 ms on the most occluded scene - while the pyramid and the cull cost more than that on the GPU and 0.2-0.4 ms of CPU; frames were faster with it off on every project measured. Revisit with a vertex-bound scene. |
| Dirty flags on transforms | Rejected. An unconditional world-transform pass over a flat array is predictable and fast enough; a dirty flag is a second source of truth about whether a matrix is current, and its bugs are invisible objects nobody can reproduce. |
| Generated inspector UI | Not adopted, though the reflection to do it exists. A generated row cannot say what a field means; the cards carry units, ranges, warnings and why a value is refused. The dispatch is shared (`editComponentCard<T>`), the rows are not. |
| Converging the "three field descriptions" | Rejected: there are not three. `Reflect::Traits` is the field list; `BehaviorFieldVisitor` is type erasure over it for callers holding a `Behavior*`; `NetSchema`'s codecs answer a different question - which fields travel, at what precision. |
| Reflecting Joint, Ragdoll, Animation and ScriptComponent | Left hand-written. The reflection driver stops at entity references (`Joint`, `Ragdoll`), private accessors (`Animation`'s tracks) and polymorphic behaviours (`ScriptComponent`). The target is "reflection carries what it can describe", not "every component reflected". |
| An iterated closest point for capsule against box | Replaced by a closed form. The alternating projection averaged about seven passes, wanted hundreds for a segment nearly tangent to a face, and a run cut short answers a distance too large - which reads as no contact. |
| Substepping the solver | Deferred with a trigger. The solver takes Box2D v3's soft contacts in one full step; substeps buy stiffer mass ratios and joint chains at about equal cost, and the price is re-tuning every measured table in physics.md. Trigger: a mass-ratio stack or a ragdoll chain seen failing that iterations cannot fix. |
| Speculative contacts | Declined. They need a separation where the narrowphase has only a penetration, and mesh triangles cannot give one without the distance query deleted with the convex hull collider. Primitives alone would leave two collision behaviours live, and the tunnelling that matters is through level meshes. |
| One serialized undo command for every component | Declined. A single before/after-JSON command would replace the typed `ComponentEditCommand` family and `VKM_EDITOR_COMPONENTS` (about 250 lines), but it serializes on every frame of a drag - which the override path was made to stop doing - and the typed commands are tested and not churning. Revisit if a new kind of edit needs a command the typed family cannot express. |
| Keyframe `Animation` tracks folded into clips | Declined. Eased per-entity tracks and skeletal clips serve different authoring - a prop's bob against an imported character's walk - and folding them saves about 400 lines at the price of easing curves in clips, a scene format change and every authored track re-made. Revisit when clips gain curves for their own reasons. |
| Inertialized transitions | Rejected. They add per-bone state to buy composition without a blend graph, and there is no blend graph. Revisit if an interrupted transition is seen to look wrong. |
| Folding `SkySystem` into `RenderView` | Rejected. `SkySystem` writes the key light's `Transform` and `Light` *in the scene*, which is what makes the inspector, the gizmo, the shadow pass and the sun disc agree on where the sun is. One source of truth is worth more than one fewer system. |
| Encoding a snapshot once per peer | Deferred with a trigger. The world is encoded once per connection though the bytes do not depend on it; a cache keyed by (entity, type) would live where this engine's worst bugs have. Trigger: snapshot encode above a few percent of a server frame, or `maxPlayers` above eight. |
| A UI widget library | Rejected. UI is ECS primitives - a clipping rect, a scrolling rect, a quad, a string, a hit-tested tint - and a widget is whatever a project parents together. Revisit when two projects want the *same* widget. |
| A scrollbar as a component | Rejected with it. `UIScroll` publishes `contentSize` and `viewSize` in the canvas's reference pixels, so a project draws one from two `UIImage`s and a division. |
| Axis flags on `UIScroll` | Refused. The wheel is one axis, and it drives whichever axis overflows, vertical first - what a browser does. Two flags would have to stay in step with content the walk already measures. |
| Multi-channel SDF text, or re-baking on resize | Not adopted. The single-channel field is clean to about three times its baked height, and the bake is sized from the monitor, so one bake covers every size the window can reach. Revisit for text beyond the bake cap. |
| A test hook for the UI pointer | Refused. A setter that exists for a test is a second writer of the pointer. There is one: `InputHandle::moveTo`, used by the window on a host with one and by the caller on a host without. |
| Authored state and session state on one component | Kept as separate fields. `Animation`, `Animator` and `AudioSource` carry `playOnStart` (authored, serialized), `playing` (advancing now) and `started` (was `playOnStart` honoured - without it a finished one-shot restarts every frame). A scene file is what was authored, not what was happening. |
| Stealing a voice at the cap | Rejected. Past `MAX_ACTIVE_VOICES` `AudioDevice::play` refuses the new sound: the oldest voice is as likely to be the music as a footstep. The cap is a runaway guard, not a mix budget ([audio.md](../reference/audio.md#the-voice-budget)). |
| Downmixing a stereo clip to mono | Rejected, at import and at cook. Mono is right for a clip on a positioned source and wrong for the same file on a music bed, so it is a property of the pairing; the inspector card and `AudioSystem` warn where a positioned source plays a stereo clip. Revisit as an authored per-clip flag in the cook, if a project asks. |
| Splitting `inspector_panel.cpp` | Declined. Its size is one `VKM_INSPECTOR_CARDS` row per component over shared helpers; splitting it spreads the one index that makes adding a component one edit. |
| A truthful roughness/metalness in the depth prepass | Declined. It would add a UV and two texture samples to the pass whose purpose is to be cheap, every frame, for a debug view. The forward pass writes those views instead, from the surface it samples anyway, at the cost of a uniform branch; the G-buffer is the normal alone. |
| Bounces in the irradiance bake | Three gathers (`GLIrradianceBaker::BOUNCES`), each lighting the captured surfaces from the grid the one before made ([lighting.md](../reference/lighting.md#what-a-capture-is-lit-by)). In a closed room of 0.7-albedo plaster lit through a doorway and a window, three hold 80% of the light ten gathers reach and four 88%, for a fifth more bake time; on physics_lab's open course two hold 98.7% and three 99.9%. Every gather after the first skips the backface mask and the refused probes, so a bake costs about two and a half times one gather lit by the sky: 85 -> 220 ms for the room's 256 probes, about 230 -> 520 ms for physics_lab's 480. Revisit as a per-volume field if a project's interiors read too dark at three, or its bakes run too long. |
| Weighting probe taps by validity at lookup time | Declined: eight manual taps per coefficient per fragment, where `dilateProbeGrid` repairs refused probes once at bake time and the lookup stays four fetches. Revisit for a grid too coarse for its geometry. |
| Interpolating fixed-step state for the drawn frame | Extrapolated instead: `Clock::getFixedAlpha` says how far the frame is past the last tick, and a body that must move every frame carries its last tick forward by its velocity, so the local player pays no latency. Interpolation between ticks belongs to `NetSession::interpolate`, for remote bodies. |
| Splitting static from moving shadow casters | Declined. A held spot tile or cube face redraws whole when anything in it moves; Unreal splits the two so a mover redraws only itself, at the price of a second depth store per light and a composite per sample. With six 2D tiles (the sun's cascades and the spots) and two cubes a whole-tile redraw is cheap, and the frame is CPU-bound. Trigger: a capture showing shadow redraws above a few per cent of a frame, in a scene with a persistent mover beside a shadowing light. |
| LTC specular for area lights | Not adopted. An area light's diffuse is the exact polygon form factor; its specular is Karis's representative point, as UE4 shipped it. LTC needs two fitted 64x64 tables for a light type no project uses large. Trigger: a large rect light over a glossy floor that reads wrong in a project. |
| Authenticating or encrypting packets | Declined. The join token stops floods and spoofed joins, but an on-path attacker can still read and forge a session. Closing that needs a key exchange - netcode.io's connect tokens from a backend, or DTLS - and a LAN game or a self-hosted server has no backend to issue them. Trigger: internet play through a matchmaker that can issue tokens. |
| clang-format and clang-tidy for the mechanical rules | Not adopted. The `docs` suite holds the tree to what code-style.md says - list layout, width, the Rule of 5, structs, comments, tables and X-macro rows included, which a formatter would need fencing off from - and runs with the build on every machine with no extra tool. Trigger: a semantic rule (linkage, `override`) the suite cannot express and review keeps catching by hand. |
| A shipping build of the engine | Adopted. A game ships on `VKM_SHIPPING` - `Release`, no profiler, debug info kept for reading a crash - and its module is built again against it; the editor and every other command run the development engine. One build cannot be both: the profiler that lets a frame be measured is code, a thread and a listening port a player's game has no use for ([building.md](../reference/building.md#the-shipping-engine)). |

Any obvious render idea has probably been tried: read
[../reference/rendering.md](../reference/rendering.md) before
proposing one, and measure before and after.

---

## 5. What everything else stands on

A mistake in one of these is never local:

- **`Scene` and the ECS** - open, type-erased, no registration.
- **`FrameContext`** - references are services, pointers are per-frame stage
  products.
- **`HostView`** (`ctx.hostView`, `system/visibility/host_view.h`) - the one
  product an authoring host writes into the frame's data flow: a view to render
  through instead of the scene's active camera - the editor's own viewpoint, or
  a scene camera it previews. It is the host's view as `HostChrome`'s viewport
  rect is the host's frame, a statement a game never makes; `VisibilitySystem`
  is its only reader, so nothing past the `Visibility` product knows whose view
  it was. An authoring need to see the world differently extends this rather
  than teaching a system that the editor exists.
- **`NetSession`** - one object, four states. `simulates(entity)` decides which
  end moves what; offline it answers yes to everything, so single-player is
  unchanged by its existence. `Disconnected` is not bookkeeping: it is the
  opposite of `Offline` on the only question that matters - an offline end
  decides everything, a dropped client decides nothing. Owned by `Engine` by
  value like `Clock` and `EventBus`, it brackets the frame rather than taking
  part in it ([networking.md](../reference/networking.md)).
- **`Handle<T>` and `ResourceManager`** - assets owned once, referenced by
  handle, serialized by name.
- **The scene, prefab, project and cooked asset formats** - the clean-break rule
  above, and 5.1.
- **`RenderBackend`** - the frame-path seam between engine and GPU. The second
  seam is deliberate: `EditorRenderHooks` (`system/render/editor_render_hooks.h`)
  is offscreen rendering for authoring tools - thumbnails, material previews -
  that a shipped game never asks for. `GLBackend` implements both and opts in by
  overriding `RenderBackend::editorHooks()`; the editor shows placeholders when
  the answer is null. It lives in `system/render` because the backend must
  implement it and cannot see editor code. An authoring-side GPU need extends
  this seam rather than inventing a third.
- **Stage order** - Input, Simulation, Transform, Visibility, Render, Editor. A
  system is placed by responsibility and relies on ordering, never on manual
  sequencing.
- **Determinism of the simulation** - a fixed step, stage order, and every
  entity walk that feeds simulated state ordered by slot. A client replays the
  ticks the server disagreed with and must arrive where the server did, so a
  change that lets machine state or iteration order leak into a tick breaks
  networking far from where it was made
  ([implementation.md](implementation.md#4-determinism)).

`EditorSystem` is the one system that holds other systems, taken by constructor
(`editor/editor_system.h`). It may because it is not in the frame's data flow: it
is the authoring tool driving the engine, at the Editor stage, after every producer
has run. Any other system that wants a reference to another has a
`FrameContext` product it has not defined yet.

### 5.1 What each format's version gate actually does

One rule, several mechanisms. Know which one you are holding before you bump a
constant:

| Format         | Constant                                              | What the reader refuses                       |
|----------------|-------------------------------------------------------|-----------------------------------------------|
| Cooked asset   | `io/asset/asset_cook.h` `COOKER_VERSION` - one number for every kind's byte layout and for what the cooker makes of a recipe (a material's recipe is its runtime form, and has no cooked file) | Anything but an exact match in the header - though a bump never reaches that check: `AssetCook::cacheKey` mixes it into every cooked file's name, so after a bump no file is found, every load falls back to its recipe and the next cook bakes it again. A change to one kind re-bakes every kind. |
| Scene          | `io/scene/scene_serializer.cpp`                       | Missing, zero, or *newer* than this build. Nothing else. |
| Prefab         | `io/scene/prefab.cpp`                                 | The same shape.                               |
| `project.json` | `io/project.cpp` `engineVersion`                      | Nothing. It warns: it is provenance, not a format version, and refusing to open a project would be worse. |
| Asset manifest | `io/asset/asset_library.cpp` `MANIFEST_VERSION`       | Anything but an exact match, either direction. The library then starts empty, every name loads from its recipe - whose path needs no row - and the next cook records it again: total recovery of derived data, refused whole rather than half-understood. |
| Library recipe | None - `library/<type>/<uid>.json` carries no version | Nothing past its shape: one with no `source`, or of a `kind` no factory dispatches, does not load. Within that a key this build does not know is ignored and one it lacks reads as its default (a texture's `usage` as `Data`, and one it does not recognise is reported); a key that holds the wrong type costs that one asset, reported by name and recipe path, never the scene. The recipe is regenerated from what the import made of it each time its asset is cooked, so the next cook writes it in the current form - with whatever the default read. It is version-controlled source, so a change to a key's meaning is the one format change here that alters authored data: say in the commit what an old recipe reads as. |

For scenes and prefabs, **bumping the constant does not stop an old file
loading.** The gate refuses the future, not the past: an older file is read by
the current reader and a removed field comes back as its default. Say in the
commit what an old file will do. If the answer must be "refuse it", that changes
what the gate does and belongs with the owner.

Round-trip any format change against what ships:
`examples/physics_lab/scenes/obstacle_course.json` is the saved scene of record.
A format change that scene cannot survive is a format change that did not
happen.

---

## 6. Facts that have cost real time

Things that are true, not obvious, and have each produced a wrong result someone
had to chase:

- **Forward is -Z and screen-right is +X**, right-handed with +Y up - glm's own
  convention. Screen-right is `cross(forward, up)`. The convention lives in
  `Math::computeForward` and its siblings; every place that ever broke was a
  place that hand-rolled an axis.
- **ccache is active on this machine** though nothing in the CMake mentions it.
  A build timing taken without `CCACHE_DISABLE=1` is not a measurement.
- **A cached build hides a header that does not compile on its own.** Compile
  it alone, with the flags from `build/compile_commands.json`, before trusting
  that it includes what it uses.
