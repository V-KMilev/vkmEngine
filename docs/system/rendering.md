# Rendering System

The rendering system is split into a backend-agnostic engine-level
render graph and a concrete OpenGL backend. The engine side builds a
per-frame RenderView snapshot, the backend syncs GPU resources to it,
and the render graph drives an ordered list of passes that produce
the final image.

## Key Files

- `src/engine/system/render/render_system.h` — RenderSystem (System subclass)
- `src/engine/system/render/render_view.h` — RenderView + DrawableData + LightData
- `src/engine/system/render/environment_config.h` — EnvironmentConfig + sub-configs + RenderMode
- `src/engine/system/render/render_graph.h` — RenderGraph (ordered pass list with declared resource flow)
- `src/engine/system/render/render_graph_resource.h` — RGResource enum (logical transient resources)
- `src/engine/system/render/render_graph_builder.h` — RenderGraphBuilder (per-pass declare API)
- `src/engine/system/render/render_graph_context.h` — RenderGraphContext (per-frame execution context)
- `src/engine/system/render/render_pass.h` — RenderPass (abstract pass)
- `src/engine/system/render/render_backend.h` — RenderBackend (abstract backend)
- `src/engine/system/render/environment.{h,cpp}` — Environment as a scene entity
- `src/backend/opengl/` — OpenGL implementation
- `src/engine/core/engine_config.h` — cross-cutting render budgets (MAX_LIGHTS, NUM_CASCADES, ...)

## Pipeline Overview

```
RenderSystem::update(FrameContext)
  |-- Pull EnvironmentConfig from the scene's Environment entity
  |-- RenderView::build(scene, resources, visibility)
  |     |-- Copy camera data from Visibility
  |     |-- Gather DrawableData from visible entities
  |     |-- Sort drawables (sort priority: Opaque < AlphaMask < Unlit < Transparent)
  |     |-- Sub-sort the Transparent run back-to-front by distance from camera
  |     |-- Gather shadowCasters from the WHOLE scene (not camera-culled)
  |     |-- Gather LightData; assign 2D atlas layers / cube slots
  |-- backend.syncResources(view, resources)
  |     |-- GLView::sync uploads/updates GLMesh / GLMaterial / GLTexture / GLLights
  |     |-- Builds the camera + shadow instance batches
  |-- RenderGraph::execute(backend, view, resources)
        |-- compile() (first run + after addPass) validates ordering, computes lifetimes
        |-- For each enabled pass: optionally resolve SceneHDR, then pass->execute(ctx)
```

## RenderSystem

Inherits `System`. Owns the `RenderBackend`, the `RenderGraph`, and a
persistent `RenderView` whose vectors keep capacity across frames.

```cpp
auto& renderSystem = engine.addSystem<RenderSystem>(SystemStage::Render);
renderSystem.setBackend(std::make_unique<GLBackend>());
renderSystem.addPass(std::make_unique<GLForwardPass>(pbrShader));
// ... more passes
```

Also exposes `renderMaterialPreview()` / `materialPreviewTexture()` for
the editor's Material Editor + Asset Browser. The full render graph runs
into an offscreen FBO so previews get IBL/SSR/GTAO/bloom/tone mapping
without any code duplication.

## RenderView

A per-frame snapshot containing everything passes need to render:

| Field | Type | Description |
|-------|------|-------------|
| `camera` | `CameraData` | view, projection, viewProjection + position + exposure |
| `environment` | `EnvironmentConfig` | All post / IBL / grid / debug toggles. Edited via the Inspector |
| `drawables` | `vector<DrawableData>` | Sorted by (sort priority, material, mesh) for batching |
| `shadowCasters` | `vector<DrawableData>` | Full scene (NOT camera-culled). Shadow pass renders this set |
| `lights` | `vector<LightData>` | Enabled lights with world-space transforms + shadow slot |
| `viewportWidth/Height` | `uint32_t` | Current viewport dimensions |
| `deltaTime` | `float` | Real seconds since last frame (eye adaptation) |

Drawables are sorted with a two-phase key-index sort to minimize swaps
of the ~88-byte `DrawableData` structs. The sort priority order matters:
all depth-writing material classes (Opaque, AlphaMask, Unlit) draw
before Transparent so the per-batch HDR snapshot in the transparent
forward phase contains them.

## EnvironmentConfig

Lives on a singleton Environment scene entity (created by
`Engine::sceneEnvironment(scene)`). Edited via the Render Settings window
(Window > Render Settings), which drives `EnvironmentInspector`; the
Environment is not listed in the hierarchy or the entity Inspector. Persists
through scene save/load via `ComponentSerializer`.

Covers: ambient, IBL (path + intensity), SSAO, SSR, TAA, DoF, motion
blur, color grading, tone-map curve, bloom, auto-exposure, clear
color, grid, AABB debug, wireframe.

## RenderGraph

