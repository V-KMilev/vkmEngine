# vkmEngine Renderer — 21-Feature Implementation Plan

Grounded in the code at `/data2/vkm/vkm_code/vkmEngine`. Asset/shader IDs, RG resource names, and class hierarchies all reference what's actually present today. Every "Files to change" path was verified by direct read.

---

## Quick wins (hours each)

### 1. Bloom soft-knee threshold

**Approach.** Karis/COD/Jimenez soft-knee curve in the bloom downsample's mip-0 prefilter step: `curve = vec3(threshold - knee, 2*knee, 0.25/knee); soft = max(brightness - curve.x, 0); soft = clamp((soft * soft) * curve.z, 0, soft * curve.y * 0.5); contribution = max(soft, brightness - threshold) / max(brightness, eps)`. Add `BloomConfig::threshold` and `BloomConfig::knee` (defaults ~1.0 / 0.5) and push them when binding the down shader at mip 0 only (the existing `u_karis` already gates that branch).

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_view.h` (extend `BloomConfig`)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/pass/gl_bloom_pass.cpp` (push uniforms at mip 0)
- `/data2/vkm/vkm_code/vkmEngine/shaders/post/bloom_down/fragment.shader` (apply curve when `u_karis==1`)
- `/data2/vkm/vkm_code/vkmEngine/src/editor/panels/environment_inspector.cpp` (Post group → Bloom sub-section)

**New files.** None.

**Touchpoints.** Editor inspector, JSON env serializer (`environment.cpp`).

**Dependencies.** None.

**Risks.** Knee=0 must degenerate to the existing hard cutoff (clamp guard).

**Hours.** 2–3.

---

### 2. Shader error history

**Approach.** Replace the single `LOG_ERROR` at `gl_shader_program.cpp:41` with a process-wide thread-safe ring buffer (`std::mutex` + `std::deque<ShaderErrorEntry>`, cap ~64) in a new `ShaderErrorLog` singleton. Each entry: `{timestamp, shaderName, definesHash, infoLog}`. Surface in editor's Bottom Panel as a new "Shaders" tab (filterable). Hot reload that succeeds clears that shader's entries.

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/resource/gl_shader_program.cpp` (push to log)
- `/data2/vkm/vkm_code/vkmEngine/src/editor/panels/bottom_panel.cpp` + `.h` (tab)

**New files.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/debug/shader_error_log.h`
- `/data2/vkm/vkm_code/vkmEngine/src/engine/debug/shader_error_log.cpp`

**Touchpoints.** vkmGL's `Core::Shader::recompile` throws on failure; the catch at line 40 currently logs once — replace with also-record. Also wire variant compile failures from `GLView::resolveShaderVariant`.

**Dependencies.** None.

**Risks.** Volume of errors during scrub-edits — must dedupe consecutive identical entries.

**Hours.** 3–4.

---

### 3. Expand debug visualizations

**Approach.** Add the new `RenderMode` enums (Roughness, Metallic, Emission, TangentSpace, Overdraw, BatchId, LightComplexity, LightmapUV). Most are PBR-shader debug branches (extend `u_debugMode`). Overdraw needs the forward pass to enable additive blending and write `vec4(1/64,0,0,0)` from a unique variant or a separate `shader:overdraw`. BatchId requires forward to push `u_batchId` per batch. LightComplexity needs the per-fragment light-loop count summed and visualized (turbo/jet ramp). LightmapUV requires UV channel 1 in mesh attributes — out of scope, gracefully shows a "no lightmap UVs" pattern.

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_view.h` (enum + `resolveModeConfig`)
- `/data2/vkm/vkm_code/vkmEngine/shaders/pbr/fragment.shader` (extend debug branch at end of main)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/pass/gl_forward_pass.cpp` (push `u_batchId` per batch; enable blend for overdraw)
- `/data2/vkm/vkm_code/vkmEngine/src/editor/panels/environment_inspector.cpp` (mode picker)

**New files.** Optional: `/data2/vkm/vkm_code/vkmEngine/shaders/post/debug_overdraw/{vertex,fragment}.shader` if a dedicated path is cleaner than blending in PBR.

**Touchpoints.** `RenderModeConfig` (add fields like `additiveBlend`, `int pbrDebugChannel`), unlit shader paths (skipped here — overdraw routes through PBR).

**Dependencies.** None; #14 (tangent-space POM shadow) is orthogonal.

