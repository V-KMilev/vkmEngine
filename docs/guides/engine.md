# The Engine

What vkmEngine is, what it values when two good things conflict, and what has
already been settled. This guide outranks the others: they say how to build
well, this one says what you are building and inside which limits.

---

## Absolutes

Breaking one of these is a design problem to raise, not a tradeoff to make.

- **Engine code never names a graphics API.** The GPU is reached only through
  abstract interfaces - `RenderBackend` for the frame, `EditorRenderHooks` for
  authoring-only work, `WindowSurface` for the window it draws into (section 5).
  No `gl_*` header, GL loader or API version in `src/engine/`, `src/editor/` or
  `src/tools/`; a host names the backend it picks.
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
- **A position is reopened with the owner; a measurement, by measuring.** The
  absolutes, section 3 and the rows of [section 4](#4-what-has-already-been-decided)
  marked *Position* are the owner's, and arguing one inside a task is out of
  bounds. Every other row of section 4 is a measurement, true where it was taken:
  when its trigger fires or its conditions no longer hold, measure it again, and
  take a result that overturns it to the owner.

---

## 1. What this is

A C++17 engine with an OpenGL 4.5 backend, for Windows and Linux, that **runs
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
- **No engine-level render pass abstraction.** Passes are a backend detail. The
  engine's view of rendering is `RenderBackend`, the backend-agnostic `RenderView`
  it is handed, `EditorRenderHooks`, and the `WindowSurface` the window presents
  through. No fifth without the owner.
- **No bulk recovery of pre-refactor code.** A rewritten subsystem is not mined
  for the missing feature; it is written fresh against the new shape. Recovered
  code carries the assumptions of the design it came from.

---

## 4. What has already been decided

So nobody spends a day re-proposing something settled, and nobody mistakes a
measurement for a law. A row is one of two kinds:

- **A position** - the owner's choice of what the engine is. It is marked so, and
  no number moves it.
- **A measurement** - what was built or tried, and why it was kept or dropped. It
  holds where it was taken, on the machines and scenes its commit names. A
  measurement says what reopens it - other hardware, a larger scene, a second
  backend, a cost showing in a capture - and one that does not is unfinished, not
  final. The numbers behind a row are in its commit, not here.

| Decided | Outcome and reason |
|---|---|
| The engine and its users | *Position.* The engine is designed for itself: nothing stays because a game, an example or an old file uses it, and a better shape replaces the old one with its users ported ([implementation.md](implementation.md#7-changing-a-shape-its-users-rely-on)). |
| Temporal AA | *Position.* Rejected permanently. AA is MSAA plus `specularAA()` in the forward shader. |
| Screen-space shadows | Adopted, as Bend Studio's: no noise and no history, so a still frame shades the same every time. GTAO, which is ambient occlusion, cannot cast the sun's shadow ([lighting.md](../reference/lighting.md#screen-space-shadows)). Revisit if the pass's cost shows in a capture of a GPU-bound frame. |
| Screen-space reflections | Adopted, without a temporal filter: probes cannot show what moves or what lies between them. Coherent rays and a neighbour-averaged resolve stand in for TAA ([rendering.md](../reference/rendering.md)). Revisit if a project's glossy surfaces show the trace's noise, or its cost shows in a capture. |
| A hierarchical (Hi-Z) walk for the reflection trace | Built, measured, reverted. It found thin geometry the march skips, but its pyramid cost CPU every frame and an ordinary scene paid more GPU with nothing different to see. Revisit when a project's glossy surfaces visibly lose thin geometry. |
| Cone specular occlusion from GTAO's bent normal (GTSO) | Built, measured, reverted. It counts the part of a glossy floor's lobe that lands on the floor as black, visibly darkening the floor's reflection of the horizon. Lagarde's AO-only fit stays. Revisit with a specular occlusion that reflects the occluder's radiance rather than black. |
| Visibility-bitmask GTAO (Therrien et al. 2023) | Built, measured, reverted. Its assumed thickness made a thin pole occlude as a slab, and the halo it exists to fix was already small. Revisit with a thickness taken from the scene rather than assumed. |
| SDFGI / voxel GI | *Position.* Never built, not planned. Indirect diffuse is a baked SH-L1 irradiance volume, reflection probes and GTAO. |
| Auto-exposure | *Position.* An option, off by default (`autoExposure`): a game moving between dark interiors and daylight needs it, one lit alike keeps its authored exposure, and the look stays the project's choice. Measured on the GPU, read back without a stall, decided on the CPU (`system/render/exposure.h`), so another backend adapts alike. |
| Shadow bias units | Texels of the map at the receiver, for every light (Castano 2013): one pair holds for any range, cascade or distance. The defaults leave no acne and keep a thin caster's shadow on a wall just behind it. Revisit if a light type or range needs a pair of its own. |
| Shadow atlas tile sizes | The sun's first `FULL_CASCADES` cascades each take a whole `shadowResolution` block of the atlas and the rest take half: with fewer full cascades the step in texel size showed where the soft search crossed a split, and every cascade full would leave the spots no room. A spot's tile follows how much of the screen its cone covers. Revisit with another cascade count or atlas layout. |
| Settling the soft shadow's blocker search from each shadow-map square's nearest depth | Built, measured, reverted. The search is most of the soft shadow's cost, but the points it runs for are mostly casters too: their own surface lies in the square, nearer than the search's farthest compare, so the bound settled almost nothing and building it cost more than it saved. Revisit with a bound that knows the receiver's plane, or receivers that cast nothing. |
| ACES's 1/0.6 pre-scale | Dropped. Applied, it visibly lifts every scene authored here, and the examples read washed out. Revisit with scenes authored against the reference ACES transform. |
| The sky's brightness | Lit by the scene's sun (`Atmosphere::solarIlluminance`), with no constant: `lightIntensity` is an illuminance and the bake's scattering is per steradian, so the units agree; a separate sun intensity put far too much of the light in the sky. Tune haze as `mie`, not as a constant. Revisit if a project's sky and sun disagree in a way haze cannot tune. |
| The sky's ground albedo | `Atmosphere::GROUND_ALBEDO`, Hillaire's value: the ground past a scene's floors reads as they do. Darker grounds left a dark band, or a void, below the horizon. Revisit for a world whose ground is far darker or brighter than its floors - an ocean, snow. |
| The sky drawn from a sky-view table | Adopted, Hillaire's: the skybox reads a table computed every frame rather than the baked env cube, so the sky drawn is always the current sun's and the horizon is not a cube texel. Revisit if the table's cost shows in a capture. |
| The sky's lighting baked a step a frame | Adopted, as Unreal time-slices its sky capture: baked whole, the env cube and its convolutions drop a frame each time a moving sun needs them. The price is a second set of cubes and ambient light trailing a moving sun by a bake or so; a sun run ahead of the shown sky takes more steps a frame, up to `GLIBLBaker::MAX_CATCH_UP_STEPS`, so its light keeps pace ([lighting.md](../reference/lighting.md)). Revisit if the trailing ambient shows under a project's moving sun. |
| Importance-sampled sky convolutions | Adopted (Colbert and Krivanek, GPU Gems 3 ch. 20): each convolution draws its samples by its own lobe and reads the env level its share of the lobe covers, at a fraction of the brute-force bake's cost. The render suite holds them to skies with an answer in closed form. Revisit if an HDR sky's convolution shows the sampling's noise. |
| A smaller env cube for the procedural sky | Not adopted. A smaller cube saved little of the bake, and a mirror would lose the horizon's edge. Revisit if the env cube's share of the bake shows in a capture of a moving sun. |
| Aerial perspective's default | The planet's air: `aerialPerspective` defaults to Unreal's distance scale, so a scene of a few hundred metres barely changes and one of kilometres fades into the horizon. Another scale is for a world not built to scale; haze is `mie`. Revisit if a world built to scale reads too hazy or too clear. |
| Fog read B-spline filtered across the screen | Adopted: read trilinear alone, a small glow in fog showed every froxel's square. Four trilinear reads weighted as a cubic B-spline (`shaders/bspline.glsl`) hide them; a glow one froxel wide comes out broader and dimmer at its peak. Revisit if the reads' cost shows in a capture of the passes that fog. |
| Lighting a particle once, not at each corner | Declined: measured no faster. Revisit if a profile shows the particle pass's vertex work. |
| Particles simulated on the GPU | Declined: the CPU steps and flattens a scene's particles in a small share of a frame, and a strike asks the physics world, which lives on the CPU. Revisit if a capture shows `ParticleSystem`'s step. |
| An ocean of Gerstner waves drawn from a spectrum | Adopted, as Unreal's water system ships it: up to `Ocean::MAX_WAVES` waves from JONSWAP, summed on the GPU and by the same arithmetic on the CPU, so a float's query is exact and immediate. An FFT of the spectrum (Tessendorf; HDRP, Crest) gives thousands of waves, but each gameplay query wants a GPU readback a frame late or a CPU FFT, and the tiles repeat; only the ripples, which nothing afloat asks about, are FFT. Waterline foam is read off a world-space shore, not the screen's depth, which foamed only what the camera saw. Revisit when a project's sea is seen from high above or out to the horizon, where so few waves read as regular. |
| Shader permutations | A plain variant alone, adopted: the forward program compiled without the optional features, for a material that uses none of them (`GLMaterial::plain`): the features cost even where a material does not use them. No general permutation system; revisit for a second variant when another feature's cost shows on materials that do not use it. |
| Hoisting the shadow pass's sort | Built, measured, reverted: sorting the casters once cost what sorting each tile's indices saved. Revisit if a caster shrinks to an index. |
| Buffer-stall fixes | Built, measured, reverted: the shadow instances' upload was not a stall. The persistently mapped ring (the GL floor below) did win, by removing the driver's copy. Revisit if an upload shows as a stall in a capture. |
| A radix sort of the batch keys | Built, measured, reverted: the batch's cost is building the keys, not sorting them. Revisit if the sort itself shows in a capture. |
| Hashing shadow signatures a word at a time | Built, measured, reverted: no win against the byte-wise FNV-1a. Revisit if the hashing shows in a capture. |
| `KHR_parallel_shader_compile` | Built, measured, reverted: the driver measured still compiled on the calling thread. Shortening a cold start means compiling on worker threads with shared contexts. Revisit on a driver that compiles in the background. |
| A no-error context (`KHR_no_error`) | Built, measured, reverted: no change on the driver measured. Revisit on another vendor's driver. |
| A subgroup-scalarised light loop | Built, measured, reverted: the forward loop taking each wave's lights one shared index at a time (`ARB_shader_ballot`) was slower where measured. It is made for a GPU with a scalar unit (Drobot, SIGGRAPH 2017); revisit on one, keeping it only if the others do not lose. |
| Bindless textures | Not built. llvmpipe, which CI renders on, has no `ARB_bindless_texture`, so the bound-texture path would stay beside it for ever. Materials in one buffer with draw IDs waits on it. Revisit with the Vulkan backend, where descriptor indexing is core. |
| The sun's cascades culled on the GPU | Declined. The shadow cull already runs on the workers; what the render thread keeps - the casters' keys and upload signatures - is needed whoever culls, and a GPU cull would lose the shadow-tile cache's skip of an unchanged tile. Revisit if the workers' shadow cull shows in a capture. |
| Multi-draw indirect | Adopted. Every mesh lives in one `GLMeshPool`, so a run of different meshes goes out as one `glMultiDrawElementsIndirect` per change of program, material or vertex layout, cutting the shadow and depth passes' CPU with GPU time unchanged. Revisit if building the commands shows in a capture. |
| A glm precompiled header | Rejected while a compiler cache is in use: ccache caches a PCH compile badly, so a one-file rebuild got slower. Revisit if ccache goes. |
| TCP for the wire | *Position.* Rejected. In-order delivery stalls the present snapshot behind a past one, and retransmission resends state the next packet supersedes. A snapshot says again whatever the receiver has not confirmed, so what must arrive is state the next snapshot repeats ([networking.md](../reference/networking.md)). |
| Reliable messages from a player | Adopted, upstream only. A move or a name must arrive once and in order; as held state in the command payload it cost the game a nonce, an acknowledgement and a round trip per message. They ride every command packet until a snapshot acknowledges them ([networking.md](../reference/networking.md#messages-once-in-order)). Downstream stays state; revisit when a game must tell one player a fact no component holds. |
| A name table for entities on the wire | *Position.* Adopted on the client alone. The server's slot stays an entity's name, but a client must make entities of its own (UI, effects) while connected. It maps the server's slots, keeping each one's own slot when free, so the slot order replay depends on is kept ([networking.md](../reference/networking.md#identity-is-the-servers-slot)). |
| Replicating an inactive ragdoll's bones | Rejected: replicate causes, not effects. Inactive, the bones are posed from a clip both ends choose from the replicated velocity; sent, they were nearly all of every snapshot. An active ragdoll's bones all travel ([networking.md](../reference/networking.md)). Revisit if a game needs an inactive ragdoll's pose to match exactly on every end. |
| Fixed gains for client tick pacing | Rejected: gains tuned for a short round trip swung on a long one. The gains scale with the measured round trip and the tick rate. Revisit if pacing swings on a link the scaling does not cover. |
| GJK replacing the narrowphase | *Position.* Rejected. The hand-written primitive pairs return up to four contact points, which resting stability needs; GJK+EPA returns one. GJK answers overlap alone, for a mesh triangle before its manifold is built, and there is no EPA. |
| A render graph | Rejected. A render graph solves barriers and transient-memory aliasing, which OpenGL does not have. The fixed, hand-ordered pass list *is* the design. Revisit only alongside a second backend. |
| A second graphics backend | *Position.* Planned, not started: Vulkan as the default, OpenGL kept to switch to. The engine already names no API, so the work is the backend itself; what one backend lets stay simple - the fixed pass list, the state model - stays inside it rather than becoming an engine abstraction. |
| Quality tiers | Adopted, each step measured: a tier is a preset over the project's render settings, which may turn off screen-space effects and soft shadows the project has on (`withPreset`, [rendering.md](../reference/rendering.md)). The soft filter's umbra skip is no tier's: the soft filter always takes it. Revisit a tier's step when a capture shows it buys nothing, or a lower tier still misses its frame. |
| `parallelFor`'s grain | Adopted: `CHUNKS_PER_THREAD` chunks a thread, so a preempted thread's share goes to the others - the middle of a plateau that performed alike, so another machine lands on the flat part too. Revisit on a many-core machine if the chunk claims show on the pool's lock. |
| The GL floor | 4.5 core, used, not just asked for: direct state access, immutable texture storage, a persistently mapped ring for per-frame data and reversed depth. Not 4.6: llvmpipe, which CI renders on, offers 4.5, and what 4.6 would add is an extension there and on the other drivers in use. Revisit when CI's driver offers 4.6 and the backend needs what it makes core. |
| A scripting language | *Position.* Rejected. Gameplay is a native module built against the SDK and hot-reloaded by swapping the library. A second language brings a second toolchain, a marshalling layer and a second debugger, for authors this engine does not have. |
| Thread pool sized by physical cores | Adopted: one worker fewer than the physical cores, SMT siblings counted once ([threading.md](../reference/threading.md#threadpool)). More workers left the main thread waiting on the scheduler beside the driver's, the audio's and the OS's threads, with no gain. Revisit on a many-core SMT machine if its parallel stages slow. |
| A fiber or DAG job system | Rejected. Task-graph engines have a render thread or frames in flight to order against the game; this frame is one thread with fork-join `parallelFor` on its hot loops. Revisit with a render thread or frames in flight - the Vulkan backend is the likely one. |
| Archetype ECS storage | Rejected. Sparse sets lose only on bulk multi-component iteration over far more entities than this engine's scenes hold, and the open, registration-free registry is what makes `scene.add<T>` the whole mechanism - the same design as EnTT. Revisit if such iteration shows in a capture. |
| A spatial structure for rendering | Not needed. A tree costs a rebuild or refit as things move; what a large world spends instead is working every object out again each frame, which a fixed mesh (`Mesh::movable` off) skips. Revisit if the sweep over kept boxes itself shows in a capture. |
| GPU occlusion culling | *Position.* An option, off by default (`RenderSettings::occlusionCulling`): the project's choice, in its `render` block. It is the two-phase cull (Haar and Aaltonen, 2015), run by the depth prepass; it saves only hidden instances' vertex work, so it pays where the GPU bounds the frame and much of the scene is hidden, and costs a little on an open one ([visibility.md](../reference/visibility.md#occlusion-culling-on-the-gpu-as-an-option)). |
| Dirty flags on transforms | *Position.* Rejected as a hidden second truth: when one writer forgets the flag, nobody can reproduce the bug. Replaced by a stated promise: a mesh says it never moves (`Mesh::movable` off), and the cull rechecks a rotating share of what it kept each frame (`VisibilitySystem::FIXED_CHECK_STRIDE`), so a broken promise is soon drawn right and logged ([visibility.md](../reference/visibility.md)). |
| Generated inspector UI | *Position.* Not adopted, though the reflection to do it exists. A generated row cannot say what a field means; the cards carry units, ranges, warnings and why a value is refused. The dispatch is shared (`editComponentCard<T>`), the rows are not. |
| Converging the "three field descriptions" | *Position.* Rejected: there are not three. `Reflect::Traits` is the field list; `BehaviorFieldVisitor` is type erasure over it for callers holding a `Behavior*`; `NetSchema`'s codecs answer which fields travel, at what precision. |
| Reflecting Joint, Ragdoll, Animation and ScriptComponent | Left hand-written: the reflection driver stops at entity references (`Joint`, `Ragdoll`), private accessors (`Animation`'s tracks) and polymorphic behaviours (`ScriptComponent`). The target is "reflection carries what it can describe". Revisit when the driver learns one of the three. |
| An iterated closest point for capsule against box | *Position.* Replaced by a closed form. The iteration wanted many passes for a segment nearly tangent to a face, and a run cut short answers a distance too large - which reads as no contact. |
| Substepping the solver | Deferred with a trigger. Substeps buy stiffer mass ratios and joint chains at about equal cost, at the price of re-tuning every default in physics.md. Trigger: a mass-ratio stack or a ragdoll chain seen failing that iterations cannot fix. |
| Speculative contacts | *Position.* Declined. They need a separation where the narrowphase has only a penetration, and mesh triangles cannot give one without the deleted distance query. Primitives alone would leave two collision behaviours live, and the tunnelling that matters is through level meshes. |
| One serialized undo command for every component | Declined. A before/after-JSON command would replace the typed `ComponentEditCommand` family and `VKM_EDITOR_COMPONENTS`, but it serializes on every frame of a drag. Revisit if a new kind of edit needs a command the typed family cannot express. |
| Keyframe `Animation` tracks folded into clips | Declined. Eased per-entity tracks and skeletal clips serve different authoring - a prop's bob against a character's walk - and folding them would change the scene format and re-make every authored track. Revisit when clips gain curves for their own reasons. |
| Inertialized transitions | Rejected. They add per-bone state to buy composition without a blend graph, and there is no blend graph. Revisit if an interrupted transition is seen to look wrong. |
| Folding `SkySystem` into `RenderView` | *Position.* Rejected. `SkySystem` writes the key light's `Transform` and `Light` *in the scene*, so the inspector, the gizmo, the shadow pass and the sun disc agree on where the sun is. |
| Encoding a snapshot once per peer | Deferred with a trigger. The world is encoded once per connection though the bytes do not depend on it; a cache keyed by (entity, type) would live where this engine's worst bugs have. Trigger: snapshot encoding showing in a capture of a server frame, or a game for more players than the examples host. |
| A UI widget library | Rejected; three controls adopted. UI is ECS primitives, and a widget is whatever a project parents together. `UISlider`, `UIToggle` and `UIChoice` (a setting's "< High >" row), each of which two projects built alike, are components `UIButton`s drive; the look stays the project's. Another control joins them when two projects want the *same* one. |
| A scrollbar as a component | *Position.* Rejected with it. `UIScroll` publishes `contentSize` and `viewSize`, so a project draws one from two `UIImage`s and a division. |
| Axis flags on `UIScroll` | *Position.* Refused. The wheel drives whichever axis overflows, vertical first, as a browser does; flags would have to stay in step with content the walk already measures. |
| Multi-channel SDF text, or re-baking on resize | Not adopted. The single-channel field stays clean well past its baked height, and the bake is sized from the monitor, so one bake covers every size the window can reach. Revisit for text beyond the bake cap. |
| A test hook for the UI pointer | *Position.* Refused: a setter for a test is a second writer of the pointer. The one writer is `InputHandle::moveTo`, used by the window, or by the caller on a host without one. |
| Authored state and session state on one component | *Position.* Kept as separate fields. `Animation`, `Animator` and `AudioSource` carry `playOnStart` (authored, serialized), `playing` (advancing now) and `started` (was `playOnStart` honoured - without it a finished one-shot restarts every frame). A scene file is what was authored, not what was happening. |
| Stealing a voice at the cap | *Position.* Adopted as priorities: at `MAX_ACTIVE_VOICES` a new sound takes the place of a voice of lower priority, or of its own and quieter than itself, and a voice a pause holds is kept ([audio.md](../reference/audio.md#the-voice-budget)). Oldest-first stealing stays rejected: the oldest voice is as likely to be the music as a footstep. |
| Downmixing a stereo clip to mono | Rejected, at import and at cook: mono is right for a positioned source and wrong for a music bed, so it belongs to the pairing; the inspector and `AudioSystem` warn where a positioned source plays a stereo clip. Revisit as an authored per-clip flag in the cook, if a project asks. |
| Splitting `inspector_panel.cpp` | *Position.* Declined. It is one `VKM_INSPECTOR_CARDS` row per component over shared helpers; splitting it spreads the one index that makes adding a component one edit. |
| A truthful roughness/metalness in the depth prepass | Declined: it would add a UV and texture samples to the pass meant to be cheap, for a debug view. The forward pass writes those views instead; the G-buffer is the normal alone. Revisit if something other than a debug view needs them in the G-buffer. |
| Bounces in the irradiance bake | `GLIrradianceBaker::BOUNCES` gathers, each lighting the surfels from the grid the one before made ([lighting.md](../reference/lighting.md#what-a-capture-is-lit-by)): they reach nearly all the light many more would, where fewer left interiors visibly dark, and a gather is cheap. Revisit if a project's interiors still read too dark. |
| Relighting the irradiance volume | Adopted. The bake keeps what each probe sees as surfels, and a change of the lights or the sky lights them again, a gather a frame ([rendering.md](../reference/rendering.md#bakes)) - Lumen's and Enlighten's split of geometry from light. The cost is `IrradianceVolume::MAX_PROBES`: the atlas is cubic in the grid. Revisit if a project needs a finer grid than that cap allows. |
| Reflection probes follow their light | Adopted. A probe captured under other light than the frame's is captured and convolved again a step a frame into a staging layer copied in whole, so no frame shows half a probe; the price is one more layer of each array. Surfels would not help: most of a probe's bake is the convolution. Each probe compares only the lights that reach within half its width of its box, and the volume those reaching its own: compared scene-wide, any light moving anywhere kept a refresh running every frame. A refreshed probe rests before its next, as a capture face draws the scene: under a light that never holds still, back-to-back refreshes put that draw in a frame every few. Revisit if a light outside that reach is seen to leave a probe stale, or the rest is seen as lag. |
| Weighting probe taps by validity at lookup time | Declined: a manual tap per corner per coefficient per fragment, where the bake's blend of trusted neighbours (`probeFillWeights`) repairs refused probes and the lookup stays one filtered fetch a coefficient. Revisit for a grid too coarse for its geometry. |
| Interpolating fixed-step state for the drawn frame | *Position.* Extrapolated instead: `Clock::getFixedAlpha` says how far the frame is past the last tick, and a body that must move every frame carries its last tick forward by its velocity, so the local player pays no latency. Interpolation between ticks is `NetSession::interpolate`'s, for remote bodies. |
| Splitting static from moving shadow casters | Half adopted. A tile's fixed casters (`Mesh::movable` off) are signed by the cull's `fixedSerial`, not one by one, so every tile is signed - cascades too - and a still view redraws no shadow at all. Built, measured, reverted: keeping each tile's fixed casters' depth apart and copying it back under the movers; the copy cost more than redrawing the fixed casters, even in a city of fixed meshes. Revisit with a mover beside a light over geometry too heavy to redraw each frame. |
| LTC specular for area lights | Not adopted. An area light's diffuse is the exact polygon form factor; its specular is Karis's representative point, as UE4 shipped it. LTC needs two fitted tables (Heitz et al.'s, under a BSD notice the engine would carry) and their transform of the polygon a light a pixel, for a light type no project uses large. Trigger: a large rect light over a glossy floor that reads wrong in a project. |
| Authenticating or encrypting packets | Declined. The join token stops floods and spoofed joins, but an on-path attacker can still read and forge a session. Closing that needs a key exchange - netcode.io's connect tokens from a backend, or DTLS - and a LAN game or a self-hosted server has no backend. Trigger: internet play through a matchmaker that can issue tokens - and it is required before a game is played with strangers over the internet. |
| clang-format and clang-tidy for the mechanical rules | Not adopted. The `docs` suite holds the tree to code-style.md, including rules a formatter would need fencing off from, and runs with the build with no extra tool. Trigger: a semantic rule (linkage, `override`) the suite cannot express and review keeps catching by hand. |
| A shipping build of the engine | *Position.* A game ships on `VKM_SHIPPING` - `Release`, asserts and the profiler off, debug info kept for reading a crash - and its module is built again against it; the editor and every other command run the development engine, asserts on. A player's game has no use for an assert's stop or a profiler's thread and listening port ([building.md](../reference/building.md#the-shipping-engine)). |

Any obvious render idea has probably been tried: read
[../reference/rendering.md](../reference/rendering.md) before
proposing one, and measure before and after.

---

## 5. What everything else stands on

A mistake in one of these is never local:

- **`Scene` and the ECS** - open, type-erased, no registration.
- **`FrameContext`** - references are services, pointers are per-frame stage
  products.
- **The host's two products** - all an authoring host writes into the frame.
  `HostView` (`ctx.hostView`, `system/visibility/host_view.h`) is a view to
  render through instead of the scene's active camera - the editor's own
  viewpoint, or a scene camera it previews. `VisibilitySystem` is its only
  reader, so nothing past the `Visibility` product knows whose view it was.
  `HostChrome` (`ctx.chrome`, `core/host_chrome.h`) is what the host's own UI
  holds this frame - the pointer, the keyboard, the viewport - and whether the
  world is open to its edits (`setAuthoring`). A system asks it what it must not
  take or keep, never which host it runs under. An authoring need extends one of
  these rather than teaching a system that the editor exists.
- **`NetSession`** - one object, four states. `simulates(scene, entity)` decides which
  end moves what; offline it answers yes to everything, so single-player is
  unchanged by its existence. `Disconnected` is the opposite of `Offline`: an
  offline end decides everything, a dropped client nothing of the world it was
  shown. Owned by `Engine` by value, it brackets the frame rather than taking
  part in it ([networking.md](../reference/networking.md)).
- **`Handle<T>` and `ResourceManager`** - assets owned once, referenced by
  handle, serialized by name.
- **The scene, prefab, project and cooked asset formats** - the clean-break rule
  above, and 5.1.
- **`RenderBackend`** - the frame-path seam between engine and GPU. The second
  seam is deliberate: `EditorRenderHooks` (`system/render/editor_render_hooks.h`)
  is offscreen rendering for authoring tools - thumbnails, material previews -
  that a shipped game never asks for, and the renderer of the editor's own ImGui.
  `GLBackend` implements both and offers them by returning itself from
  `RenderBackend::editorHooks()` (null offers none); the editor will not start
  without them. It lives in `system/render` because the backend must implement
  it and cannot see editor code. An authoring-side GPU need extends this seam
  rather than inventing a third.
- **`WindowSurface`** (`platform/window/window_surface.h`) - what a backend asks of
  the window: the hints before it exists, attaching to it, presenting, the swap
  interval. A host hands one to `setupEngineApp` beside its backend
  (`GLWindowSurface` with `GLBackend`); a host that draws nothing hands none.
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
| Library recipe | None - `library/<type>/<uid>.json` carries no version | Nothing past its shape: one with no `source`, or of a `kind` no factory dispatches, does not load. An unknown key is ignored, a missing one reads as its default, and a key of the wrong type costs that one asset, reported, never the scene. The next cook rewrites the recipe in the current form. It is version-controlled source, so a change to a key's meaning alters authored data: say in the commit what an old recipe reads as. |
| `.terrain` file | `io/asset/terrain_file.h` `TerrainFile::VERSION`   | Anything but an exact match. It is authored source, not derived: a bump leaves every project's terrains unreadable until a one-off pass re-writes them. Its cooked copy is the body alone, behind `COOKER_VERSION`. |

For scenes and prefabs, **bumping the constant does not stop an old file
loading.** The gate refuses the future, not the past: an older file is read by
the current reader and a removed field comes back as its default. Say in the
commit what an old file will do. If the answer must be "refuse it", that changes
what the gate does and belongs with the owner.

Round-trip any format change against what ships:
`examples/the_lab/scenes/obstacle_course.json` is the saved scene of record.
It is ported with the change, and must load and save back unchanged
([implementation.md](implementation.md#7-changing-a-shape-its-users-rely-on)).

---

## 6. Facts that have cost real time

Things that are true, not obvious, and have each produced a wrong result someone
had to chase:

- **Forward is -Z and screen-right is +X**, right-handed with +Y up - glm's own
  convention. Screen-right is `cross(forward, up)`. The convention lives in
  `Math::computeForward` and its siblings; every place that ever broke was a
  place that hand-rolled an axis.
- **A compiler cache serves builds CMake never mentions.** Where ccache is
  installed, a build timing taken without `CCACHE_DISABLE=1` is not a
  measurement.
- **A cached build hides a header that does not compile on its own.** Compile
  it alone, with the flags from `build/compile_commands.json`, before trusting
  that it includes what it uses.