Replaces the old `RenderPipeline`. Same ordered execution + per-pass
enable/disable, but each pass declares its transient resource reads
and writes via `RenderPass::declareResources(RenderGraphBuilder&)`.

`compile()` is auto-invoked by `execute()` when the pass set changed:
- Validates that no pass reads a resource before some earlier pass
  writes it (warns instead of erroring; implicit resources like
  `Backbuffer` and `SceneHDRResolved` are exempt).
- Computes per-resource lifetime (first write → last read) for future
  pool aliasing.

The graph also owns one auto-resolve: when a pass writes
`RGResource::SceneHDR` and a later pass reads `SceneHDRResolved`, the
graph calls `backend.resolveSceneColor()` between them. Passes no
longer self-resolve.

```cpp
enum class RGResource : uint8_t {
    ShadowAtlas, IBL, GBufferNormal, GBufferPosition, AO,
    SceneHDR, SceneHDRResolved, BloomChain, AdaptedLuminance,
    TAAHistory, PostScratch, Overlay, Backbuffer,
    Count
};
```

`AdaptedLuminance`, `TAAHistory`, and `Backbuffer` are *persistent* (survive
across frames - persistent ping-pong or externally owned); reads of them
before any in-frame write are not ordering errors because the previous
frame is the producer. See `rgResourceIsPersistent()` /
`rgResourceIsImplicit()` in `render_graph_resource.h`.

The graph names resources but doesn't yet own their GPU storage; the
backend's `FrameResources` still allocates them. Lifetime-based pool aliasing
(disjoint `[firstWrite..lastRead]` ranges sharing physical storage) is
**intentionally deferred** at the current scale - the lifetime arrays in
`compile()` are computed for telemetry and to keep the door open, but at
~15 transient resources the memory win does not justify the design +
correctness work. See [`docs/misc/gaps.md`](../misc/gaps.md) for the
deferral rationale.

## RenderPass

Abstract base for individual rendering stages. Each pass has a name,
an enabled flag, and overrides:

- `onResize(backend, w, h)` — react to viewport changes
- `execute(RenderGraphContext& ctx)` — perform GPU work
- `declareResources(RenderGraphBuilder& b)` — optional; what it reads/writes

## RenderBackend

Abstract interface. Backends implement `resize()`, `syncResources()`,
`resolveSceneColor()`, `setWireframe()`, plus offscreen-preview hooks
used by the editor.

```cpp
enum class RenderBackendType { NONE, OpenGL, Optix, CPU };
```

Today only OpenGL is implemented.

## OpenGL Backend

### GLBackend