**Risks.** Overdraw + transparent batches double-count; document as opaque-only metric.

**Hours.** 5–8.

---

### 4. Screenshot API outside editor

**Approach.** Promote `captureViewportScreenshot` into a backend-agnostic engine service. `RenderBackend::readbackPixels` already exists (`render_backend.h:221`). Add `Engine::Screenshot::capture(backend, x,y,w,h, windowHeight, outPath)` that owns timestamping + stb_image_write. Optional hook on the engine for headless / scripted capture (e.g., key bound in `main.cpp`).

**Files to change.**
- Move `src/editor/framework/screenshot.cpp/.h` content into engine layer.

**New files.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/io/screenshot.h`
- `/data2/vkm/vkm_code/vkmEngine/src/engine/io/screenshot.cpp`

**Touchpoints.** Editor framework now calls into the engine version; remove duplicate stb include guard.

**Dependencies.** None.

**Risks.** Module placement (currently uses APP_ROOT_DIR macro — keep but allow caller override).

**Hours.** 2.

---

### 5. Shader hot-reload include dependency graph

**Approach.** Track include edges during `processFile()` in `shader_preprocessor.cpp`. Maintain a global `std::unordered_map<canonicalPath, std::unordered_set<ShaderHandle>>` reverse index (a file → consumers map) updated whenever `preprocessShaderFile` runs. On `FileWatcher` change, look up consumers and bump only their `ShaderAsset` version. Also watch `_generated/engine_config.glsl` automatically.

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/tools/loader/shader_preprocessor.cpp` + `.h` (collect deps, expose `lastIncludesFor(path)`)
- `/data2/vkm/vkm_code/vkmEngine/src/tools/loader/shader_loaders.cpp/.h` (`watchShader` should register include files too)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/resource/gl_shader_program.cpp` (call new dep-collecting overload)

**New files.** None (track inside preprocessor).

**Touchpoints.** `FileWatcher`, `GLShader::reloadSource()` (already re-runs preprocessor — keeps existing reload path).

**Dependencies.** None.

**Risks.** Stale entries when an `#include` is renamed/removed — rebuild graph on each preprocess of the top-level shader to overwrite stale dep set.

**Hours.** 4–6.

---

### 6. Runtime graphics settings UI

**Approach.** Add a player-facing in-game "Settings" overlay (ImGui window distinct from editor) bound to F10 / pause menu. Calls `RenderSystem::setPassEnabled` (already exists at `render_system.h:109-110`) and reads from `EnvironmentConfig`. Settings persist in `editor_settings.json` (or a new `player_settings.json`).

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/editor/editor_system.cpp` (or a runtime overlay system if "outside editor" is the goal)

**New files.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/runtime_settings_overlay.h/.cpp` (engine-side overlay registerable independently of editor)

**Touchpoints.** `RenderSystem::passCount/passName/isPassEnabled/setPassEnabled` already exists; just consume.

**Dependencies.** None.

**Risks.** Persistence collision with editor settings — namespace the JSON section.

**Hours.** 4–6.

---

## Medium (1–2 days each)

### 7. Per-pass GPU timing UI

**Approach.** GPU zones already exist via `PROFILE_GPU_SCOPE_NAMED(pass.getName())` inside each backend pass and Tracy collects them on `endFrame()` (`gl_backend.cpp:82`). Add a per-pass timing ring buffer (~120 samples) maintained on the engine side by wrapping pass execute with `glGenQueries(GL_TIME_ELAPSED)`. Surface a millisecond histogram per pass in the Bottom Panel's "Profiler" tab (averages, p99, sparkline).

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_graph.cpp` (wrap each `pass.execute` with backend-provided timer scope)
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_backend.h` (`virtual TimerScope beginGPUScope(name)`)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/core/gl_backend.cpp/.h` (implement query pool)
- `/data2/vkm/vkm_code/vkmEngine/src/editor/panels/bottom_panel.cpp` (UI)

**New files.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/debug/gpu_timing.h/.cpp`

**Touchpoints.** Tracy keeps working in parallel; this just gives the editor an always-on cheap view.

**Dependencies.** None.

**Risks.** `glGetQueryObjectiv` stalls if read same frame — use N-frame lag (3) like Tracy does.

**Hours.** 10–14.

---

### 8. Render-graph visualizer

