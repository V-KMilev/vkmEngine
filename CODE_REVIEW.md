# Code Review: vkmGL + vkmEngine

*Date: 2026-05-29 · Branch: vkm/dev/shading*

**Scope.** The hot draw path was read directly (forward pass, GLView sync + shader-variant
cache, render graph, instance batcher, `RenderView::build`, the engine loop, occlusion path,
auto-exposure); the rest was covered by parallel review across all GL passes/resources, render
orchestration, ECS/core/memory/resources, the non-render systems, vkmGL itself, and the
editor/tools/serialization layer. ~37K lines in `src/` + ~3.8K in vkmGL's own source
(everything under `modules/vkmGL/modules/` is third-party and excluded).

**Overall health.** This is a well-built engine, far past what `CLAUDE.md`/`MEMORY.md` describe
(those are stale — they describe a simple forward renderer, but there is now a
deferred-prepass + shadow/IBL/GTAO/SSR/TAA/DoF/MB/bloom/OIT stack, a full ImGui editor,
serialization, async loading, and a double-buffered render-thread overlap loop). The shadow-pass
dirty-skip, the GLLights/GLCamera memcmp-before-upload, the material-preview change detection,
and the reflection-driven serializers are all the *right* patterns. Most findings below are about
applying that same discipline consistently, and about the CPU submission path not yet scaling to a
"heavy scene."

The four questions are answered in order, followed by a concrete performance roadmap.

---

## Q1 — Hardcoded / over-specific code that should be generic

### High

- **C++↔GLSL constants are coupled by comment only, with no build-time check.**
`engine_config.h:22-39` (`MAX_LIGHTS=32`, `MAX_SHADOW_CASTERS_2D=6`, `MAX_SHADOW_CASTERS_CUBE=2`,
`NUM_CASCADES=4`) and the `debugMode = 1..14` mapping in `render_view.h` both say "must match the
PBR shader" — silent drift if either side changes. A generated `engine_config.glsl` already exists
for some values; extend it to *all* of these so there's one source of truth.
- **The shader load + hot-reload-watch list is a hand-maintained dual list.**
`engine_app.cpp:56-257` registers ~25 shaders, then a *separate* `watchShader(...)` block
(lines 232-257) must mirror it. Forget a watch line → that shader silently loses hot reload.
Make it one `{name, path, samplers, variantAware}` table iterated once.
- **The render pipeline pass list is a literal `std::vector<std::string>` in app code**
(`engine_app.cpp:199-220`) — the comment even says "unblocks data-driven pipeline configs."
It should be loaded from a config asset.
- **Project paths are string-concatenated everywhere** (`APP_ROOT_DIR + "/scenes/..."`,
`/screenshots`, `/imgui.ini`, `/assets/envs/...`) across `scene_io_controller.cpp:81`,
`environment_inspector.cpp:239`, `engine_app.cpp:40`, etc. ~15 duplicated literals; one
`ProjectPaths` helper fixes it.
- **Enum↔string tables duplicated 2-4×.** `MaterialType` names appear in `component_serializer.cpp`,
`asset_serializer.cpp` (twice), and the material-editor combo, with an explicit "Order must match
the enum" warning at `material_editor.cpp:80`. `LightType` similarly. One `enumNames<T>()` table
kills all copies.

### Medium

- `**MSAA_SAMPLES = 4` is a hardcoded constant** (`gl_scene_target.h:240`) — the one rendering
quality knob with no config struct, while everything else (AO, SSR, bloom, shadow) has one.
- **Magic tuning constants buried in passes**: GTAO `u_bias=0.02f`/`u_power=1.5f`
(`gl_gtao_pass.cpp:63`), bloom `UPSAMPLE_RADIUS=0.005f`, CSM `lambda=0.6f` / `zext=radius*6.0f`
(`gl_shadow_pass.cpp:106`), IBL bake sizes (512/32/512). These should live in their config structs
so they're tunable per-target-hardware.
- **Folder-material texture discovery uses per-channel hardcoded synonym lists**
(`material_loaders.cpp:126-158`) — a textbook `{slot, patterns[], srgb}` table.
- **vkmGL `InstanceBuffer` is named generic but hardcodes `glm::mat4` + a fixed 4-vec4 attribute
layout** (`gl_instance_buffer.h:43`). Either rename `InstanceMatrixBuffer` or template it.
- `**Name::value[64]` silently truncates** (`name.h:14`); **OpenGL version + 1920×1080 window size
are `#define`s in the "backend-agnostic" platform window** (`window.h:11-15`, with an existing
`// TODO: move to config`).