Concrete `RenderBackend`. Owns:
- `Core::Context` (GLEW state + draw helpers)
- `GLView` (GPU resource synchronizer)
- `GLDefaultRenderTarget` (the window's backbuffer)
- `FrameResources` (transient: HDR target, bloom, gbuffer, TAA, exposure, post-scratch)
- Per-key thumbnail snapshot cache for the editor

Offscreen preview path: `beginPreview(size)` swaps in a private
`FrameResources` set + a small framebuffer target, the whole render
graph runs into it, `snapshotPreviewToCache(key)` copies the result
into a stable `Core::Texture2D` keyed for the editor.

### GLView

GPU resource synchronizer. Holds parallel tables keyed by handle id:

- `m_meshTable` / `m_materialTable` / `m_textureTable` / `m_shaderTable`
- per-frame UBOs: `GLCamera`, `GLLights`, `GLShadowData`
- shadow atlas + IBL set
- two `GLInstanceBatcher`s: camera-visible + full-scene shadow casters
- `m_shaderVariants` — per-material shader variant cache (see below)

`sync(view, resources)` reconciles every table against the current
RenderView. Each table only does work when its per-type version
changed or the drawable count changed.

### Shader variant cache

The PBR shader is variant-aware: optional lobes (transmission, volume,
clearcoat, anisotropy, subsurface, sheen, parallax, alpha test) sit
inside `#ifdef HAS_X` blocks. `GLView::resolveShaderVariant(handle,
flags, resources)` compiles one `GLShader` per `(shaderId, flags)`
combination, with `#define HAS_X` injected for the bits set in
`flags`. The forward pass derives `flags` from each batch's
`GLMaterial::getFeatureFlags()` (cached at material sync time).

A `ShaderAsset` opts in by setting `variantAware = true` (today: only
the PBR shader). Other shaders go through `resolveShader` and share a
single compiled program across every material that uses them.

Hot reload of a base `.shader` file bumps the asset version; the next
`resolveShaderVariant` call drops every variant of that shader from
the cache and rebuilds them lazily.

### Render Passes

Sequence as wired in `main.cpp`:

| Pass | Shader | Description |
|------|--------|-------------|
| `GLIBLBakePass` | `ibl/equirect`, `ibl/irradiance`, `ibl/prefilter`, `ibl/brdf` | Bakes the environment map into irradiance / prefilter / BRDF LUT. No-op unless the env-map path changed |
| `GLShadowPass` | `shadow` | Renders directional CSM + spot + point cube shadows into the shadow atlas. Skips when the signature matches the previous frame |
| `GLPrepass` | `prepass` | View-space normal + position MRT for GTAO and SSR |
| `GLGTAOPass` | `post/gtao` | Half-res ground-truth AO into the gbuffer's occlusion slot |
| `GLForwardPass` (Opaque) | `pbr` variants + `unlit` | Per-material PBR variants for opaque + AlphaMask + Unlit batches |
| `GLSkyboxPass` | `skybox` | Skybox into the HDR target between opaque and transparent |
| `GLForwardPass` (Transparent) | `pbr` variants | Snapshots the opaque+sky HDR, then draws transparent batches with refraction sampling. Per-batch resnapshot for layered glass |
| `GLAABBDebugPass` | `aabb_debug` | Wireframe AABB visualization (gated by `env.aabbDebug`) |
| `GLGridPass` | `grid` | Procedural infinite grid with distance fade |
| `GLSSRPass` | `post/ssr` | Screen-space reflections, additively blended into the HDR scene |
| `GLLensFlarePass` | `post/lens_flare` | Ghosts + halo from bright pixels, additively blended (off by default). Runs after SSR so reflections can themselves cause flare, and before TAA/DoF for temporal stabilisation |
| `GLTAAPass` | `post/taa` | Temporal AA (off by default; MSAA already handles spatial) |
| `GLDoFPass` | `post/dof` | Depth-of-field over the resolved HDR (off by default) |
| `GLMotionBlurPass` | `post/motion_blur` | Camera motion blur (off by default) |
| `GLBloomPass` | `post/bloom_down`, `post/bloom_up` | COD/Jimenez bloom mip chain |
| `GLExposurePass` | `post/lum`, `post/exposure` | Auto-exposure metering + adaptation |
| `GLCompositePass` | `post/composite` | Final HDR -> bloom -> exposure -> AgX/PBR-Neutral/ACES tone map -> sRGB into the backbuffer |

### GPU Resources

| Class | Description |
|-------|-------------|
| `GLMesh` | VAO + VBO + IBO. Vertex layout: pos(3f), normal(3f), uv(2f), tangent(4f) |
| `GLMaterial` | UBO (176 bytes, std140) + texture bindings + cached `featureFlags` |
| `GLTexture` | Wraps `Core::Texture2D` |
| `GLLights` | UBO with up to `Config::MaxLights = 32` lights |
| `GLShadowAtlas` | 2D array (`MaxShadowCasters2D = 6` layers, 4 reserved for CSM) + cube array (`MaxShadowCastersCube = 2`) |
| `GLShadowData` | Per-caster lightSpace matrix + bias UBO |
| `GLIBL` | Equirect HDR + env cube + irradiance + prefilter + BRDF LUT targets |
| `Core::InstanceBuffer` | Per-instance mat4 buffer (vkmGL; orphan-grow, divisor=1) |
| `GLInstanceBatcher` | Groups sorted drawables into instance batches. Owns cross-batcher VAO arbitration |
| `FrameResources` | `GLHdrTarget` + `GLBloom` + `GLGBuffer` + `GLTAA` + `GLAutoExposure` + `GLPostScratch` |

### Shader Interface

**Vertex attributes:**
- 0–3: Per-vertex (position, normal, uv, tangent)
- 4–7: Per-instance (mat4 model matrix columns, divisor=1)

**UBO bindings** (see `GLConfig::UBOBindingPoints`):
- 0: Material (176 bytes)
- 1: Lights
- 2: Camera (view/proj + camera position + exposure + ambient)
- 3: Shadow (per-caster matrices + biases)

**Texture slots** (see `GLConfig::TextureSlots`):

| Slot | Texture | Notes |
|------|---------|-------|
| 0 | Albedo |  |
| 1 | Normal |  |
| 2 | MetallicRoughness | Shared with AOMetallicRoughness variant |
| 3 | AO |  |
| 4 | Emission |  |
| 5 | Height |  |
| 6 | Clearcoat |  |
| 7 | Transmission |  |
| 8 | Metallic | When not using combined |
| 9 | Roughness | When not using combined |
| 10 | ShadowMap2D | `sampler2DArrayShadow` |
| 11 | ShadowMapCube | `samplerCubeArrayShadow` |
| 12 | IrradianceMap | IBL diffuse cube |
| 13 | PrefilterMap | IBL specular cube (roughness mips) |
| 14 | BrdfLUT | Split-sum BRDF/DFG lookup |
| 15 | SSAO | GTAO factor |
| 16 | EnvCube | Sharp env cube for low-roughness mirror reflections |
| 17 | SceneColor | Resolved opaque scene for transmissive refraction |
