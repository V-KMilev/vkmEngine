# Rendering System

The renderer has two halves with one seam between them. The engine half builds a
backend-agnostic `RenderView` snapshot each frame. The backend half syncs its GPU
resources to that snapshot and runs a **fixed, ordered list of passes** to produce
the image. There is **no engine-level render graph** and **no pass abstraction**
above the backend - passes are an OpenGL implementation detail.

> If you have read older docs or comments: there is no `RenderGraph`,
> `RenderGraphBuilder`, `RGResource`, `RenderPass`, `EnvironmentConfig`, or shader
> variant cache. None of those types exist. The renderer also has no TAA, DoF,
> lens flare, or auto-exposure. What it does have is listed below.

## Key files

- `src/engine/system/render/render_system.h` - RenderSystem (System subclass; owns the backend)
- `src/engine/system/render/render_view.h` - RenderView (the engine -> backend contract)
- `src/engine/system/render/data/` - the POD frame structs: CameraData, DrawableData, LightData, ShadowCasterData, ProbeData
- `src/engine/system/render/render_settings.h` - RenderSettings + RenderMode (editable tuning)
- `src/engine/system/render/render_backend.h` - RenderBackend (the abstract seam)
- `src/engine/ecs/environment.h` - Environment (HDR/skybox), a scene-level struct
- `src/backend/opengl/` - the OpenGL backend

## Per-frame flow

```
RenderSystem::update(FrameContext)
  |-- RenderView::build(scene, visibility)        // engine side, backend-agnostic
  |     |-- buildCamera     from the Visibility snapshot
  |     |-- buildDrawables  from the visible set (sorted for batching;
  |     |                   transparent run sub-sorted back-to-front)
  |     |-- buildShadowCasters  from the whole scene (NOT camera-culled)
  |     |-- buildLights / buildProbes
  |     |-- copy RenderSettings and Environment into the view
  |-- backend.render(view, resources)             // GLBackend
        |-- GLView::sync      upload/refresh changed GPU resources
        |-- bake IBL          only when environment.hdrPath changed
        |-- shadow plan       assign atlas slots, upload shadow UBO
        |-- per-frame UBOs    camera, lights
        |-- partitionDrawables  split into opaque vs transparent buckets
        |-- run the 10 passes in order
        |-- probe update      re-bake new/moved/changed reflection probes
```

`GLBackend::render` is the authority for this order
(`src/backend/opengl/gl_backend.cpp`).

## RenderView - the contract

This struct is the entire engine-to-backend interface; every backend consumes
exactly it, which is what makes backends interchangeable. `build()` refills it from
the `VisibilitySystem` output, reusing the vectors' capacity across frames.

| Field | Type | Notes |
|-------|------|-------|
| `viewportX/Y/Width/Height` | `uint32_t` | Scene render rect |
| `surfaceHeight` | `uint32_t` | Full backbuffer height (lets a bottom-left backend flip the rect) |
| `camera` | `CameraData` | view / projection / viewProjection + position |
| `drawables` | `vector<DrawableData>` | Visible set, pre-sorted for batching |
| `shadowCasters` | `vector<ShadowCasterData>` | Whole scene, not camera-culled |
| `lights` | `vector<LightData>` | Enabled lights with world transforms + shadow slot |
| `probes` | `vector<ProbeData>` | Reflection probes in the scene |
| `settings` | `RenderSettings` | Pass toggles + per-effect params, copied each frame |
| `environment` | `Environment` | HDR path / intensity / skybox toggle |

Drawables are sorted so that all depth-writing classes (Opaque, AlphaMask, Unlit)
precede Transparent, and the transparent run is ordered back-to-front - the
transparent forward phase snapshots the opaque scene for refraction, so opaques
must already be drawn.

## RenderSettings and RenderMode

`RenderSettings` (in `render_settings.h`) is plain data owned by the RenderSystem,
mutated by the editor's Render Settings panel, and copied into the view each frame.