### Bug found while reviewing this slice

- In vkmGL `gl_texture.cpp:11-12`, `#define STBI_ONLY_PNM` appears *after* `#include "stb_image.h"`,
so it has no effect — the restriction is silently not applied.

---

## Q2 — Architectural issues

### High

- **The render-graph resource model contradicts itself.** `RGResource` is a *closed enum*
(`render_graph_resource.h:27-46`) with fixed-size arrays sized by `RG_RESOURCE_COUNT` — adding one
transient means editing 5+ central files. That's the opposite of the *open* type-erased ECS.
Worse, the elaborate descriptor-aware **alias-group solver** in `render_graph.cpp:138-198`
operates on resources that **don't independently exist**: the GL backend collapses
`SceneHDR`+`SceneHDRResolved` onto one object, and `GBufferNormal`/`GBufferPosition`/`AO` all onto
`&m_gbuffer` (`gl_frame_resources.h:49-60`). So the lifetime/aliasing analysis is computed, shown
in the editor, and — by the graph's own admission — never consumed to actually pool memory. It's
dead computation plus a misleading abstraction. Either make resources real (and pool them) or drop
the solver.
- **The backend abstraction is nominal — passes downcast and read `void`*.** `ctx.resource<T>(id)`
is an unchecked `static_cast<T*>(void*)` (`render_graph_context.h:46-78`); every pass does
`static_cast<GLBackend&>(backend)` after a runtime `getType()` guard repeated ~18× verbatim. A
wrong `T` is silent UB. If a second backend is real intent, this boundary needs type tags / a
registry; if it isn't, the abstraction is paying cost for nothing.
- **Passes own CPU resources and do file I/O / image synthesis inside `execute()`.** Composite
generates a 512² lens-dirt texture and loads a color-grade LUT from disk (`gl_composite_pass.cpp`),
lens-flare synthesizes a starburst, forward owns the SSS LUT. This violates the threading contract
documented in `render_graph_context.h:35-38` ("a pass must not touch process singletons"). And
`gl_hiz_pass.cpp:116` *publishes to a global `OcclusionOracle` singleton from inside execute()*,
and exposure mutates backend state read by the editor — both will be races the moment the render
thread split tightens.
- **No LOD and no culling beyond CPU frustum/instancing.** Instancing only merges *identical*
`(mesh, material)` runs (`gl_instance_batcher.cpp:33-63`). A "heavy scene" with many *distinct*
meshes degenerates to one draw call per unique mesh with zero LOD — this is the structural ceiling
for the 60-FPS-heavy goal, more than any single pass.

### Medium

