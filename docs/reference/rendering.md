# Rendering System

The renderer has two halves with one seam between them. The engine half builds a
backend-agnostic `RenderView` each frame. The backend half syncs its GPU
resources to that view and runs a **fixed, ordered list of passes** to produce
the image. There is **no engine-level render graph**, and **no pass abstraction is
exposed to the engine** - passes (`GLPass`) live entirely inside the backend as
an OpenGL implementation detail.

The renderer has no TAA, FXAA, motion blur, lens flare or auto-exposure, and
no shader variant cache ([engine.md](../guides/engine.md#4-what-has-already-been-decided)).
What it does have is listed below.

## Key files

- `src/engine/system/render/render_system.h` - RenderSystem (System subclass; owns the backend)
- `src/engine/system/render/render_view.h` - RenderView (the engine -> backend contract)
- `src/engine/system/render/data/` - the POD frame structs: CameraData, RenderObjects (with ObjectDraw), LightData, ProbeData
- `src/engine/system/render/data/` also carries DecalData, IrradianceVolumeData and ParticleData - the same flattening for what the decal, forward and particle passes read per frame
- `src/engine/system/render/editor_render_hooks.h` - EditorRenderHooks, the authoring-only second seam (thumbnails, material previews); see [engine.md](../guides/engine.md#5-what-everything-else-stands-on)
- `src/engine/system/render/render_settings.h` - RenderSettings + RenderMode (editable tuning)
- `src/engine/system/render/render_backend.h` - RenderBackend (the abstract seam)
- `src/engine/ecs/environment.h` - Environment (sky, night sky, fog), a scene-level struct
- `src/backend/opengl/` - the OpenGL backend

## Per-frame flow

```
RenderSystem::update(FrameContext)
  |-- RenderView::build(scene, visibility, ui, splash, poses, particles)  // engine side, backend-agnostic
  |     |-- objects         borrowed from the Visibility product: every mesh's
  |     |                   model, bounds, handles and bone range, written once
  |     |                   by the cull, and the index lists naming what the
  |     |                   camera sees (UNSORTED - the backend does all sorting
  |     |                   and partitioning) and what the shadow pass and the
  |     |                   offline captures draw (the whole scene, casters first)
  |     |-- camera          copied from the Visibility snapshot
  |     |-- buildLights / buildProbes / buildDecals / buildParticles
  |     |-- skinMatrices / ui  borrowed from the pose buffer and the UISystem
  |     |-- copy Environment into the view (RenderSystem copies the settings)
  |-- backend.render(view, resources)             // GLBackend
        |-- onWorldReplaced   drop every cache whose world or asset graph was replaced
        |-- GLView::sync      upload/refresh changed GPU resources
        |-- bake IBL          when the HDR path changed, or the procedural
        |                     sky's sun/params moved (persistent GLIBLBaker)
        |-- shadow plan       assign atlas slots, fork the per-tile caster cull
        |                     onto the thread pool (GLShadowData::finishCull joins
        |                     it just before the passes; everything between runs
        |                     beside it)
        |-- per-frame UBOs    camera, lights, and the shadow block the plan filled
        |-- partitionDrawables  split the camera's objects into opaque /
        |                     alpha-mask / transparent
        |-- skin palette      upload skinMatrices once (GLSkinPalette, SSBO 5);
        |                     its count is the frame's "is anything posed?", and
        |                     every skinned code path below is gated on it
        |-- objects           upload every object's model once (GLObjectBuffer,
        |                     SSBO 10), and its first bone (SSBO 6) when posed;
        |                     every instanced draw of the frame indexes it
        |-- opaque batch      group the opaque bucket into instanced runs (once,
        |                     shared): an index list of objects, in run order,
        |                     and a multi-draw command per run
        |-- run the passes in order
        |-- irradiance update re-bake the SH volume when its box/grid changed
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
| `viewportX` / `viewportY` / `viewportWidth` / `viewportHeight` | `uint32_t` | Scene render rect |
| `surfaceWidth` / `surfaceHeight` | `uint32_t` | The full backbuffer the viewport rect sits within: what a window-wide pass measures against, and what lets a bottom-left backend flip the rect |
| `camera` | `CameraData` | view / projection / viewProjection + position, from whatever `Visibility` rendered through: the scene's active camera, or an authoring host's view (`HostView`, [visibility.md](visibility.md#where-the-view-comes-from)). The view never knows which |
| `hasCamera` | `bool` | Whether a camera resolved this frame. Without one the scene's lists - lights, probes, decals, particles, the irradiance volume - are left empty rather than gathered, so a backend keeps what it baked of them instead of taking the empty lists for a scene without them |
| `objects` | `const RenderObjects*` | Borrowed from the `Visibility` product, which outlives the render: one object per Mesh, at its storage index, as parallel columns the cull wrote once - `models`, `bounds`, `draws` (mesh, material, bone count) and `skinFirst` - and two lists of object indices. `visible` is what the camera sees, in object order (UNSORTED; the backend sorts and partitions). `scene` is every drawn mesh, camera or not, shadow casters first: the shadow pass draws its first `casterCount`, the reflection-probe and irradiance captures the opaque ones, because a capture looks every way - and the opaque ones of the caster prefix again, into the key light's map a capture draws for itself. Every item a pass draws is an index into it; nothing copies a matrix. Never null once `build()` has run |
| `skinMatrices` | `const vector<mat4>*` | The frame's bone palettes (`PoseBuffer::palette()`, borrowed whole); each object carries its rig's range in them (`skinFirst`, and `ObjectDraw::skinCount`, 0 = not posed). Null when the frame posed nothing, which is what turns the backend's whole skinned half off |
| `lights` | `vector<LightData>` | Enabled lights with world transforms, each with its entity slot - which decides which lights get the scarce shadow tiles; the tile itself is the backend's to assign |
| `probes` | `vector<ProbeData>` | Reflection probes in the scene |
| `decals` | `vector<DecalData>` | Gathered scene-wide, not camera-culled: a decal's own material is usually its own, so the backend syncs it off this list |
| `particlesAdditive` / `particlesAlpha` | `vector<ParticleData>` | Billboards, split by blend and unsorted: the additive half is order-independent, and `GLParticlePass` sorts the alpha half back-to-front |
| `irradianceVolume` / `hasIrradianceVolume` | `IrradianceVolumeData` + `bool` | The scene's one baked-GI volume, chosen by `findIrradianceVolume` and with its grid clamped to `IrradianceVolume::MAX_RESOLUTION` on the way here. A value and a flag rather than a list, the way `Visibility` carries its camera - see "A scene has one irradiance volume" below |
| `settings` | `RenderSettings` | Pass toggles + per-effect params, copied each frame |
| `environment` | `Environment` | The scene's sky (HDR or procedural), night sky and fog, copied whole |
| `ui` | `const UIDrawData*` | The UISystem's screen-space draw list, borrowed; independent of the camera, so it survives the no-camera path. Null when the UISystem has not run |
| `splash` | `SplashFrame` | The startup logo over black, or nothing once the sequence is over. Survives the no-camera path for the same reason |
| `worldEpoch` | `uint64_t` | `Scene::epoch()` at build time. A replaced world reuses the slots and poses of the one before it, so a backend cache of what a *place* looked like - a baked probe, a baked SH volume - cannot tell on its own; `GLBackend::onWorldReplaced` is the one place that does |

The frontend does **not** sort what it draws - `visible` is in object order.
All sorting and partitioning happens in the backend: `partitionDrawables` splits
opaque from transparent, `GLInstanceBatcher` groups by (skinned, material, mesh)
for instancing - by (material, mesh) alone on a frame that posed nothing, where
there is no second program to sort towards - and `GLForwardPass` drives the
depth-writing classes (Opaque, AlphaMask, Unlit) before the back-to-front
transparent run. The transparent forward phase snapshots the opaque scene for
refraction, so opaques must already be drawn.

## RenderSettings and RenderMode

`RenderSettings` (in `render_settings.h`) is plain data owned by the Engine and
carried on `FrameContext::render`: mutated by the editor's Render Settings panel
or a game's settings screen, read by the visibility pass, and copied into the
view each frame.

- **Toggles:** `gtao`, `bloom`, `probes`, `ssr`, and the editor grid's axes
  (`gridAxisX/Y/Z`): a line for each axis on and the plane of every two, X and Z
  the ground; it draws while any is on, `gridShown()`.
- **Per-effect params:** GTAO (radius/intensity/power), screen-space
  reflections (`ssrMaxRoughness`, `ssrMaxDistance`), bloom
  (strength/threshold/knee/radius; the threshold and knee are as the viewer sees
  the frame, after the `exposure`, so raising it does not change what glows).
- **Quality:** `msaaSamples` (1/2/4/8), `shadowResolution` (1024/2048/4096, the
  largest shadow tile, which the sun's near cascades take; each sun and spot's
  penumbra is its own `sourceRadius`, [lighting.md](lighting.md)),
  `textureFiltering` (`Nearest` / `Bilinear` / `Trilinear`) and
  `textureAnisotropy` - the degree layered on trilinear sampling, pinned to 1 by
  the coarser two modes and clamped to the ceiling that
  `RenderBackend::maxAnisotropy()` reports. The editor shows the pair as one
  list (Nearest ... Anisotropic 16x, truncated to that ceiling);
  `GLView::setTextureFiltering` offers it to every synced texture when it
  changes, and a texture uploaded since takes it as it is built - sampler state
  rides no version gate, so those are the two ways a texture can lack it.
  Offered, not imposed: each texture resolves it against its own
  `TextureParams::filterOverride`, and one that states `Nearest` keeps
  `Nearest` - see [Resources](resources.md#textureasset) for why the asset
  outranks the setting on that one question. The same resolve respects whether
  the texture has a mip chain (`isMipmapped` - the one it carries from the cook,
  or the one GL builds at upload), so a texture without one is never given a
  mipmap minification filter.
- **`tonemap`:** the display transform the composite pass ends the frame with -
  `ACES` (the default: Hill's fit of the RRT and ODT, the ACES of three.js, Godot
  and Bevy, without three.js's 1/0.6 pre-scale),
  `Reinhard` (`c/(c+1)`, which never reaches white and flattens the mid-tones),
  `KhronosNeutral` (glTF's, built to hold an object's authored albedo as it
  brightens rather than pushing it toward white) or `AgX` (Sobotka's, in
  Wrensch's minimal fit: every channel runs to white together through a wider
  gamut, so a bright saturated light whitens instead of turning another colour,
  for a flatter look). It ships in `project.json`, unlike `renderMode` beside it,
  because it is a decision about what the game looks like rather than about what a
  developer is inspecting. The `TONEMAP_*` constants the shader switches on are
  written out of the enum by `GLBackend::shaderConstants`, the same way `MODE_*`
  are. This is not auto-exposure, which the engine refuses - a fixed curve
  decides how an authored range lands, where auto-exposure makes the brightness
  itself a moving target.
- **`exposure`:** a fixed exposure in stops (EV; 0 is as lit) the composite
  scales the frame by - 2^exposure, after the bloom is added and before the
  tonemap - so an author decides where the lit range lands on the curve. It sits
  beside `tonemap` because the two together are the display transform the
  project ships. Authored and constant, it is the opposite of the refused
  auto-exposure, which would move it every frame.
- **`cullMaxDistance` / `cullMinPixels`:** the visibility pass's two thresholds -
  how far away an entity stops being drawn, and how small on screen. They ship
  in `project.json` like the rest of this struct, and `VisibilitySystem` reads
  them off the same `FrameContext::render` the renderer does, so the editor's
  Culling card edits the one copy there is.
- **`renderMode`:** composite output selector - `Default` (final image) or a debug
  view: `Wireframe` (the opaque and alpha-masked draws again as lines, over the
  shaded frame), `LightingOnly` (every surface a white dielectric), the material
  views `Albedo`, `Roughness` and `Metalness` (the surface as the forward pass
  samples it, written raw, the sky black), `Normals`, `Depth`, `AmbientOcclusion`,
  `GiOnly`, `DirectOnly`, `Clusters` (Forward+ light-count heatmap), `Bloom`,
  `ShadowAtlas` and `Fog`. The `MODE_*` constants the composite shader
  switches on are written out of this enum by `GLBackend::shaderConstants` into
  the prelude every stage is compiled with - there is no generated file and
  nothing to include.

Scene-look settings (the HDR or procedural sky, the night sky, fog, IBL
intensity) live in `Environment` and serialize with the scene. `RenderSettings`
is the project's: everything above except `renderMode` and the grid, which are the
editor's own, ships in `project.json` (`visitShippedRenderFields`), because it
decides what the game looks like rather than what one scene does.

## RenderBackend - the seam

Abstract interface (`render_backend.h`). The engine only ever sees this; it never
includes a `gl_*` header. Core methods: `init`, `render(view, resources)`,
`reloadChangedShaders` and `maxAnisotropy`. `readFrame(view, pixels)` reads back
the viewport rect `render` just drew, top row first: a screenshot is a request
gameplay leaves on the window (`WindowManager::saveScreenshot`), which
`RenderSystem` takes after the next frame it renders, reads back through the
backend and writes as a PNG (`debug/screenshot.h`). So a screenshot is the game's
view - the scene and its UI - without the editor's panels around it, and a host
that renders nothing writes none. The editor's offscreen renders -
`renderPreview` for material and asset thumbnails, and the texture and chrome
image lookups - are on the second seam, `EditorRenderHooks`, which a backend
opts into by overriding `editorHooks()`. One implementation exists (OpenGL), and
a second is not planned - see
[engine.md](../guides/engine.md#4-what-has-already-been-decided).

## OpenGL backend

`GLBackend` owns:

- `Vkm::GL::Context` - GLEW state + draw helpers (from vkmGL)
- `GLView` - the GPU resource synchronizer, and the `GLMeshPool` every mesh lives in
- Render targets: `m_sceneHDR` (the geometry target: colour + depth + G-buffer),
  `m_sceneMS` (multisample twin when MSAA is on), `m_postA`/`m_postB`
  (colour-only post ping-pong scratches), `m_ao` (GTAO, sized by its pass once
  the pass runs), `m_bloom`. A frame with an empty viewport draws nothing. A target
  nothing but its pass reads is that pass's own rather than the backend's: the
  GTAO pass keeps the linear-depth mip chain it prefilters and its raw,
  undenoised result, and the reflection
  pass the lit frame's mip chain and its per-pixel hits
- `m_shadowAtlas` + `m_shadowData`, `m_ibl` + `m_iblBaker`, `m_clusterGrid`,
  `m_fog` (froxel volumes, lazily allocated), `m_irradiance` + its baker, the
  reflection-probe manager `m_probes`
- `m_preview` - a separate minimal forward+composite path for editor thumbnails
  (it does **not** run the full pass list). Like `m_fog` it builds itself on
  first use, so the runtime host never compiles its programs or allocates its
  scratch target

**MSAA.** When `msaaSamples > 1` the geometry passes render into `m_sceneMS` and
`GLResolvePass` resolves it into `m_sceneHDR`; the whole post chain stays
single-sample, so no post pass ever has to know how many samples the frame drew
with. The multisample attachments are textures, and each resolve is one
fullscreen draw that reads their samples rather than a blit that averages
them, because what a pixel's samples should become differs per image. Depth
and the G-buffer take sample 0 - the same sample for both - since along a
silhouette the average of two surfaces' depths or encoded normals is neither
surface, and every screen-space reader wants one that exists. Colour is
averaged through a tonemap (Karis: each sample weighted by `1 / (1 + c)`, `c`
its brightest channel after the `exposure`), so one bright sample of a highlight
does not outweigh the rest of its pixel and a bright edge stays antialiased - a
saturated blue one too, which luma would barely weigh. The reflection inputs take the same
weights, so the reflection the Reflections pass subtracts is the one the
resolved colour holds. When it is off, the geometry passes render straight into `m_sceneHDR`, the
two resolve passes no-op, and the multisample storage is released rather than
kept against the setting coming back - at 4x it is the largest allocation in the
frame.

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
| 1 | Shadow | Renders directional CSM + spot + point-cube depth maps into the atlas. A spot's tile or a point light's face is redrawn only when what it holds changed - its matrix, or a caster in it moved, was re-uploaded or is posed - and the sun's cascades, which follow the camera, every frame, with their depth clamped so a caster nearer the sun than a cascade's near plane still shadows. Culling and grouping are **not** done here - `GLShadowData::build` does both on the thread pool. The pass uploads the drawn tiles' lists of objects, and a draw command per run of casters sharing a mesh, into one `GLDrawList` - the transforms are the frame's object buffer - then draws each tile as one multi-draw per program and vertex layout - its runs are keyed static meshes first, then skinned ones drawn as stored, then posed ones (`ShadowRun::key`), so neither alternates - skinned casters included, through programs a frame that posed nothing never binds (see [animation.md](animation.md#the-gpu-path)), and alpha-masked ones, per material, through programs that cut the shadow by it ([lighting.md](lighting.md#shadow-atlas)). A tile with no casters is still cleared |
| 2 | DepthPrepass | Clears the scene target; early-Z for opaque geometry + writes the G-buffer (an oct view-normal, two channels, which GTAO, the decals, the Normals view and the reflection trace and resolve read). Draws `ctx.opaqueBatch`, the shared batch the forward pass reuses, one multi-draw per material run; it binds no material, since nothing it writes depends on one. Two programs (`prepass` / `prepass_skinned`), switched once at the skinned boundary |
| 3 | ResolveDepth | MSAA only: one draw resolving depth and the G-buffer into `m_sceneHDR`, sample 0 of each |
| 4 | GTAO | Full-res ground-truth AO + bent normal into `m_ao`. First folds the scene depth into a linear-depth mip chain of its own (`shaders/gtao/prefilter`); the horizon search then reads each step from the level its pixel length picks, which is what keeps a wide radius in cache. The search writes a target of the pass's own, and one compute dispatch (`shaders/gtao/denoise`) averages its visibility over a 5x5 neighbourhood on each pixel's own plane into `m_ao` - edge-aware and spatial only, with no history - and only then shapes it by intensity and power |
| 5 | ClusterCull | Compute: culls lights into the Forward+ cluster grid SSBO |
| 6 | FogCompute | Compute: froxel light inject + front-to-back integration (allocates the volumes on the first fog frame). Before anything is lit, because everything lit fogs itself through it - see [Fog](#fog) |
| 7 | Skybox | Fills the background before geometry so transparents blend over it, fogged at the far plane. With fog on and no sky to show it still draws, black, so the fog lies in front of the background too |
| 8 | Forward | The PBR ubershader: opaque (depth-primed), alpha-mask (writes depth, alpha-to-coverage under MSAA), then back-to-front transparents sampling an opaque snapshot for refraction; one multi-draw per material in each, since its textures are bound per material. Every surface is fogged in the shader at its own depth. Beside the colour, while `ssr` is on, it writes each pixel's reflection inputs - the environment reflection's weight and roughness, and the reflection itself as it was added (weight times radiance), both dimmed by the fog as the colour is - into colour attachments 2 and 3 of the scene target, which the Reflections pass reads, and which the target does not carry at all with `ssr` off; a transparent surface dims both under it by its own opacity ([lighting.md](lighting.md)). Two programs (`pbr` / `pbr_skinned`) sharing one fragment file and one per-frame uniform set |
| 9 | Particles | CPU billboard particles into the scene target, depth-tested, never depth-writing, each fogged in the shader at its own depth. Into the reflection inputs too, as zero at the particle's opacity, so smoke dims the reflection behind it as a transparent surface does |
| 10 | ResolveColor | MSAA only: one draw resolving colour and the reflection inputs, all tonemap-weighted alike, into `m_sceneHDR`, and depth again when alpha-mask drew |
| 11 | Reflections | Screen-space reflections on the resolved frame. Copies the lit colour into a mip chain and filters it down; traces each pixel smoother than `ssrMaxRoughness` along its G-buffer normal's reflection through this frame's depth, rejecting surfaces seen from behind; then, through the reflection weight and the environment reflection the forward pass wrote beside the colour, replaces the probe / sky reflection with the traced colour - a glossy pixel averaging its neighbours' rays on the same surface (chain: src -> dst) |
| 12 | Decals | Projected decal boxes blended into the post colour chain, sampling depth + G-buffer, lit as the surface they land on is lit diffusely - by the key light through its cascades, and by the irradiance volume or the sky under GTAO (`shaders/ambient.glsl`, which the fog reads too) - and fogged at its depth. After the reflections, so a glossy floor's reflection does not paint over what is stuck to it. With the reflections on, the chain is already off the geometry target and the decals blend in place; with them off, the pass first copies the frame into the chain (`GLPass::promoteColorChain`) |
| 13 | DoF | Circle-of-confusion disk blur driven by the camera's focus distance / amount, with a radius of at most `Camera::dofMaxBlur` of the viewport's height, so it looks the same at any resolution (chain: src -> dst) |
| 14 | Bloom | Compute, one dispatch per level (a framebuffer bind and a draw cost the CPU about three times as much). Bright-pass + mip-chain down/upsample off the chain, the first level capped and cleared of NaNs; composite adds it |
| 15 | Composite | The bloom added at `bloomStrength` - it holds only the light past the threshold, so nothing else is dimmed - then the `exposure`, then the `tonemap` curve and the exact sRGB encode (`shaders/color.glsl`, which the UI pass shares) to the backbuffer viewport, dithered by half a step after the encode (or a debug buffer per `renderMode`) |
| 16 | Grid | The editor's world grid: grids on the XZ, XY and ZY planes and the three axis lines, one fullscreen draw blended into the backbuffer viewport. After the tonemap, so the axes keep `Math::AXIS_COLORS` as the gizmos show them. Each pixel's ray finds its point on each plane and its nearest point on each axis, each tested against the scene's depth in the shader, and they blend far to near |
| 17 | UI | Screen-space in-game UI overlay drawn flat on top (no-op when empty). See [ui.md](ui.md) |
| 18 | Splash | The startup logo over black, covering the whole surface. A no-op once the sequence is over |

The Splash pass is last because a splash is not drawn on top of the frame -
it is what is on screen instead of one. It covers the whole surface rather
than the viewport rect, so in the editor it hides the panels as well, and
`EditorSystem` stands aside while it is up rather than painting chrome over
it. What it draws arrives on `RenderView::splash` as a path and an opacity,
not as pixels: the engine core cannot decode a file, so `SplashSystem`
decides which logo is up and how faded, and the pass - which links the
loaders - reads it, uploading once per logo rather than once per frame.

IBL is **not** a pass: the persistent `GLIBLBaker` re-bakes inside `render()`
when `environment.sky.hdrPath` changes or, for the procedural sky, when the sun
angles or a sky parameter change - producing the irradiance and prefilter
products the forward pass samples. The BRDF/DFG LUT beside them depends on no
environment, so the backend integrates it once at `init` and every bake leaves it alone. A scene
that names no sky drops the baked one. A reflection probe is baked at frame end
when it is new, moved, resized or bumped, and the baked ones are bound per frame
into a probe UBO; the SH irradiance volume re-bakes when its box, grid, or bake
version changes. Neither re-bakes when the sky changes: each is a capture of the
scene under the sky it was baked with, and its `bakeVersion` is how an author
takes it again. A frame with no camera bakes neither and keeps both.

**A scene has one irradiance volume**, and which one is `findIrradianceVolume`
- the lowest-slot entity carrying an `IrradianceVolume` and a `Transform`, the
same rule that decides the eye, the key light and the ear. The backend holds a
single probe grid and the forward pass places a fragment in a single box, so a
second volume is not a second source of indirect light; it is one no frame ever
reads, which is why the Inspector's card says so on the ones it did not pick.
`RenderView` therefore carries `irradianceVolume` and `hasIrradianceVolume`
rather than a list, the way `Visibility` carries its camera.

**A probe inside a wall is refused, not shipped.** The radiance capture culls
back faces, so from inside a solid it sees straight through the walls and
records the room on the far side - light a trilinear fetch would then blend into
the near one. So each probe is captured twice: once for radiance, once as a
backface mask (`shaders/irradiance/backface`, culling off, one bit per
direction). The SH projection reads both, and a probe whose nearest surface
faces away over more than a quarter of the sphere is marked refused in the
alpha of its first coefficient. `dilateProbeGrid`
(`system/render/irradiance_dilation.h`) then reads the grid back and replaces
every refused probe with a blend of its trusted neighbours, spreading one cell
per round, before the grid is uploaded again. The repair is offline because the
forward pass samples the volume with hardware trilinear filtering and so cannot
skip a probe. A volume where *no* probe
was trusted is not marked ready at all, so the frame falls back to the global
IBL rather than to a grid of guesses.

**A capture is shadowed by the key light.** The frame's cascades are fitted to
the camera, so a probe or irradiance capture draws a map of its own: the key
light's depth over the region the bake describes - a probe's influence box,
the volume's box - across the light, and along it every caster standing over
that region, since a roof well above a room still shades its floor. Without it
the sun would light indoor floors through their ceilings in the only indirect
diffuse there is. The other lights capture unshadowed. The map is 2048 texels across
the region and read through the ordinary hard kernel, installed as a
one-cascade ShadowBlock the next frame's own upload replaces.

**A bake is frame time.** All three run inside `render()`, so the frame that
notices the change is the frame that pays: an irradiance grid is twelve cube
faces per probe. That is why
`IrradianceVolume::MAX_RESOLUTION` exists and why `RenderView` clamps every
axis to it - the grid is a product, so the cost is cubic in a number a scene
file can hold anything in.

### Fog

The froxel volume (`GLFogPass`: `shaders/fog/inject`, then `integrate`) holds,
for each froxel of the camera's frustum out to `FogSettings::maxDistance`
(metres, the far plane if nearer - `GLFogVolume::depth`), the light the medium scatters toward
the eye up to that froxel's far bound, and the transmittance of the light from
behind it. The slices are exponential between the near plane and that reach,
so a reach nearer than the far plane spends them where fog is seen; a point
past it takes the last slice's value - the fog accumulated up to the reach,
and nothing scattered beyond. What it scatters is every clustered light through the medium - the
sun through its cascades - and the environment's own light: the sky's
irradiance, or the irradiance volume's where one covers the froxel, read once
per froxel facing away from the eye (`shaders/ambient.glsl`, as the decals read
it), so fog in shade or indoors is lit as the walls there are rather than
black. No pass applies it to the finished frame. Everything drawn through
the medium fogs itself in its own shader, at its own depth, as it is drawn -
the skybox at the far plane, every surface of the forward pass, the particles,
the decals - through `shaders/fog.glsl`, given the volume by
`GLPass::bindFog`. A pass over the finished frame could fog a pixel only by the
opaque surface behind it: a muzzle flash a metre away in thick fog would vanish,
and a near window would be fogged as if it were the street behind it.

How a colour takes the fog follows how it is blended, with `S` the scattered
light in front of it and `T` the transmittance:

- **Opaque, alpha-blended and alpha particles** write `colour * T + S`. The
  blend scales both terms by the opacity, and the rest of the pixel keeps the
  fog that was already in front of what lies behind.
- **Additive particles** hide nothing, so they bring no scattered light of
  their own: `colour * T`.
- **A transmissive surface** fogs its own colour before it mixes in the
  refracted scene. That copy is taken after the opaque scene and the sky were
  fogged, so it is fogged over its whole path to the eye already.
- **The reflection inputs** are dimmed by `T` with the colour, so the
  Reflections pass replaces a reflection that lost to the fog with a traced one
  that loses the same. The traced colour is read from the fogged frame, so it
  carries the fog between the eye and what it reflects rather than between the
  reflector and it - an approximation, but one that agrees with the surface
  around it.

With fog on and no sky to show, the Skybox pass still draws the black
background, because the fog lies in front of it too. The fog costs one 3D fetch
per shaded fragment rather than a framebuffer bind and a fullscreen draw, and
under MSAA each surface covering part of a pixel takes its own fog rather than
the one at sample 0's depth. The shading-split debug views (`GiOnly`,
`DirectOnly`, `Clusters`) are left unfogged, and the `Fog` view shows the
scattered light in front of each pixel's opaque surface. The offline captures
and the editor preview never fog - `u_hasFog` is 0 in their programs - because
the volume describes this camera's frustum, not a probe's. The Grid and UI
draw after everything and are overlays, so they are not fogged either.

### Particles

The one drawn thing whose simulation is not in the renderer. `ParticleSystem`
steps every `ParticleEmitter` on the CPU into pools of its own - the live
particles are simulation state, published as `FrameContext::particles`
(`LiveParticles`), not fields of the authored component - and
`RenderView::build` flattens them into the two `ParticleData` lists the
pass reads. It is
CPU-side: the counts an FPS needs - muzzle flashes, impacts, sparks - are small,
and it keeps an emitter authorable as plain component data rather than as a
compute program.

Four things about it are load-bearing and easy to get wrong:

- **Particles are world-space and are never re-based.** A particle records the
  emitter's *resolved world position* at the instant it spawned and then moves on
  its own; moving the emitter afterwards does not drag it. That is what a trail
  is, and it is also why the emitter's origin has to be right at spawn time -
  a stale origin is not a frame of lag, it is baked into every particle that
  frame emitted.
- **The system runs in the Transform stage, after `HierarchySystem`.** For the
  reason above: it reads a resolved world transform, so it has to run after the
  thing that resolves it. `AudioSystem` sits beside it for the same reason and
  `BoneSocketSystem` sits *ahead* of the resolve for the mirror of it.
- **`spawnAccumulator` carries the fractional remainder**, so an emitter at 3
  particles a second still emits evenly at 240 fps instead of rounding to zero
  every frame. At `maxParticles` only the fraction survives: banking whole spawns
  would let a long-saturated emitter discharge every credit at once the instant
  particles start dying.
- **`additive` splits the draw, not just the blend.** The additive half is
  order-independent and goes out unsorted; the alpha half is sorted back-to-front
  by `GLParticlePass`. That is why the two lists on `RenderView` are separate.

Everything else is authored per emitter: `rate`, `lifetime`, `maxParticles`, the
initial `velocity` plus a per-axis random `spread` (drawn from a generator the
system owns and reseeds with each world, so a world loaded again draws the same
sequence), a constant `acceleration`,
and the colour/size ramp that `RenderView::build` evaluates at each particle's
age, so the pass draws each billboard as it is handed. `softness`
is the billboard's edge falloff - 1 is a soft blob, 0 a hard-edged disc.

### GLView - GPU sync

Holds four tables keyed by handle id - mesh, material, texture, font atlas - each
slot remembering the version and handle generation it was uploaded at.
`sync(view, resources)` walks every list on `RenderView` that names a handle -
the objects the camera sees (mesh, material and the material's textures), the
scene-wide objects (mesh, and for the shadow casters among them the material
and its textures too, since a cutout casts through its map), decals (material
and its textures) and the UI draw commands (font atlas, and each image's texture) - and
`ensure()`s each asset, uploading only when the version moved on or the
generation says the slot was recycled (see [resources.md](resources.md) for
the version mechanism). Each sync bumps a stamp, and a slot remembers the stamp
it was last checked under, so an asset a thousand objects share is looked up
once a sync and every later encounter is one compare. Skin data needs nothing
here: it rides on `MeshAsset` through the same version gate, and the palette is a
per-frame array carrying no handles at all.

That list is the whole rule, and it has to be, because three of the four are
gathered scene-wide rather than from the visible set: an off-screen occluder's
mesh and material and a decal's own material need never appear among what the camera sees, and every
pass answers a GPU object it cannot resolve by silently skipping the draw. A scene load or play-stop restore swaps the whole asset
graph and restarts its handles and versions, which no per-asset gate can see, so
the backend calls `invalidate()` and the next sync repopulates. The per-frame
UBOs and the shadow / IBL sets are not here - GLBackend owns those. All materials
share one PBR ubershader, built as two programs (`pbr` and `pbr_skinned`, which
differ only in the vertex stage); features are runtime uniform toggles, not
compiled `#ifdef` variants.

### Shader binding contract

Every binding point and texture unit is one row of `VKM_GL_BINDINGS` in
`src/backend/opengl/convention/gl_bindings.h` - the single source of truth. The
table expands once into the `GLBindings` constants C++ binds with and once, in
`GLBackend::shaderConstants`, into the `#define`s of the prelude every stage is
compiled with (`UBO_CAMERA`, `SSBO_LIGHTS`, `POST_SLOT_SCENE_DEPTH`,
`MATERIAL_SLOT_ALBEDO`, ...), which a `layout(binding = ...)` qualifier takes -
every sampler's, so no unit is set from C++. A material map's bit in
`textureFlags` is its slot (`hasTex` in `shaders/material.glsl`). The same goes
for the fragment output locations (`OUT_*`, where a location is the scene
target's colour attachment of that number), the
compute work-group sizes a `local_size` and a dispatch count must agree on
(`GROUP_*`), and the light and material types the shaders switch on
(`LIGHT_*`, `MAT_*`, written from `LightType` and `MaterialType`). The image
units a compute pass binds for itself - the fog and SH-projection volumes, and
the level of a mip chain being written - are the pass's own and stay literal.
Vertex attributes (`ATTR_*`, from `GLBindings::VertexAttributes`) are
per-vertex position/normal/uv/tangent (slots 0-3)
plus one per-instance uint at slot 4 (binding 4, divisor 1) naming the instance's
object - its index into the storage buffers every object's model (binding 10)
and first bone (binding 6) went up in, once for the frame. Every instanced draw,
in every pass, takes that shape: a batch, a shadow tile and a capture differ
only in the list of objects they hand it. On a skinned mesh only, bone indices and
weights at slots 8/9 in a **second stream at divisor 0**, parallel to the
vertices.

**Every mesh lives in one pool** (`GLMeshPool`, owned by `GLView`): one index
buffer, and one vertex array per layout - static, or skinned with its second
stream - over buffers every mesh of that layout shares, each `GLMesh` a range
in them. That is what lets a run of different meshes go out as one
`glMultiDrawElementsIndirect` (core in 4.3): a `GLDrawList` holds the object
indices and a command per run - the mesh's `firstIndex` and `baseVertex`, and a
`baseInstance` that slices the object list through the divisor-1 attribute,
since 4.3 core has no `gl_BaseInstance`. A pass binds program and material state
between multi-draws and nothing between the commands of one, which is the point
on a driver whose cost is the validation after each state change. A full stream
grows by doubling and copies on the GPU; a mesh's range is offsets, so nothing
that names it moves. `Vertex` stays 48 bytes: see
[animation.md](animation.md) for why the skin rides beside it rather than in it.
Storage binding 5 carries the frame's bone palettes, and binding 0 the lights,
a storage buffer because the list outgrows a uniform block. UBO binding points
cover the Material, Camera, Shadow and Probe blocks. The Camera block
(`shaders/camera.glsl`, `CameraUBO` in `gl_camera.h`) carries every
fact about the eye a pass reads - view, projection and their inverses, the
position, the viewport size, near and far - so no pass sets one as a uniform of
its own; the scene capture and the editor preview fill their own. Texture slots cover the PBR material
maps plus the shadow depth (one tiled 2D atlas, then one cube per point-light
slot), IBL set (irradiance / prefilter / BRDF LUT / env cube), the GTAO factor,
the scene colour/depth/G-buffer samplers, the froxel fog volume, and the SH
irradiance volume.