**Approach.** Per-frame snapshot of the lifetime data already computed at `render_graph.cpp:115-119` (`m_lifetimes`, `m_reads`, `m_writes`). Editor panel renders a horizontal Gantt: rows = `RGResource`, columns = passes, colored intervals = `[firstWrite, lastRead]`, with arrows for cross-references. Click a resource to show producer/consumer detail. Reuses `rgResourceName()`.

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_graph.h` (expose `passReads/passWrites/lifetime/passCount` — already public)

**New files.**
- `/data2/vkm/vkm_code/vkmEngine/src/editor/panels/render_graph_panel.h/.cpp`

**Touchpoints.** Editor panel registration in `editor_system.cpp`.

**Dependencies.** None. Pairs naturally with #7 (same panel can host both).

**Risks.** Dynamic enable changes mid-frame need a stable per-frame snapshot — copy compile state when execute begins.

**Hours.** 8–12.

---

### 9. Auto-exposure with temporal smoothing

**Approach.** Current `gl_exposure_pass.cpp` already uses an exponential ease-toward via `u_speed * deltaTime`. The "single-tap" description refers to the metering source: it samples the top mip directly without rejecting outliers. Improvements:
1. Histogram-based metering: replace the simple log-luminance reduction with a 256-bin compute or fragment pass that builds a histogram and reads the 50–95th percentile to reject specular fireflies (Lottes/UE4 approach).
2. Lift/drag bands: separate adaptation speeds for brightening vs darkening (eye adapts faster to bright). Add `ExposureConfig::adaptSpeedUp/Down`.
3. Compensate for delta-time spikes by clamping `deltaTime` inside the shader to ~0.1s.

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_view.h` (`ExposureConfig` fields)
- `/data2/vkm/vkm_code/vkmEngine/shaders/post/lum/fragment.shader` and `shaders/post/exposure/fragment.shader`
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/pass/gl_exposure_pass.cpp`
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/resource/gl_auto_exposure.h` (histogram target if going that route — additional R32UI 1×256 texture + atomic-image writes; needs GL 4.3)

**New files.** None.

**Touchpoints.** Composite pass reads `AdaptedLuminance` (unchanged interface).

**Dependencies.** None.

**Risks.** Histogram via image atomics needs GL 4.3 (engine targets 4.2 — check `#version 420 core`). Fallback: keep mip-reduce but use a clipped log-luminance and an outlier reject in the lum shader.

**Hours.** 12–16.

---

### 10. Shader variant flag expansion

**Approach.** Extend `MaterialFeature` (`material_asset.h:30`) and the variant cache (`gl_view.h:194`) to key on more than feature flags. Add a `ShaderVariantKey` struct: `{materialFlags, lightCountBucket, shadowKindMask, csmCascadeCount}`. Light-count buckets (e.g., 0, 1, 4, 8, 16, 32) avoid combinatorial blowup. Shadow-kind mask is a 3-bit (`HasDirectionalShadow`, `HasPointShadow`, `HasSpotShadow`). Bucket from `RenderView` once per frame; collect per-batch when constructing the cache key.

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/core/gl_view.h/.cpp` (`resolveShaderVariant` signature: take `ShaderVariantKey`)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/pass/gl_forward_pass.cpp` (compute key per batch)
- `/data2/vkm/vkm_code/vkmEngine/shaders/pbr/fragment.shader` (gates: `#ifdef LIGHT_COUNT_BUCKET_*`, `#ifdef HAS_DIRECTIONAL_SHADOW`, etc.)

**New files.** None.

**Touchpoints.** `gl_material.h:190` (`getFeatureFlags`); add `ShaderVariantKey` next to it.

**Dependencies.** None. Prereq for cleanly implementing #16 (CSM for spot/point) since cascade-aware variants matter only when shadows exist.

**Risks.** Cache size explosion — limit buckets aggressively; report variant count in Bottom Panel.

**Hours.** 12–16.

---

### 11. Shadow atlas dynamic sizing

**Approach.** `GLShadowAtlas` ctor takes fixed counts (`gl_shadow_map.h:32-37`). Move from static-on-construct to lazy-grow: `ensureCapacity(uint32_t need2D, uint32_t needCube, uint32_t res2D, uint32_t resCube)`. Internally reallocate when current capacity is insufficient (or oversized by 2×, for shrink). Per-light per-resolution would require atlas tiling (Forward+/clustered style) — out of scope for "dynamic sizing"; keep uniform per-bucket res for now.

