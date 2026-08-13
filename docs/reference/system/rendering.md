# Rendering System

The renderer has two halves with one seam between them. The engine half builds a
backend-agnostic `RenderView` snapshot each frame. The backend half syncs its GPU
resources to that snapshot and runs a **fixed, ordered list of passes** to produce
the image. There is **no engine-level render graph**, and **no pass abstraction is
exposed to the engine** - passes (`GLPass`) live entirely inside the backend as
an OpenGL implementation detail.

> If you have read older docs or comments: there is no `RenderGraph`,
> `RenderGraphBuilder`, `RGResource`, `RenderPass`, `EnvironmentConfig`, or shader
> variant cache. None of those types exist. The renderer also has no TAA, SSR,
> FXAA, motion blur, lens flare, or auto-exposure. What it does have is listed
> below.

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
  |     |-- buildLights / buildProbes
  |     |-- buildDrawables  from the visible set (UNSORTED - visibility order;
  |     |                   the backend does all sorting/partitioning)
  |     |-- buildShadowCasters  from the whole scene (NOT camera-culled)
  |     |-- copy RenderSettings and Environment into the view
  |-- backend.render(view, resources)             // GLBackend
        |-- GLView::sync      upload/refresh changed GPU resources
        |-- bake IBL          when the HDR path changed, or the procedural
        |                     sky's sun/params moved (persistent GLIBLBaker)
        |-- shadow plan       assign atlas slots, cull casters per tile, upload shadow UBO
        |-- opaque batch      group the opaque bucket into instanced runs (once, shared)
        |-- per-frame UBOs    camera, lights
        |-- partitionDrawables  split into opaque / alpha-mask / transparent
        |-- run the 18 passes in order
        |-- probe update      re-bake new/moved/changed reflection probes
        |-- irradiance update re-bake the SH volume when its box/grid changed
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
| `drawables` | `vector<DrawableData>` | Visible set, in visibility order (UNSORTED); the backend sorts/partitions |
| `shadowCasters` | `vector<ShadowCasterData>` | Whole scene, not camera-culled |
| `lights` | `vector<LightData>` | Enabled lights with world transforms + shadow slot |
| `probes` | `vector<ProbeData>` | Reflection probes in the scene |
| `settings` | `RenderSettings` | Pass toggles + per-effect params, copied each frame |
| `environment` | `Environment` | HDR path / intensity / skybox toggle |

The frontend does **not** sort drawables - it emits them in visibility order.
All sorting and partitioning happens in the backend: `partitionDrawables` splits
opaque from transparent, `GLInstanceBatcher` groups by (material, mesh) for
instancing, and `GLForwardPass` drives the depth-writing classes (Opaque,
AlphaMask, Unlit) before the back-to-front transparent run. The transparent
forward phase snapshots the opaque scene for refraction, so opaques must already
be drawn.

## RenderSettings and RenderMode

`RenderSettings` (in `render_settings.h`) is plain data owned by the RenderSystem,
mutated by the editor's Render Settings panel, and copied into the view each frame.

- **Toggles:** `gtao`, `bloom`, `probes`, `contactShadows`, `grid`.
- **Per-effect params:** GTAO (radius/intensity/power/bias), bloom
  (strength/threshold/knee/radius), contact shadows (length/thickness).
- **Quality:** `msaaSamples` (1/2/4/8), `shadowResolution` (1024/2048/4096 per
  atlas tile).
- **`renderMode`:** composite output selector - `Default` (final image) or a debug
  view: `Depth`, `Normals`, `Roughness`, `Metalness`, `AmbientOcclusion`, `Bloom`,
  `ShadowAtlas`, `ContactShadows`, `Fog`, `GiOnly`, `DirectOnly`, `Clusters`
  (Forward+ light-count heatmap). The `MODE_*` constants the composite shader
  switches on are generated from this enum at configure time (`render_modes.glsl`).

