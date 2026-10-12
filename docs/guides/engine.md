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
| Contact shadows | Removed, then adopted in 1.0.7 as Bend Studio's screen-space shadows: the removed technique needed a temporal filter, and GTAO, which is ambient occlusion, cannot cast the sun's shadow. Bend's has no noise and no history - each pixel compares 60 contiguous depths toward the light, shared by its wavefront - so a still frame shades the same every time. 0.17 ms in physics_lab at 1920x1053 on an RTX A4000 ([lighting.md](../reference/lighting.md#screen-space-shadows)). |
| Screen-space reflections | Adopted, without a temporal filter: probes cannot show what moves or what lies between them. Coherent rays and a neighbour-averaged resolve stand in for TAA ([rendering.md](../reference/rendering.md)). |
| A hierarchical (Hi-Z) walk for the reflection trace | Built, measured, reverted. A nearest-depth pyramid walked as McGuire and Mara and Uludag describe found the thin side faces and contact lines the 16-48-step march skips in the mirror goldens, at about the march's GPU time there (-0.06 to +0.10 ms on a 0.3-1.0 ms pass at 1080p). But building the pyramid cost 0.07 ms of CPU every frame, and on physics_lab the pass took 0.11 ms more GPU (0.40 -> 0.51 ms) with nothing different to see: a ray grazing a receding floor descends at almost every cell, because the floor nearer the eye shares it. Revisit when a project's glossy surfaces visibly lose thin geometry. |
| Cone specular occlusion from GTAO's bent normal (GTSO) | Built, measured, reverted in 1.0.6. Jimenez's share of the specular lobe's cone inside the cone GTAO finds open darkens a glossy floor's reflection of the horizon by half even with that cone opened to the whole hemisphere: near the horizon half the lobe lands on the floor, which has light of its own, and the cone counts it as black (glint golden 14-25% of pixels, reflections 7%). Its aperture is also steep in the visibility near one - GTAO's 0.99 on an open floor closes it to 84 degrees. Lagarde's AO-only fit stays. Revisit with a specular occlusion that reflects the occluder's radiance rather than black. |
| Visibility-bitmask GTAO (Therrien et al. 2023) | Built, measured, reverted in 1.0.6. Sectors occluded only between a sample's surface and a face a fixed thickness behind it should leave the wall behind a pole lit; but the horizon search's distance falloff already keeps a halo inside the 0.6 m radius - a 5 cm pole half a metre from a wall left no halo - while the bitmask's thickness, half the radius, made the pole occlude as a 30 cm slab and darkened a band each side of it. Revisit with a thickness taken from the scene rather than assumed. |
| SDFGI / voxel GI | Never built, not planned. Indirect diffuse is a baked SH-L1 irradiance volume, reflection probes and GTAO. |
| Auto-exposure | Rejected. The tonemap curve is fixed and chosen by the project. |
| Shadow bias units | Texels of the map at the receiver, for every light (Castano 2013): one pair holds for any range, cascade or distance, where a range-relative bias held for one light size. The defaults, 0.5 slide and 1.0 normal offset, were measured on pillars and a curtain 2 cm off a wall: no acne, and the curtain keeps its shadow, which a normal offset three times larger lost. |
| Shadow atlas tile sizes | The 2D atlas is 2x2 blocks of `shadowResolution`, each tile a power-of-two square within them. The sun's nearest three cascades take a whole block and the last takes half (`FULL_CASCADES`): at two, the sixfold step in texel size at the third split showed where the sun's soft search crossed it (the penumbra golden), and all four would leave the spots no room. A spot's tile follows how much of the screen its cone covers. |
| ACES's 1/0.6 pre-scale | Dropped. Hill's fit as three.js applies it lifts every scene authored here by three quarters of a stop, and the examples read washed out. |
| The sky's brightness | Lit by the scene's sun (`Atmosphere::solarIlluminance`), with no constant: `lightIntensity` is an illuminance and the bake's scattering is per steradian, so the units agree. A separate `sunIntensity` of 22 against a light of 3 put the sky's light on a horizontal floor at 40% of the sun's at 50 degrees, and 57% once multiple scattering was added. Under one sun, clear air (`mie` 1) gives 11% at 50 degrees and 15% at 30; clear daylight is 15-25%, the rest haze, so the default `mie` is 5: 18% at 50 degrees, 26% at 30. At 8 and above the sky turns milky around the sun. Tune it as `mie`, not as a constant. The default `exposure` is a stop up to match: under one sun, shade holds about a sixth of the light the separate sky gave it, and a stop up brings shade and sky back into the display's range while ACES rolls off the sunlit whites. |
| The sky's ground albedo | `Atmosphere::GROUND_ALBEDO` 0.3, Hillaire's (Unreal's sky ships 0.4): past a scene's floors the ground reads as they do, and a metal reflects it so. Under one sun Bruneton's 0.1 left a dark band below the horizon, and 0.03 a void. |
| The sky drawn from a sky-view table | Adopted, Hillaire's: the skybox reads a table computed every frame rather than the baked env cube, so the sky drawn is always the current sun's, and the horizon is as sharp as the table is dense there, not a cube texel. On an RTX A4000 the table costs 0.018 ms a frame and the aerial-perspective volume beside it 0.030 ms; away from the horizon the table and the cube agree within 3%. |
| The sky's lighting baked a step a frame | Adopted, as Unreal time-slices its sky capture. Baked whole, the procedural sky's env cube and its convolutions take about 13.5 ms of GPU on an RTX A4000, once for every half degree the sun moves: a frame dropped every few seconds of a day-night cycle. Spread over `GLIBLBaker::slicedSteps` frames, a step costs a median 0.23 ms and at most 1.25 ms in a game scene at Ultra whose sun never stops: a bake starts each half degree, 0.08 ms a frame on average. The price is a second set of cubes (33 MB) and ambient light trailing a moving sun by up to two bakes; a jump past `SkyParams::DRIFT`, or any other change, still bakes whole, so no capture taken meanwhile shows a sky the scene has left. A sun or moon run ahead of the shown sky - a time-lapse - takes a step more a frame for each `GLIBLBaker::CATCH_UP_DEGREES` it is behind, up to `GLIBLBaker::MAX_CATCH_UP_STEPS` (`GLIBLBaker::stepsFor`), so its light keeps pace rather than popping in two bakes late; a sun within that of the shown sky still takes one. The cap is the frame's: six of the costliest steps in a row are 1.9 ms on an RTX A4000, twelve 3.4. |
| Importance-sampled sky convolutions | Adopted. An irradiance texel was a 15,876-sample grid over the hemisphere, and every prefilter level took 1024 GGX samples a texel. Each now draws its samples by its own lobe - 1024 by the cosine, 256, 512 or 1024 by roughness - and reads the env level its share of the lobe covers (Colbert and Krivanek, GPU Gems 3 ch. 20). Against the old bake and an 8192-sample reference the levels land within 0.6% without Colbert and Krivanek's +1 level and cube-distortion term, the first rough level within 1.1%, the irradiance 0.3% brighter on average; under skies with an answer in closed form - one radiance everywhere, half a sky bright - the render suite holds them within a percent. Each sample reads its level off the env cube's whole mip chain, a probe's too: cut at five levels, a 1024 probe's samples each spanned six texels of its last. The whole bake went from 37 to 13.5 ms; an irradiance face, 3-4 ms when split by azimuth to keep each step under 0.78 ms, is now one step of about 0.12 (timed offscreen, the GPU at full clock). |
| A smaller env cube for the procedural sky | Not adopted. Drawn from the sky-view table, the procedural sky does not need the cube sharp, but halving it, measured when the whole bake took 32 ms, saved 7, and a mirror would lose the horizon's edge. |
| Aerial perspective's default | The planet's air: `aerialPerspective` 1, Unreal's distance scale at 1. Under the default sky a surface keeps 99.3% of its light at 100 m, 95% at 760 m and 83% at 3 km, so a scene of a few hundred metres barely changes and one of kilometres fades into the horizon as the sky beside it does. Another scale is for a world not built to scale; haze is `mie`, which the sky shows too. |
| Fog read B-spline filtered across the screen | Adopted. Read trilinear alone, a lamp's glow a few froxels wide in fog showed every froxel's square. Four trilinear reads weighted as a cubic B-spline (`shaders/bspline.glsl`) hide them for under 2% of the forward pass - 20.07 -> 20.39 ms with fog and lanterns at night, the GPU down-clocked - and a glow one froxel wide comes out broader and dimmer at its peak, as a B-spline smooths rather than passes through its texels. |
| Lighting a particle once, not at each corner | Declined. An alpha-blended puff takes the same light at its four corners, and the vertex stage works it out at each. A pass lighting each puff once would save three of the four: in the render suite on an RTX A4000, 20,000 puffs under 40 lamps took 2.2-2.3 ms a frame lit at every corner, lit at one, and unlit alike. Revisit if a profile shows the particle pass's vertex work. |
| An ocean of Gerstner waves drawn from a spectrum | Adopted, as Unreal's water system ships it: up to 64 waves from JONSWAP (TMA where shallow), summed per vertex on the GPU and by the same arithmetic on the CPU, so a float's query is exact and immediate. The state of the art for open ocean is Tessendorf's FFT of the same spectrum in two or three tiled cascades (HDRP, Crest): thousands of waves, but each gameplay query wants a GPU readback a frame late or a CPU FFT, and the tiles repeat. The spectrum below the waves, which nothing afloat asks about, is FFT's: three tiled cascades, every wavenumber of each one's band a ripple, transformed once a frame in shared memory and read once each a pixel, the longest B-spline filtered in four taps - 0.15 ms in a game's menu view. Before it, 48 sines summed into lattice-snapped fields cost 0.4 ms and still wove a visible pattern; summed per pixel, 32 cost 1.3-1.5 ms of forward pass at Ultra. Waterline foam is read off a world-space shore - the scene drawn looking down over the sea ahead of the eye, then a distance transform - for 0.07 ms: read off the screen's depth instead, it foamed only what the camera saw, hung below things in a straight-sided curtain, and spending 0.8 ms on taps only stepped where they stopped. Revisit when a project's sea is seen from high above or out to the horizon, where 64 waves read as regular. Moving a game's sea off the CPU took about 1.4 ms off its frame's CPU (the shaping and the 1.6 MB upload), for 0.01 ms of vertex work. |
| Shader permutations | Built, measured, reverted: no win. One ubershader branches on material flags at run time. |
| Draw-sort hoisting | Built, measured, reverted: no win. |
| Buffer-stall fixes | Built, measured, reverted: no win. |
| Multi-draw indirect | Adopted. Every mesh lives in one `GLMeshPool`, so a run of different meshes goes out as one `glMultiDrawElementsIndirect` (core 4.3) per change of program or material. Six interleaved runs a side: on a shooter's map the shadow pass's CPU fell 0.71 -> 0.34 ms, the depth prepass 0.40 -> 0.18 and the forward pass 0.98 -> 0.75, the backend's whole frame 3.78 -> 2.88 ms, with GPU time within 1% either way; on stress_arena, whose few meshes were already instanced, 0.02-0.06 ms a pass. The forward pass binds a material per run, for its textures; the prepass binds only its block, for a `waves` surface's vertices. |
| A glm precompiled header | Rejected while `ccache` is active: a one-file rebuild takes 10.8 s without the PCH and 15.4 s with it, because ccache caches a PCH compile badly. Without ccache it is a ~25% win - revisit if ccache goes. |
| TCP for the wire | Rejected. In-order delivery stalls the snapshot describing the present behind one describing the past, and retransmission resends state the next packet supersedes. The design survives loss instead ([networking.md](../reference/networking.md)): a snapshot says again whatever the receiver has not confirmed, so what must arrive - an entity gone, a prefab spawned - is state the next snapshot repeats, not a message a channel resends. |
| Reliable messages from a player | Adopted, upstream only. A move or a name must arrive once and in order; carried as held state in the command payload it cost the game a nonce, an acknowledgement in its replicated state and a round trip per message. They ride every command packet until a snapshot acknowledges them - the commands' redundancy, not a retransmit timer ([networking.md](../reference/networking.md#messages-once-in-order)). Downstream stays state; revisit when a game must tell one player a fact no component holds. |
| A name table for entities on the wire | Adopted on the client alone. The server's slot stays an entity's name, but "a client makes no entity of its own while connected" could not hold where UI and effects are entities. A client maps the server's slots, keeping each one's own slot when it is free so slot order - which replay depends on - is kept ([networking.md](../reference/networking.md#identity-is-the-servers-slot)). |
| Replicating an inactive ragdoll's bones | Rejected: replicate causes, not effects. Inactive, the bones are posed from a clip both ends choose from the replicated velocity; sent, four characters of twenty bones were 96% of every snapshot. An active ragdoll's bones are the answer rather than a copy of it, and all of them travel ([networking.md](../reference/networking.md)). |
| Fixed gains for client tick pacing | Rejected. Gains tuned at 30 ms overshot on every round trip of a 200 ms link and swung between the two bounds about a hundred times a minute; the gains scale with the measured round trip and the tick rate. |
| GJK replacing the narrowphase | Rejected. The hand-written primitive pairs return up to four contact points, which resting stability needs; GJK+EPA returns one. GJK answers overlap alone, for a mesh triangle before its manifold is built, and there is no EPA. |
| A render graph | Rejected. A render graph solves barriers and transient-memory aliasing; OpenGL has neither problem to solve - the driver tracks hazards and there is no aliasing API. The fixed, hand-ordered pass list *is* the design. Revisit only alongside a second backend. |
| A second graphics backend | Not planned. Its real cost is not the port but that every simplification a single backend allows - the pass list, the state model, `RenderView` itself - becomes an abstraction. Revisit if a target platform drops GL, a needed feature exists only in Vulkan, or driver divergence costs more than the port would. |
| Raising the GL floor to 4.5/4.6 | Deferred with a trigger. Direct state access, `glClipControl` and SPIR-V would each delete code, but the value is in *using* 4.5, not requesting it, and using it is a backend rewrite verified only by looking at frames. Do it in one pass, in its own release, at a display - a half-converted backend is two state models at once. |
| A scripting language | Rejected. Gameplay is a native module built against the SDK and hot-reloaded by swapping the library. A second language brings a second toolchain, a marshalling layer and a second debugger, for authors this engine does not have. |
| Thread pool sized by physical cores | Adopted in 1.0.6: one worker fewer than the physical cores, a core's SMT siblings counted once ([threading.md](../reference/threading.md#threadpool)). On an 8-thread 4-module FX-8320, seven workers - one fewer than the logical CPUs - left the main thread waiting a scheduler slice 20-35 times in 40 s of a game scene, three workers 0-9 times, at the same frame rate, with 1% lows of 88-96 fps where they were 80-85. That measures how many threads were runnable beside the driver's, the audio's and the OS's, not how much a `parallelFor` gains from a core's second thread; the stress arena's CPU-bound frame took 6.0-6.2 ms with three workers and 6.1 with seven. Revisit on a many-core SMT machine - a 16-core one now runs 15 workers where it ran 31 - if its parallel stages slow.
| A fiber or DAG job system | Rejected. The engines that run a task graph - Unreal, Bevy, Naughty Dog's fibers - have a render thread or frames in flight to order against the game. This frame is one thread with fork-join `parallelFor` on its hot loops, and a graph buys nothing until there are dependencies to express. |
| Archetype ECS storage | Rejected. Sparse sets lose to archetypes on bulk multi-component iteration at 10^5-10^6 entities; this engine's benchmark scene is 13.7k, and the open, registration-free registry is what makes `scene.add<T>` the whole mechanism. The same design as EnTT. |
| A spatial structure for rendering | Rejected. At this entity count a flat parallel frustum sweep beats a BVH that must be rebuilt or refitted as things move, and has no second representation of where things are to go stale. Physics answers queries separately. |
| GPU occlusion culling | Built, measured, removed. After a depth prepass the forward pass already rejects hidden fragments at the depth test, so a Hi-Z cull could save only the vertex work of hidden instances - 0.13 ms on the most occluded scene - while the pyramid and the cull cost more than that on the GPU and 0.2-0.4 ms of CPU; frames were faster with it off on every project measured. Re-measured in 1.0.6, with the frame GPU-bound rather than CPU-bound: a game scene at Ultra spends 0.23 ms of an 8.4 ms frame in the depth prepass and about as much in the forward pass's vertex work, so even half the geometry hidden would save about 0.25 ms against a pyramid and a cull that cost 0.1 ms or more, while the pixel shading it cannot touch is 4 ms (the sun's soft shadow alone 1.8). Revisit with a vertex-bound scene. |
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
| A UI widget library | Rejected; two controls adopted. UI is ECS primitives - a clipping rect, a scrolling rect, a quad, a string, a hit-tested tint - and a widget is whatever a project parents together. Two projects built the same slider and toggle, so `UISlider` and `UIToggle` are components on a `UIButton`; the look stays the project's. Another control joins them when two projects want the *same* one. |
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
- **`NetSession`** - one object, four states. `simulates(scene, entity)` decides which
  end moves what; offline it answers yes to everything, so single-player is
  unchanged by its existence. `Disconnected` is not bookkeeping: it is the
  opposite of `Offline` on the only question that matters - an offline end
  decides everything, a dropped client nothing of the world it was shown. Owned by `Engine` by
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