Settings: `EnvironmentConfig::shadow.{atlasRes2D, atlasResCube, max2DCasters, maxCubeCasters}` with editor-side validation against `Config::MaxShadowCasters2D`. Move that compile-time max into a runtime upper bound; the GLSL side uses `SHADOW_MAX_CASTERS_2D` from `engine_config.glsl` (so changing it requires shader recompile — flag in UI).

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/resource/gl_shadow_map.h/.cpp` (resize API)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/core/gl_view.h` (drop static-size init, lazy on first sync)
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_view.h` (new `ShadowConfig` sub-struct)
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_view.cpp` (slot assignment honours dynamic max)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/pass/gl_shadow_pass.cpp` (re-read atlas resolution per frame)

**New files.** None.

**Touchpoints.** Shader recompile when max-caster constants change (post-shader-include refactor).

**Dependencies.** None. Prereq for #16.

**Risks.** Texture array reallocation invalidates the sampler bind state of the PBR shader — re-bind on rebuild; doc that GL drivers may not free the old texture until the next idle.

**Hours.** 10–14.

---

### 12. Render-pass plugin/factory

**Approach.** Passes are hard-wired in `main.cpp:185-229`. Introduce a `RenderPassFactory` registry: `registerPass(name, [](FactoryCtx&) -> unique_ptr<RenderPass>)`. Pipeline composed from a JSON / TOML config (`pipelines/default.json` lists pass names and per-pass params). Factories own shader handle resolution against the resource manager. Editor's Pipeline tab gains add/remove/reorder.

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/main.cpp` (collapse the wall of `addPass` calls)
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_system.h/.cpp` (load pipeline from file)
- `/data2/vkm/vkm_code/vkmEngine/src/editor/panels/environment_inspector.cpp` (advanced Pipeline tab)

**New files.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_pass_factory.h/.cpp`
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/pass/gl_pass_registration.h/.cpp`
- `/data2/vkm/vkm_code/vkmEngine/pipelines/default.json`

**Touchpoints.** Hot-reload of the pipeline file via `FileWatcher`.

**Dependencies.** None. Makes #20 + #21 (very heavy) much easier to land incrementally.

**Risks.** Pass ordering correctness regressions — keep the read/write declarations enforced by `RenderGraph::compile` validation.

**Hours.** 12–16.

---

### 13. Per-probe IBL blending

**Approach.** Today's `IBLConfig` (`render_view.h:101`) holds one global `path/intensity`. Convert to a list of `IBLProbe { position, influenceRadius, falloffRange, irradianceMap, prefilterMap, brdfLUT(shared), intensity }` stored as a new `ReflectionProbe` ECS component. `GLIBL` becomes `GLIBLProbeSet`: array of irradiance + prefilter cubemap arrays (`GL_TEXTURE_CUBE_MAP_ARRAY`), capped (e.g. 8 active probes). Forward pass uploads probe positions/radii in a new UBO. Shader does per-fragment weighted blend: parallax-corrected probes (Lazarov) with `weight = saturate(1 - dist/radius) / sumWeights`. Skybox + global fallback when sum is zero.

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/ecs/component/` (new `reflection_probe.h`)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/resource/gl_ibl.h/.cpp` (probe array storage)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/pass/gl_ibl_bake_pass.cpp` (bake per probe)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/pass/gl_forward_pass.cpp` (bind probe UBO)
- `/data2/vkm/vkm_code/vkmEngine/shaders/pbr/fragment.shader` (probe loop + parallax correction)
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_view.cpp` (gather probes)

**New files.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/ecs/component/reflection_probe.h`
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/resource/gl_probe_data.h/.cpp`

**Touchpoints.** Scene serializer (probes are entities), editor inspector for the new component.

**Dependencies.** None.

**Risks.** Bake time × probeCount — keep async/staggered (one probe face per frame trickle bake).

**Hours.** 14–18.

---

### 14. POM self-shadowing