- `**Scene::forEach` has divergent const/non-const semantics.** The non-const overload *lazily
creates* storage for every queried type (`scene.h:199-216`) while the const one early-returns if
missing. A read-only query through a `Scene&` can silently allocate component sets, and it mutates
`m_components` — undercutting the "safe to swap between frames" assumption. The hot path
`render_view.cpp:226` takes a non-const `Scene&`. Make non-const `forEach` use `findStorage` +
early-return like the const path.
- `**forEach` + structural mutation in the callback is unsafe and undocumented.** `SparseSet::remove`
is swap-and-pop (`sparse_set.h:73-94`); destroying the iterated entity mid-`forEach` skips the
moved element. Worth a loud doc warning at minimum.
- **The thread pool is not work-stealing.** It's a single `std::deque` behind one global mutex
(`thread_pool.cpp:36-119`); every `parallelFor` chunk pop + every completion decrement locks it.
For tens of thousands of fine-grained cull/animation chunks that's the scaling ceiling, and
re-entrant `parallelFor` silently falls back to fully serial (`thread_pool.h:73-83`).
`MEMORY.md`/`CLAUDE.md` calling it "work-stealing" is inaccurate.
- **vkmGL `Context` caches raster state but not *bindings*.** It early-outs redundant
depth/blend/cull changes (good), but `glUseProgram`/`glBindFramebuffer`/`glBindTexture`/
`glBindVertexArray` are all unconditional (`gl_shader_base.cpp:57`, `gl_buffer.cpp:46`,
`gl_texture.cpp:127`). The forward pass hand-dedups shader/material binds, but ~18 other passes and
all texture binds don't. The bound-object cache belongs *here* in `Context` (its stated job is
"state change limiter"), not scattered. Also: config methods like `Texture2D::setWrap` do
`bind();…;unbind()` as a side effect (`gl_texture.cpp:188`), which would silently corrupt any
binding cache — the fix and the cache should land together (ideally via DSA; the project is on
4.3+).
- `**RenderSystem::update()` is an intentional no-op** (`render_system.cpp:49`) — it's special-cased
by the engine loop, so it isn't really a `System`. The base interface doesn't model the
build/execute split.
- `**GLObject` move-assign doesn't release the handle; every subclass must remember to.**
(`gl_object.cpp:22`) Hand-maintained invariant; a future subclass that forgets leaks a GL handle
silently. And `bind(GLenum target)` is honored by only 2 of ~6 subclasses — a leaky uniform
interface (Sampler's `bind` is a literal no-op).

### Threading model — checked directly, sounder than first feared

The loop (`engine.cpp:139-166`) runs non-mutator systems + `buildView K` overlapping render `K-1`,
with double-buffered views (`m_views[K&1]` vs `(K-1)&1`) and a `mutatesResources()` gate protecting
the ResourceManager. There is **no data race as written**, because `executeFrame` consumes a
fully-snapshotted `RenderView` (`build()` copies model matrices/light data into `DrawableData`,
verified in `render_view.cpp:191-201`) and doesn't touch live `Scene`. *But* the safety rests on two
invariants that are nowhere asserted: (a) `executeFrame` must never read `Scene` or live
`ctx.visibility`, and (b) `buildView` must snapshot everything. Add a debug assertion / document
these — one well-meaning future edit to `executeFrame` that reads a component turns this into a
heisenbug.

---

## Q3 — Making the renderer faster (goal: 60 FPS heavy scene)

Currently ~3.3 ms on a small scene. To hold 16.6 ms on a *heavy* scene requires winning on two
fronts: the **CPU submission path** (currently does a lot of O(scene) work *every frame*, regardless
of motion) and the **GPU pass stack** (currently does redundant work).

### CPU side — the submission path doesn't scale yet

#### Critical

- **Shadow casters are rebuilt by a full-scene walk every single frame.** `render_view.cpp:225-243`:
`scene.forEach<Mesh, Transform>` over **every mesh in the scene** (not the frustum-visible set),
copying a 64-byte matrix into a `DrawableData` each, then **sorted** (another full copy — see
next). On a 13K-entity scene that's ~13K iterations + a sort, *in addition* to the visible
drawables loop, paid even when nothing moved and even when no light casts shadows (lights are
computed *after* this, so it can't even early-out on zero shadow casters). The single biggest
reclaimable CPU cost in `build`. Cache it; invalidate on transform/visibility/material edits; skip
entirely when no shadow-casting light exists.
- `**OcclusionOracle::snapshot()` copies the entire Hi-Z pyramid under a mutex, once per culled
entity.** Verified: `occlusion_oracle.cpp:26-29` returns `m_current` *by value* (deep-copies a
`std::vector<float>`), and `occlusion_culler.h:46` calls it **per entity** from worker threads.
That's tens of thousands of ~32 KB heap copies/frame serialized on one lock. Gated behind `useHiZ`
(off by default), so it's *latent* — but enabling occlusion culling today would make heavy scenes
**slower**, not faster. Snapshot once per frame into `VisibilityContext`, pass by const-ref.

#### High

- `**sortDrawables` Phase-2 re-materializes a full copy of every `DrawableData`.** Verified
`render_view.cpp:89-96`: it sorts cheap 12-byte keys (good) but then does
`sorted[i] = drawables[keys[i].second]` for all N and swaps — so the "avoid 88-byte swaps" win is
undone by a full 96-byte-per-element copy. Runs for *both* drawables and shadowCasters. Permute in
place (cycle-sort via the index array) or keep drawables in a persistent SoA and only sort indices.
- **Per-entity double sparse-set probe for the world matrix.** `render_view.cpp:238-239, 269-270, 320-321`: `scene.has<WorldTransform>(id) ? scene.get<WorldTransform>(id)` probes the same slot
twice, per entity, per loop, per frame. `visibility_system.cpp` already hoists
`storage<WorldTransform>()` once; `render_view.cpp` should copy that.
- **Animated entities defeat the hierarchy dirty-flag optimization.** `animation_system.cpp:62-70`
re-scans all animations serially and `markDirty`s every playing one's whole subtree every frame
(by construction — they move every frame), so `resolveWorldTransforms` recomputes them regardless.
At 20% animated that serial re-dirty + subtree recursion is the main-thread cost. And
`hierarchy_operations.cpp:218` allocates a `std::array<std::vector<EntityId>, 32>` **fresh every
frame** (32 vector ctors + growth churn) — make it a persistent member, cleared not freed (like
VisibilitySystem already does).
- `**materialTypeOf` is a per-frame-rebuilt linear scan** (`render_view.cpp:174-183`). Defensible at
~100 unique materials (the comment's case), but it's O(unique-materials) per drawable and degrades
on a heavy scene with many materials. Make it a persistent cache keyed by material id, invalidated
on the material type-version bump.
- **Visibility gather is the one serial step in an otherwise-parallel cull**; the cullers each
recompute `center`/`halfExtent` independently in the inner loop (`frustum_culler.h:34` + distance
  - screen-size = 3× redundant). Compute once, pass in.

### GPU side — passes doing work that isn't needed

#### Critical

- **The prepass runs a full opaque re-draw even when nothing consumes the G-buffer.** Verified:
`gl_prepass.cpp` has **no `enabledForView` override** — it only early-outs on empty drawables,
never on "AO/SSR/HiZ/DoF/MB/TAA all disabled." So with the default config (most of those off) you
pay a **second full opaque geometry pass** (into RGBA16F×2 MRT) every frame for nothing. Likely the
biggest reclaimable GPU cost on a heavy static scene. Add `enabledForView` returning false when no
consumer is active; same for **GTAO** (`gl_gtao_pass.cpp`, no override, should gate on
`ao.enabled`). The exposure pass already does this correctly (`gl_exposure_pass.cpp:23`) — copy
that pattern.

#### High

- **Redundant full-res MSAA resolves stack up.** The graph resolves SceneHDR→Resolved whenever a
pass reads Resolved after a SceneHDR write (`render_graph.cpp:294`). SSR, LensFlare, and OIT-resolve
each write SceneHDR and re-dirty it, so with the post stack on you pay 3+ full-res 4×MSAA→RGBA16F
resolve blits/frame. SSR and LensFlare also additively blend *into the MSAA target* (4× the
fragment cost). Run those on the resolved single-sample target and resolve once.
- **The overlay attachment is MSAA-resolved every frame even when no overlay pass drew.**
`gl_composite_pass.cpp:231` calls `resolveOverlay()` unconditionally; the overlay is cleared to
transparent each frame, so the blit usually moves zeros (and forces a rebind dance). Gate on "did
any AABB/Grid/Outline pass run" — the graph already knows.
- **Blocking `glGetTexImage` in auto-exposure, purely for an editor readout.** Verified
`gl_auto_exposure.h:112` + `gl_exposure_pass.cpp:101`: every frame auto-exposure is on, it stalls
the pipeline to read back 1 pixel so the editor can show a number. Same pattern in HiZ
(`gl_hiz_pass.cpp:106`, `glReadPixels` + per-frame `std::vector` alloc). Throttle to a few Hz,
double-buffer via PBO, or skip when the card isn't visible.
- **vkmGL instance/UBO upload is a bare per-frame `glBufferSubData` on `STREAM_DRAW`**
(`gl_instance_buffer.h:43-64`) with no orphaning / persistent mapping → implicit sync risk every
frame. Orphan (`glBufferData(size,nullptr)` then sub-data), or `glMapBufferRange` with
`INVALIDATE|UNSYNCHRONIZED`, or a persistent-mapped triple-buffered ring (4.4).
- **All transient targets are allocated regardless of feature enablement**
(`gl_frame_resources.h:38-46`) — TAA history, OIT pair, full HiZ pyramid, post scratch all
allocated at every resize even when those passes are off. Tens of MB idle VRAM at 4K. Allocate
lazily on first enable.

#### Medium

- DoF/SSR run full-res (both are half-res candidates; GTAO and bloom already correctly half-res).
G-buffer stores full view-space position as RGBA16F×2 when it's reconstructable from depth (halve
the MRT write). `glValidateProgram` runs on every link in release (`gl_shader_base.cpp:102`) —
gate behind `!NDEBUG`. Viewport/scissor are uncached in `Context`.

---

## Q4 — File reorganization

### Clear misplacements (do these)

- `**src/editor/input/editor_actions.*` is not input handling** — it's entity
create/duplicate/delete, material dup, scene framing, model-import. Even `editor_system.h:13` has a
clarifying comment because the path misleads. Move to `editor/framework/` (peer to
`editor_commands`, which it calls). Only `editor_keybinds` belongs in `input/`.
- `**src/engine/io/screenshot.*` isn't serialization** — it depends on `WindowManager` +
`RenderBackend` and writes viewport PNGs. `engine/io/` otherwise is scene/component/asset JSON.
Move to `platform/` or `system/render/`.
- `**EnvironmentConfig` is a Scene component but lives outside `ecs/component/`** and isn't in
`components.h`, despite being queried via `forEach<EnvironmentConfig>` all over. Move it next to the
other components.
- `**gl_sss_lut.{h,cpp}` is in `resource/` but is a generator, not a GPU-object wrapper** — and the
dirt/starburst generators are inlined in their pass `.cpp`s. Pull all CPU image synthesis into
`tools/generator/` (or a backend `gl_texture_gen`), consistent with how SSS-LUT was *almost*
factored out.

### Worth splitting

- `**render_view.h` (578 lines) is a god-header**: it holds `DrawableData`/`CameraData`/`LightData`
(the actual view payload) *plus* ~18 per-effect config structs, `RenderMode`, `EnvironmentConfig`,
and a 110-line inline `resolveModeConfig` switch — all dragged into every pass TU. Split the config
zoo + `resolveModeConfig` into `render_config.{h,cpp}`. (`environment.cpp` is nearly empty and even
mis-named — it's just ECS accessors, not config.)
- `**environment_inspector.cpp` (803 lines)** is a self-contained sub-editor doing double duty as
both the inspector body and the Render Settings window; candidate for a `panels/environment/` split.

### Inconsistency to settle

- vkmGL mixes header-only (`gl_instance_buffer.h`, `gl_texture_cube.h`, `gl_screen_triangle.h`) with
split-impl siblings, and the comments admit the deciding factor was "avoid touching the build
file," not design — the header-only ones carry non-trivial GL logic that bloats every TU. Pick a
rule.

### Correctness bug to fix regardless of org

- `**MaterialPreviewSession::evict()` is dead code** (zero callers) — preview thumbnails are keyed by
recyclable handle id, so a deleted asset whose id is reused returns a stale thumbnail. Wire
`evict()` into asset deletion, or key by `AssetId` (GUID). (`material_preview_session.cpp:33`)

---

## Suggested order of attack for the 60-FPS-heavy goal

Cheap, high-impact, low-risk first:

1. **Add `enabledForView` to prepass + GTAO** so they don't run when no consumer is active. (Biggest
  free GPU win; mirrors the exposure pass already written.)
2. **Stop rebuilding `shadowCasters` from a full scene walk every frame** — cache + dirty-invalidate,
  skip when no shadow-casting light. (Biggest free CPU win.)
3. **Kill the per-frame full `DrawableData` copies** in `sortDrawables`/`sortTransparentsByDepth`
  (permute in place) and the **double `WorldTransform` probe** in `render_view.cpp`.
4. **Gate the overlay MSAA resolve**, **make MSAA count configurable**, and **lazily allocate**
  TAA/OIT/HiZ targets.
5. **Throttle/PBO the exposure + HiZ CPU readbacks.**
6. **Add a bound-object cache to vkmGL `Context`** (program/FBO/texture/VAO) + fix instance-buffer
  orphaning. (Driver-overhead win that scales with draw count.)
7. Persist the hierarchy bucket array; snapshot the occlusion pyramid once/frame *before* turning
  occlusion on.

Then the **structural** items that actually unlock "heavy": a real **LOD** system and **draw-call
reduction beyond identical-mesh instancing** (multi-draw-indirect / GPU-driven culling) — without
these, a scene of many *distinct* meshes is draw-call-bound no matter how clean the passes are.
Before any of this, **profile** to confirm whether the heavy scene is CPU- or GPU-bound — Tracy GPU
zones and the GPU timing pool are already wired, so a captured frame on a representative heavy scene
will say which half of this list to prioritize.

---

## Master findings table

Priority key: **C** = Critical · **H** = High · **M** = Medium · **B** = Bug · **N** = Note.


| #   | Q   | Pri | Finding                                                                                                     | Location                                                                                  | Suggested fix                                           |
| --- | --- | --- | ----------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------------- |
| 1   | Q1  | H   | C++↔GLSL limits coupled by comment only                                                                     | `engine_config.h:22-39`, `render_view.h`                                                  | Generate all into `engine_config.glsl`                  |
| 2   | Q1  | H   | Shader load + hot-reload-watch are a dual hand-kept list                                                    | `engine_app.cpp:56-257`                                                                   | One `{name,path,samplers,variantAware}` table           |
| 3   | Q1  | H   | Pipeline pass list is a literal vector in app code                                                          | `engine_app.cpp:199-220`                                                                  | Load from config asset                                  |
| 4   | Q1  | H   | Project paths string-concatenated ~15×                                                                      | `scene_io_controller.cpp:81`, `environment_inspector.cpp:239`, `engine_app.cpp:40`        | `ProjectPaths` helper                                   |
| 5   | Q1  | H   | Enum↔string tables duplicated 2-4×                                                                          | `component_serializer.cpp`, `asset_serializer.cpp`, `material_editor.cpp:80`              | Single `enumNames<T>()` table                           |
| 6   | Q1  | M   | `MSAA_SAMPLES=4` hardcoded, no config                                                                       | `gl_scene_target.h:240`                                                                   | Move to a quality config struct                         |
| 7   | Q1  | M   | Magic tuning constants buried in passes                                                                     | `gl_gtao_pass.cpp:63`, `gl_shadow_pass.cpp:106`                                           | Move to per-effect config structs                       |
| 8   | Q1  | M   | Per-channel hardcoded texture synonym lists                                                                 | `material_loaders.cpp:126-158`                                                            | `{slot,patterns[],srgb}` table                          |
| 9   | Q1  | M   | `InstanceBuffer` named generic, hardcodes mat4 + 4-vec4 layout                                              | `gl_instance_buffer.h:43`                                                                 | Rename `InstanceMatrixBuffer` or template               |
| 10  | Q1  | M   | `Name::value[64]` truncates; GL version/size `#define`d in platform window                                  | `name.h:14`, `window.h:11-15`                                                             | Move to config                                          |
| 11  | Q1  | B   | `#define STBI_ONLY_PNM` after `#include` → no effect                                                        | `gl_texture.cpp:11-12`                                                                    | Move define before include                              |
| 12  | Q2  | H   | `RGResource` closed enum + alias solver on resources that don't independently exist (and is never consumed) | `render_graph_resource.h:27-46`, `render_graph.cpp:138-198`, `gl_frame_resources.h:49-60` | Make resources real & pool, or drop solver              |
| 13  | Q2  | H   | Backend abstraction nominal: `void`* cast + `getType()` downcast ×18                                        | `render_graph_context.h:46-78`                                                            | Type tags / registry, or drop the abstraction           |
| 14  | Q2  | H   | Passes do file I/O / singleton writes inside `execute()`                                                    | `gl_composite_pass.cpp`, `gl_hiz_pass.cpp:116`                                            | Backend-owned resources built at init                   |
| 15  | Q2  | H   | No LOD; instancing merges only identical (mesh,material)                                                    | `gl_instance_batcher.cpp:33-63`                                                           | LOD system + MDI / GPU-driven culling                   |
| 16  | Q2  | M   | `Scene::forEach` const/non-const diverge; non-const lazily creates storage                                  | `scene.h:199-216`                                                                         | Non-const uses `findStorage` + early-return             |
| 17  | Q2  | M   | `forEach` + structural mutation in callback skips elements                                                  | `sparse_set.h:73-94`                                                                      | Loud doc warning / deferred-removal                     |
| 18  | Q2  | M   | Thread pool is single-mutex deque, not work-stealing; nested falls back to serial                           | `thread_pool.cpp:36-119`, `thread_pool.h:73-83`                                           | Per-worker deques w/ stealing                           |
| 19  | Q2  | M   | `Context` caches raster state but not bindings; config methods bind/unbind as side effect                   | `gl_shader_base.cpp:57`, `gl_texture.cpp:127,188`                                         | Central binding cache via DSA                           |
| 20  | Q2  | M   | `RenderSystem::update()` is a no-op; not really a `System`                                                  | `render_system.cpp:49`                                                                    | Model build/execute split in base                       |
| 21  | Q2  | M   | `GLObject` move-assign doesn't release; `bind(target)` honored by 2/6 subclasses                            | `gl_object.cpp:22`                                                                        | NVI `releaseHandle()`; fix bind interface               |
| 22  | Q2  | N   | Overlap loop sound but rests on unasserted invariants                                                       | `engine.cpp:139-166`, `render_view.cpp:191-201`                                           | Assert: `executeFrame` reads only snapshot              |
| 23  | Q3  | C   | Shadow casters rebuilt via full-scene walk + sort every frame                                               | `render_view.cpp:225-243`                                                                 | Cache + dirty-invalidate; skip if no shadow light       |
| 24  | Q3  | C   | `OcclusionOracle::snapshot()` deep-copies pyramid under lock, per entity                                    | `occlusion_oracle.cpp:26-29`, `occlusion_culler.h:46`                                     | Snapshot once/frame into `VisibilityContext`            |
| 25  | Q3  | H   | `sortDrawables` Phase-2 full `DrawableData` copy (drawables + casters)                                      | `render_view.cpp:89-96`                                                                   | Permute in place / sort indices only                    |
| 26  | Q3  | H   | Per-entity double `has`+`get` WorldTransform probe                                                          | `render_view.cpp:238-239,269-270,320-321`                                                 | Hoist `storage<WorldTransform>()` once                  |
| 27  | Q3  | H   | Animated entities re-dirty subtrees every frame; per-frame `array<vector,32>` alloc                         | `animation_system.cpp:62-70`, `hierarchy_operations.cpp:218`                              | Persist buckets; cache depth on `Hierarchy`             |
| 28  | Q3  | H   | `materialTypeOf` linear scan rebuilt per frame                                                              | `render_view.cpp:174-183`                                                                 | Persistent cache keyed by material id                   |
| 29  | Q3  | H   | Serial visibility gather; cullers recompute center/halfExtent 3×                                            | `frustum_culler.h:34` + distance/screen-size                                              | Compute bounds once; parallel/SoA gather                |
| 30  | Q3  | C   | Prepass does full opaque re-draw with no `enabledForView`                                                   | `gl_prepass.cpp`                                                                          | Gate on any G-buffer consumer active; same for GTAO     |
| 31  | Q3  | H   | 3+ redundant full-res MSAA resolves; SSR/LensFlare blend into MSAA                                          | `render_graph.cpp:294`                                                                    | Run on resolved target; resolve once                    |
| 32  | Q3  | H   | Overlay MSAA resolve runs unconditionally                                                                   | `gl_composite_pass.cpp:231`                                                               | Gate on "any overlay pass ran"                          |
| 33  | Q3  | H   | Blocking `glGetTexImage`/`glReadPixels` readbacks for editor numbers                                        | `gl_auto_exposure.h:112`, `gl_exposure_pass.cpp:101`, `gl_hiz_pass.cpp:106`               | Throttle / PBO / skip when card hidden                  |
| 34  | Q3  | H   | Per-frame `glBufferSubData` on STREAM_DRAW, no orphaning                                                    | `gl_instance_buffer.h:43-64`                                                              | Orphan / persistent-mapped ring                         |
| 35  | Q3  | H   | All transient targets allocated regardless of feature enablement                                            | `gl_frame_resources.h:38-46`                                                              | Lazy alloc on first enable                              |
| 36  | Q3  | M   | DoF/SSR full-res; G-buffer position RGBA16F; `glValidateProgram` in release; viewport uncached              | `gl_shader_base.cpp:102`, G-buffer, `Context`                                             | Half-res, reconstruct from depth, gate `!NDEBUG`, cache |
| 37  | Q4  | H   | `editor_actions.*` misfiled under `input/` (not input handling)                                             | `src/editor/input/editor_actions.*`                                                       | Move to `editor/framework/`                             |
| 38  | Q4  | M   | `screenshot.*` in `io/` is a platform/render concern                                                        | `src/engine/io/screenshot.*`                                                              | Move to `platform/` or `system/render/`                 |
| 39  | Q4  | M   | `EnvironmentConfig` is a component but outside `ecs/component/`                                             | (definition site)                                                                         | Move next to components; add to `components.h`          |
| 40  | Q4  | M   | `gl_sss_lut.*` + inline dirt/starburst gens are generators in wrong place                                   | `gl_sss_lut.*`, `gl_composite_pass.cpp`, `gl_lens_flare_pass.cpp`                         | Consolidate into `tools/generator/`                     |
| 41  | Q4  | M   | `render_view.h` (578 lines) god-header dragged into every pass TU                                           | `render_view.h`                                                                           | Split config zoo → `render_config.{h,cpp}`              |
| 42  | Q4  | M   | `environment_inspector.cpp` (803 lines) self-contained sub-editor                                           | `environment_inspector.cpp`                                                               | Split into `panels/environment/`                        |
| 43  | Q4  | M   | vkmGL inconsistent header-only vs split-impl                                                                | vkmGL `buffer/`, `texture/`                                                               | Pick and document a rule                                |
| 44  | Q4  | B   | `MaterialPreviewSession::evict()` dead code → stale thumbnail on id reuse                                   | `material_preview_session.cpp:33`                                                         | Wire to asset deletion or key by `AssetId`              |

---

## Editor Feature-Coverage Audit (2026-05-29)

Follow-up pass answering "is the editor up to date with everything the engine
provides?" Method: a multi-agent inventory of engine features (ECS components,
the render/view + post stack, resources/assets) cross-checked against what the
editor UI actually exposes, plus an adversarial pass to clear false "not
exposed" claims.

**Headline: the editor is in very good shape.** The entire render/post stack is
fully surfaced - all 20 render passes (per-pass enable), AO/SSR/TAA/DoF/motion
blur/bloom/auto-exposure/lens flare/color-grade LUT/shadows/OIT/Hi-Z/IBL/tonemap,
all 19 RenderMode diagnostics, every PBR material parameter + texture slot, all
5 light types, cameras, and reflection probes. Gaps are concentrated, not
systemic.

### Gap table

| #   | Gap                                                                   | Sev  | Effort  | Status                          |
| --- | --------------------------------------------------------------------- | ---- | ------- | ------------------------------- |
| E1  | LOD authoring (inspector card, add/remove, per-level mesh + decimate)  | High | medium  | DONE (UI); persistence pending  |
| E2  | LOD persistence - serialize `MeshLOD` + a "decimate" asset source      | High | medium  | open                            |
| E3  | Primitive creation - Plane/Triangle/Pyramid/Cone (only Cube/Sphere)    | Med  | small   | open                            |
| E4  | "New Material" - no create-from-scratch button                         | Med  | small   | open                            |
| E5  | Animation keyframe authoring (playback only today)                     | Med  | large   | open                            |
| E6  | MSAA sample count - hardcoded 4x, no field/UI                          | Med  | medium  | open                            |
| E7  | ReflectionProbe HDR Browse button + duplicate/undo snapshot drop       | Low  | small   | partial (snapshot fixed w/ E1)  |
| E8  | TextureParams + procedural texture generators - no UI                  | Low  | medium  | open                            |
| E9  | Skybox show/hide toggle, manual IBL rebake, clearColor alpha           | Low  | trivial | open                            |

### Detail + suggested hooks

- **E1 - LOD authoring (done):** added a "Level of Detail" inspector card
  (`inspector_panel.cpp` `drawMeshLODSection`): Levels slider, per-level mesh
  picker (shared `pickAsset`, lifted out of the Mesh section), switch-height
  threshold, and a Decimate button calling `Engine::decimateMesh`. Wired into
  the draw chain + Add Component menu, with `AddComponentCommand<MeshLOD>` /
  `RemoveComponentCommand<MeshLOD>`, and `MeshLOD` added to `EntitySnapshot` +
  `duplicateEntity`.
- **E2 - LOD persistence (open):** `MeshLOD` is not in `component_serializer`,
  and decimated levels carry no reload source, so save/load silently drops LOD.
  Needs a serializer entry + a "decimate" asset source (`{base AssetId,
  gridResolution}`) + an AssetFactory that re-decimates the base on load.
- **E3 - primitives:** `generatePlane/Triangle/Pyramid/Cone` exist and are
  JSON-registered but absent from the Create menu. Extend `EntityKind` +
  `createEntity` + `drawCreateEntityMenu` (Plane = floor, most-wanted).
- **E4 - new material:** `generateDefaultMaterial` is only reachable as a side
  effect of creating geometry, duplicating, or loading a PBR folder. Add a "New
  Material" button to the Asset Browser Materials tab / Material Editor toolbar.
- **E5 - animation keyframes:** the inspector shows playback + key counts only;
  no add/edit/curve. Likely a dedicated dockable timeline panel; at minimum
  expose the explicit `Animation::length` field.
- **E6 - MSAA:** `MSAA_SAMPLES = 4` is a compile-time constant
  (`gl_scene_target.h`). Add `EnvironmentConfig.msaaSamples`, reallocate the
  scene target on change, expose an Off/2x/4x/8x combo.
- **E7 - reflection probe:** HDR path is a bare InputText (no Browse, unlike the
  IBL slot). The duplicate/undo snapshot drop is fixed alongside E1 (added to
  `EntitySnapshot` + `duplicateEntity`); the Browse button is still open.
- **E8 - textures:** no UI for `TextureParams` (wrap/filter/format/mipmaps) or
  the procedural generators (solid/white/black/normal/gray).
- **E9 - misc:** the skybox can only be suppressed via the advanced per-pass
  list (no dedicated toggle); IBL bake is automatic (no manual Rebake); and
  `clearColor` is a `vec4` edited via `ColorEdit3` (alpha never editable).

### Cleared false positives

Lens-dirt and starburst are fully exposed (the structs have no further fields);
the per-pass list *does* cover the skybox (so it can be disabled, just not
obviously); and ReflectionProbe add/remove *is* undoable - only the duplicate /
snapshot path dropped it.