- **Toggles:** `gtao`, `ssr`, `motionBlur`, `bloom`, `probes`, `fxaa`, `grid`.
- **Per-effect params:** GTAO (radius/intensity/power/bias), SSR (intensity/maxDistance),
  motion blur (intensity/maxVelocity/samples), bloom (strength/threshold/knee/radius).
- **Shadows:** `shadowResolution` (1024/2048/4096 per atlas tile).
- **`renderMode`:** composite output selector - `Default` (final image) or a debug
  buffer: `Depth`, `Normals`, `Roughness`, `Metalness`, `AmbientOcclusion`, `Bloom`,
  `ShadowAtlas`. The integer values must match the `MODE_*` constants in the
  composite shader.

## RenderBackend - the seam

Abstract interface (`render_backend.h`). The engine only ever sees this; it never
includes a `gl_*` header. Core methods: `init`, `resize`, `render(view, resources)`,
plus offscreen `renderPreview` hooks the editor uses for material/asset thumbnails.
One implementation exists today (OpenGL); Optix/CPU are the reason the seam is
backend-agnostic.

## OpenGL backend

`GLBackend` owns:

- `Core::Context` - GLEW state + draw helpers (from vkmGL)
- `GLView` - the GPU resource synchronizer
- Render targets: `m_sceneHDR`, `m_sceneColor` (opaque snapshot for refraction),
  `m_ao` (GTAO), `m_bloom`
- `m_shadowAtlas` + `m_shadowData`, `m_ibl`, the reflection-probe manager `m_probes`
- `m_preview` - a separate minimal forward+composite path for editor thumbnails
  (it does **not** run the full pass list)

### The passes (fixed order)

From `gl_backend.cpp` - a hardcoded `m_passes` list, run top to bottom:

| # | Pass | Does |
|---|------|------|
| 1 | Shadow | Renders directional CSM + spot + point-cube shadow maps into the atlas (skips when the shadow signature is unchanged) |
| 2 | DepthPrepass | Early-Z for opaque geometry; also clears the HDR target and primes the G-buffer (view-space normal/position) for GTAO and SSR |
| 3 | GTAO | Half-res ground-truth ambient occlusion into the AO target |
| 4 | Skybox | Fills the background before geometry so transparents blend over it |
| 5 | Forward | The PBR ubershader: opaque/AlphaMask/Unlit (depth-primed), then transparent buckets that sample the opaque snapshot for refraction |
| 6 | SSR | Screen-space reflections, additively blended into the HDR scene |
| 7 | MotionBlur | Camera motion blur over the resolved scene |
| 8 | Bloom | Bright-pass + mip-chain down/upsample |
| 9 | Grid | World-space ground grid overlay (debug) |
| 10 | Composite | Tonemap + optional FXAA to the backbuffer (or a debug buffer per `renderMode`) |

IBL is **not** a pass: `bakeEnvironment()` runs a transient `GLIBLBaker` inside
`render()` only when `environment.hdrPath` changes, producing the irradiance,
prefilter, and BRDF/DFG products the forward pass samples. Reflection probes are
baked at frame end and bound per frame into a probe UBO.

### GLView - GPU sync

Holds per-type tables keyed by handle id (mesh / material / texture / shader) plus
the per-frame UBOs and the shadow/IBL sets. `sync(view, resources)` reconciles each
table against the RenderView and only does work when that resource type's `version`
bumped or the drawable count changed (see [resources.md](../resources.md) for the
version mechanism). All materials share one compiled PBR program; features are
runtime uniform toggles, not compiled `#ifdef` variants.

### Shader binding contract

The forward shader's bindings are defined in
`src/backend/opengl/convention/gl_bindings.h` - the single source of truth, mirrored
by the GLSL. Vertex attributes are per-vertex position/normal/uv/tangent (slots 0-3)
plus a per-instance model matrix (slots 4-7, divisor 1). UBO binding points cover
Material, Lights, Camera, and Shadow blocks; texture slots cover the PBR material
maps plus the shadow atlas (2D array + cube array), IBL set (irradiance / prefilter /
BRDF LUT / env cube), the GTAO factor, and the resolved scene-color sampler for
refraction. Treat `gl_bindings.h` as authoritative rather than hardcoding slot
numbers from memory.