Scene-look settings (sky, fog, IBL intensity, physics) live in `Environment`
and serialize with the scene; `RenderSettings` is machine-quality tuning and
does not.

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
- Render targets: `m_sceneHDR` (the geometry target: colour + depth + G-buffer),
  `m_sceneMS` (multisample twin when MSAA is on), `m_postA`/`m_postB`
  (colour-only post ping-pong scratches), `m_ao` (GTAO), `m_contactShadow`,
  `m_bloom`
- `m_shadowAtlas` + `m_shadowData`, `m_ibl` + `m_iblBaker`, `m_clusterGrid`,
  `m_fog` (froxel volumes, lazily allocated), `m_irradiance` + its baker, the
  reflection-probe manager `m_probes`
- `m_preview` - a separate minimal forward+composite path for editor thumbnails
  (it does **not** run the full pass list)

Post passes do not blit results back into `m_sceneHDR`: the frame context
carries a colour chain (`colorSrc`/`colorDst` + `flipColor()`). A pass samples
`colorSrc`, writes `colorDst`, and flips; after the first flip the chain
ping-pongs between the two scratches and the composite reads whichever is
current. Depth and the G-buffer stay on the geometry target and are sampled
from there.

### The passes (fixed order)

From `gl_backend.cpp` - a hardcoded `m_passes` list, run top to bottom:

| # | Pass | Does |
|---|------|------|
| 1 | Shadow | Renders directional CSM + spot + point-cube depth maps into the atlas each frame. Culling and mesh-grouping are **not** done here - `GLShadowData::build` does both on the thread pool, so the pass only gathers, uploads and draws |
| 2 | DepthPrepass | Clears the scene target; early-Z for opaque geometry + writes the G-buffer (oct view-normal / roughness / metalness). Draws `ctx.opaqueBatch`, the shared batch the forward pass reuses |
| 3 | ResolveDepth | MSAA only: resolves depth (and the G-buffer when GTAO / decals / a debug view will read it) into `m_sceneHDR` |
| 4 | GTAO | Full-res ground-truth AO + bent normal into the mask target |
| 5 | ContactShadow | Screen-space sun visibility raymarch (skips when the scene has no directional light) |
| 6 | Skybox | Fills the background before geometry so transparents blend over it |
| 7 | ClusterCull | Compute: culls lights into the Forward+ cluster grid SSBO |
| 8 | FogCompute | Compute: froxel light inject + front-to-back integration (allocates the volumes on the first fog frame) |
| 9 | Forward | The PBR ubershader: opaque (depth-primed), alpha-mask (writes depth, alpha-to-coverage under MSAA), then back-to-front transparents sampling an opaque snapshot for refraction |
| 10 | Particles | CPU billboard particles into the scene target, depth-tested, never depth-writing |
| 11 | ResolveColor | MSAA only: resolves colour (and re-resolves depth when alpha-mask drew) into `m_sceneHDR` |
| 12 | Decals | Projected decal boxes blended into the post colour chain, sampling depth + G-buffer |
| 13 | FogApply | Composites the integrated froxel fog (chain: src -> dst) |
| 14 | DoF | Circle-of-confusion disk blur driven by the camera's focus distance / amount (chain: src -> dst) |
| 15 | Bloom | Bright-pass + mip-chain down/upsample off the chain; composite blends it back |
| 16 | Grid | World-space ground grid overlay into the chain (LEQUAL test done in its shader) |
| 17 | Composite | Tonemap to the backbuffer viewport (or a debug buffer per `renderMode`) |
| 18 | UI | Screen-space in-game UI overlay drawn flat on top (no-op when empty). See [ui.md](ui.md) |

IBL is **not** a pass: the persistent `GLIBLBaker` re-bakes inside `render()`
when `environment.hdrPath` changes or, for the procedural sky, when the sun
moves or a sky parameter changes - producing the irradiance, prefilter, and
BRDF/DFG products the forward pass samples. Reflection probes are baked at
frame end and bound per frame into a probe UBO; the SH irradiance volume
re-bakes when its box, grid, or bake version changes.

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
BRDF LUT / env cube), the GTAO factor, the contact-shadow mask, the scene
colour/depth/G-buffer samplers, the froxel fog volume, and the SH irradiance
volume. Treat `gl_bindings.h` as authoritative rather than hardcoding slot
numbers from memory.