**Approach.** After `parallax()` finds the displaced UV, march a second ray from that displaced position toward each contributing light direction in tangent space. Sample heights; accumulate occlusion as max(0, layerDepth - heightAt) over N steps. Single attenuation multiplier on direct lighting. Heitz/Brigade or McGuire's "self-shadowing parallax" reference. Best with a single (sun) light to keep cost bounded; for additional lights either gate with a `HAS_POM_SHADOW` variant flag or share the search with the main parallax march.

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/shaders/pbr/fragment.shader` (new `parallaxShadow(uv, lightDirTS)` helper after `parallax()` at line 207–234; call inside light loop when `HAS_PARALLAX`)

**New files.** None.

**Touchpoints.** `MaterialFeature::Parallax` already gates compile.

**Dependencies.** None.

**Risks.** Cost scales with NdotL × stepCount × lightCount. Limit to first 2 lights or directional only; document.

**Hours.** 6–10.

---

## Heavy (multi-day each)

### 15. Weighted-Blended OIT

**Approach.** McGuire-Bavoil 2013 weighted-blended OIT. Two extra render targets (RGBA16F accum + R8 revealage), attached to the existing HDR FBO. Transparent forward phase:
- Render to (accum, revealage) with weight `w(z, alpha) = clamp(pow(min(1.0, a*10.0)+0.01, 3.0) * 1e8 * pow(1.0 - z*0.9, 3.0), 1e-2, 3e3)`
- accum: `vec4(rgb * a, a) * w`, blend `(GL_ONE, GL_ONE)`
- revealage: `(1-a)`, blend `(GL_ZERO, GL_ONE_MINUS_SRC_COLOR)`
- Composite pass: `finalRGB = lerp(accum.rgb/max(accum.a, 1e-4), backgroundRGB, revealage)`

Drop the per-object back-to-front sort in `render_view.cpp:109-128` (keep distance sort behind a feature flag for refractive/transmissive batches, since OIT can't handle screen-space refraction correctly).

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_view.cpp` (drop or gate sort)
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_graph_resource.h` (`OITAccum`, `OITRevealage`)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/core/gl_frame_resources.h/.cpp` (allocate)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/pass/gl_forward_pass.cpp` (transparent phase uses MRT)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/pass/gl_composite_pass.cpp` (OIT resolve before tonemap)
- `/data2/vkm/vkm_code/vkmEngine/shaders/pbr/fragment.shader` (emit weighted outputs when `OIT_PASS` defined)

**New files.** Possibly a dedicated `shaders/pbr/fragment_oit.shader` or just a variant define.

**Touchpoints.** Transmission (`HAS_TRANSMISSION`) needs an opt-out path (still uses sorted refraction snapshot). MSAA-resolve already handled by the graph.

**Dependencies.** Cleaner with #10 (variant expansion adds an `OIT` flag).

**Risks.** OIT accumulates *all* transparents — doesn't replace sorted refraction for glass behind glass. Keep both code paths, choose per material.

**Hours.** 16–24.

---

### 16. CSM for point/spot lights

**Approach.** Misnomer in the requirements — true cascaded shadows are sun-only by construction. What's actually wanted: better shadow quality for spot/point. Two concrete improvements:
- **Spot**: dual-paraboloid or single-perspective with PCSS soft penumbra. PCSS uses a blocker search to derive a per-pixel kernel.
- **Point**: split each cube face into sub-frusta (one shadow texel-density level per face-zone), or move to a virtual shadow map page table. Simpler: render each face at adaptive resolution based on `light.radius / dist(camera)` and pack into the existing cube atlas.

If "CSM for point" is read literally, that's tetrahedral or omni-cascades (Crytek 2010 — 4 cascades per cube face): expensive and rarely shipped today. Recommend instead PCSS + per-face resolution scaling.

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/pass/gl_shadow_pass.cpp` (per-face resolution)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/resource/gl_shadow_map.h/.cpp` (mipped cube array or per-face viewport)
- `/data2/vkm/vkm_code/vkmEngine/shaders/pbr/fragment.shader` (PCSS sampling at `samplePointShadow` line 362, `sample2DShadow` line 330)

**New files.** None.

**Touchpoints.** PBR shader's existing `kPoissonDisk12` + `ign` rotation already give the noise pattern; PCSS adds the blocker pre-pass.

**Dependencies.** Requires #11 (dynamic shadow atlas) for per-face resolution scaling.

**Risks.** PCSS blocker search adds 16+ taps per fragment; gate behind quality preset.

**Hours.** 24–40.

---

### 17. Render graph resource aliasing

**Approach.** The infrastructure is already half-done: `RGResourceLifetime` (firstWrite, lastRead) is computed in `render_graph.cpp:78-149`. Add a graph-coloring step: sort resources by `firstWrite`, walk and assign each to the earliest physical slot whose previous occupant's `lastRead < this->firstWrite` AND whose physical descriptor (size/format/MSAA) matches. `FrameResources::allocate` consults the plan instead of allocating one-per-RGResource.

Each backend exposes `aliasGroup(RGResource)` → an opaque key the engine compares; in OpenGL the key is `(width, height, internalFormat, samples)`. Implement a `RGPhysicalSlot` table the graph populates at compile.

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_graph.cpp` (aliasing solver)
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_graph.h` (slot table, drop the "Future work" comment at line 61-70)
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/frame_resources.h` + the GL impl `gl_frame_resources.h/.cpp` (slot-keyed allocation)

**New files.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/rg_aliasing.h/.cpp`

**Touchpoints.** Persistent resources (`rgResourceIsPersistent`) opt out of aliasing automatically. SceneHDRResolved is derived from SceneHDR (already aliased in spirit).

**Dependencies.** Pairs naturally with #8 (visualizer surfaces the alias groups).

**Risks.** GL doesn't have explicit transient memory like Vulkan; aliasing means binding the *same* texture id at different lifecycles. Layout transitions implicit. Correctness regressions show as smeared content — extensive validation needed (overlap check at compile).

**Hours.** 20–30.

---

### 18. Full LTC area light specular (Phase 2C)

**Approach.** The shader already has the LTC *diffuse* path (`ltcQuadIrradiance` at line 522). For specular, follow Heitz 2016 "Real-Time Polygonal-Light Shading with Linearly Transformed Cosines": precomputed LUT (`ltc_mat` and `ltc_amp`) indexed by `(NdotV, roughness)`. For each rect/disk light:
- Build LTC `M^-1` from the LUT.
- Multiply rect/disk vertices by `M^-1`.
- `ltcQuadIrradiance(transformedVerts)` gives the specular term.
- Multiply by Fresnel + `ltc_amp.r` (normalization).

Disks: project to a polygon approximation (8-sided) or use the analytic disk LTC variant.

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/shaders/pbr/fragment.shader` (replace `areaRectClosestPoint`/`areaDiskClosestPoint` path with LTC; the Phase 2A representative-point at line 543 stays as a fallback when LTC LUT isn't bound)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/core/gl_view.h/.cpp` (bake/load LTC LUT once at init)
- `/data2/vkm/vkm_code/vkmEngine/src/tools/loader/texture_loaders.cpp` (HDR LUT loader if shipping a baked table)

**New files.**
- `/data2/vkm/vkm_code/vkmEngine/assets/luts/ltc_mat.hdr`
- `/data2/vkm/vkm_code/vkmEngine/assets/luts/ltc_amp.hdr`
- `/data2/vkm/vkm_code/vkmEngine/src/tools/generator/ltc_lut_generator.cpp` (offline bake or runtime — Heitz publishes raw values)

**Touchpoints.** Light UBO already carries axisU/axisV (line 118-119). Add LUT samplers to the PBR sampler binding table in `main.cpp`.

**Dependencies.** None, but cleanest with #10 (an `LTC_AREA_LIGHTS` variant).

**Risks.** Two-sided emission requires evaluating both orientations; clamp negative results.

**Hours.** 16–24.

---

### 19. Better subsurface (thickness-aware)

**Approach.** Move from the wrap shader (line 669-674) toward Burley/Christensen normalized-diffusion or pre-integrated SSS. Two viable paths:
1. **Pre-integrated skin (Penner)**: precompute a 2D LUT `(NdotL, 1/curvature)` of subsurface response, sample per-fragment using `1/curvature ≈ length(fwidth(N))`. Requires a thickness map per material.
2. **Screen-space separable SSS (Jimenez)**: post-process blur of irradiance in screen space, gated by per-pixel mask + depth. Needs a separable kernel and an irradiance MRT in the forward pass.

Recommend the LUT approach: lower invasiveness, no new render target, matches existing IBL-LUT pipeline.

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/shaders/pbr/fragment.shader` (replace wrap diffuse with LUT sample)
- `/data2/vkm/vkm_code/vkmEngine/src/engine/resource/material_asset.h` (`thicknessTexture` handle)
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/resource/gl_material.h/.cpp` (texture flag bit + slot)
- `/data2/vkm/vkm_code/vkmEngine/main.cpp` (load the SSS LUT once)

**New files.**
- `/data2/vkm/vkm_code/vkmEngine/assets/luts/sss_preintegrated.hdr`
- `/data2/vkm/vkm_code/vkmEngine/src/tools/generator/sss_lut_generator.cpp`

**Touchpoints.** `MaterialUBOData` (already has `subsurfaceColor`, `thicknessFactor`); add `subsurfaceRadius` (Burley scale).

**Dependencies.** None.

**Risks.** `fwidth(N)` produces curvature only for smooth surfaces — flat-normal-mapped materials show banding. Mitigate by sampling curvature from a baked texture.

**Hours.** 20–28.

---

## Very heavy (week+ each)

### 20. Hi-Z occlusion culling

**Approach.** Currently a stub (`occlusion_culler.h:29` TODO). Implementation:
1. Reuse depth from the existing `GLPrepass` (`pass/gl_prepass.cpp`). Build a Hi-Z pyramid (max-depth mip chain) right after prepass — new `GLHiZPass` that runs `glGenerateMipmap`-style max-reduce.
2. Visibility test moves to a compute pass: each candidate AABB is projected to screen, the smallest enclosing mip level is sampled, compared against AABB's minimum depth. Output a visibility bitmask.
3. CPU side (`occlusion_culler.h`): for now, GPU-only culling means draw indirect with the bitmask compacted on GPU; an alternative is to read back the previous frame's Hi-Z for use as the current frame's CPU oracle (1-frame latency).

Reference: Ulrich 2002 / Bittner 2004 + Wihlidal "GPU-Driven Rendering" (2015). One-frame-latent CPU readback is the safest first step.

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/visibility/culling/occlusion_culler.h` (real impl)
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/visibility/visibility_system.cpp` (read back last frame's Hi-Z)
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_graph_resource.h` (`HiZPyramid` enum)

**New files.**
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/pass/gl_hiz_pass.h/.cpp`
- `/data2/vkm/vkm_code/vkmEngine/shaders/post/hiz_reduce/{vertex,fragment}.shader`
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/visibility/culling/hiz_oracle.h/.cpp`

**Touchpoints.** ParallelFor culling loop in `visibility_system.cpp:153-189` calls `OcclusionCuller::isVisible`; signature changes to consume an oracle. Editor stats panel for occluded-count.

**Dependencies.** Cleanest with #17 (Hi-Z pyramid is a transient with own lifetime). Benefits from #8 to visualize coverage.

**Risks.** 1-frame readback latency causes pop-in for fast camera moves. Mitigate with conservative grow on the AABB before the oracle query. GPU-driven indirect path is much more invasive.

**Hours.** 40–60.

---

### 21. Real-time GI / light probes

**Approach.** Irradiance Volume (Greger et al. 1998, modernized as DDGI by McGuire/Majercik 2019). Keep scope to "irradiance probes" — full DDGI requires ray tracing or distance volumes (per-probe trace), heavy on GL.

Pragmatic path:
1. Sparse grid of irradiance probes in a 3D texture (`GL_TEXTURE_3D` of 4 sphere-harmonics RGB values, or 9 SH bands × RGB = 27 floats — packed in a 3D atlas).
2. Probes baked offline as a first iteration (uses #13's per-probe IBL infrastructure). Live update: per-frame relight one probe by reading its irradiance map at runtime against the current sun, picked round-robin (60 probes / second @ 60fps).
3. Forward pass samples 8 neighboring probes per fragment trilinearly, weights by visibility (chebyshev test if depth probes available; otherwise simple distance).

For *true* runtime GI on GL 4.2, options are limited: light propagation volumes (Crytek 2011) or voxel cone tracing (Crassin 2011). Both are large undertakings. Recommend probes-as-grid first; LPV/VCT in a separate ticket.

**Files to change.**
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/resource/gl_ibl.h/.cpp` (probe grid texture)
- `/data2/vkm/vkm_code/vkmEngine/shaders/pbr/fragment.shader` (probe sample in ambient term, replace IBL-only path)
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_view.h` (`GIConfig`)

**New files.**
- `/data2/vkm/vkm_code/vkmEngine/src/engine/ecs/component/probe_grid.h`
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/pass/gl_probe_update_pass.h/.cpp`
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/resource/gl_probe_grid.h/.cpp`
- `/data2/vkm/vkm_code/vkmEngine/shaders/gi/probe_update/{vertex,fragment}.shader`

**Touchpoints.** Editor probe-grid placement tool (sub-feature of inspector). Serializer (probe-grid component). #13 (per-probe IBL) provides the bake path.

**Dependencies.** **#13 first** (per-probe IBL infrastructure is the source of probe data). #12 (pipeline plugin) makes adding the probe-update pass safer.

**Risks.** SH ringing on high-frequency lighting; light leaks through walls without visibility data. Storage: a 16×16×16 probe grid at 27 floats/probe = ~450 KB — fine. Update cost dominates; budget carefully.

**Hours.** 60–100.

---

## Build order

Designed so quick visual wins land first, each medium task unlocks (or is unlocked by) at most one heavy task, and prerequisite dependencies are satisfied. Numbers in parens are the feature IDs.

**Phase 1 — Quick wins (1 week, parallelizable):**
1. (4) Screenshot API outside editor — trivial code move, 2h
2. (1) Bloom soft-knee — visible win, 3h
3. (6) Runtime graphics settings UI — leverages existing `setPassEnabled`, 6h
4. (2) Shader error history — debugging quality-of-life, 4h
5. (5) Hot-reload include graph — devloop quality-of-life, 6h
6. (3) Expanded debug visualizations — payoff for #2/#5, 8h

**Phase 2 — Medium foundations (2–3 weeks):**

7. (7) Per-pass GPU timing UI — 14h
8. (8) Render-graph visualizer — share panel with #7, 12h
9. (12) Render-pass plugin/factory — makes everything heavier easier, 16h
10. (11) Shadow atlas dynamic sizing — 14h *(prereq for #16)*
11. (10) Shader variant flag expansion — 16h *(unblocks cleaner #15, #16, #18)*

**Phase 3 — Visible quality wins (3 weeks):**

12. (9) Auto-exposure improvements — 16h
13. (14) POM self-shadowing — 10h
14. (13) Per-probe IBL blending — 18h *(prereq for #21)*

**Phase 4 — Heavy features (4–6 weeks):**

15. (18) LTC area-light specular — 24h, completes the Phase 2A→2C arc already commented in the shader
16. **Pair: (11+16)** Re-open shadow atlas for spot/point quality (PCSS + adaptive res) — 40h. Done after #11 lands in Phase 2.
17. (15) Weighted-Blended OIT — 24h. Easier after #10.
18. (17) Render-graph aliasing — 30h. Pairs visually with #8.
19. (19) Better subsurface — 28h.

**Phase 5 — Very heavy (multi-month):**

20. (20) Hi-Z occlusion culling — 60h. Easier after #17, since Hi-Z pyramid is a new transient.
21. (21) Real-time GI / probe grid — 100h. Strictly after #13.

### Suggested pairings

- **#11 + #16** — shadow atlas dynamic sizing is a hard prereq for per-face point-shadow resolution; ship in a single feature branch.
- **#7 + #8** — same Bottom Panel real estate, similar data sources; one PR.
- **#13 + #21** — per-probe IBL produces the bake data that the probe grid samples; #13 ships first, #21 starts immediately after.
- **#10 + #15 + #18** — variant cache expansion gives clean OIT and LTC variants; do #10 first then either of the other two can land independently.
- **#17 + #20** — Hi-Z pyramid lives best inside the aliased transient pool; do #17 first.

### Risks at the build-order level

- **Shader variant explosion** if #10 ships before #11 (light-count buckets compound with shadow-kind variants).
- **Pipeline rewire churn** if #12 doesn't ship before #15/#20/#21 — the manual `addPass` sequence in `main.cpp:185-229` becomes a refactor blocker. Aim #12 early.
- **GL version**: histogram metering (#9) and Hi-Z compute (#20) want GL 4.3. Current PBR uses `#version 420 core`; bumping the engine target is a single global decision — make it once during Phase 2.

---

### Critical files for implementation

These five surface up across the most features and are the highest-leverage to keep clean:

- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_view.h`
- `/data2/vkm/vkm_code/vkmEngine/src/engine/system/render/render_graph.cpp`
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/core/gl_view.h`
- `/data2/vkm/vkm_code/vkmEngine/src/backend/opengl/pass/gl_forward_pass.cpp`
- `/data2/vkm/vkm_code/vkmEngine/shaders/pbr/fragment.shader`
