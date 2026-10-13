# Rendering System

The renderer has two halves with one seam between them. The engine half builds a
backend-agnostic `RenderView` each frame. The backend half syncs its GPU
resources to that view and runs a **fixed, ordered list of passes** to produce
the image. There is **no engine-level render graph**, and **no pass abstraction is
exposed to the engine** - passes (`GLPass`) live entirely inside the backend as
an implementation detail.

The renderer has no TAA, FXAA, motion blur, lens flare or auto-exposure, and
no shader variant cache ([engine.md](../guides/engine.md#4-what-has-already-been-decided)).
What it does have is listed below.

## Key files

- `src/engine/system/render/render_system.h` - RenderSystem (System subclass; owns the backend)
- `src/engine/system/render/render_view.h` - RenderView (the engine -> backend contract)
- `src/engine/system/render/data/` - the POD frame structs: CameraData, RenderObjects (with ObjectDraw), LightData, ProbeData
- `src/engine/system/render/data/` also carries DecalData, IrradianceVolumeData, ParticleData and OceanData - the same flattening for what the decal, forward, particle and ocean passes read per frame
- `src/engine/system/render/editor_render_hooks.h` - EditorRenderHooks, the authoring-only second seam (thumbnails, material previews); see [engine.md](../guides/engine.md#5-what-everything-else-stands-on)
- `src/engine/system/render/render_settings.h` - RenderSettings + RenderMode (editable tuning)
- `src/engine/system/render/render_backend.h` - RenderBackend (the abstract seam)
- `src/engine/platform/window/window_surface.h` - WindowSurface, what a backend asks of the window (context, present, swap interval)
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
        |-- sky tables        the atmosphere's air tables, when the air changed
        |-- bake IBL          an HDR when its path changed; the procedural sky
        |                     when its sun or a value moved - a step a frame
        |                     while the sun only drifts (persistent GLIBLBaker)
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
        |                     SSBO 10), its flags (SSBO 7: the viewmodel), and
        |                     its first bone (SSBO 6) when posed;
        |                     every instanced draw of the frame indexes it
        |-- opaque batch      group the opaque bucket into instanced runs (once,
        |                     shared): an index list of objects, in run order,
        |                     and a multi-draw command per run
        |-- run the passes in order
        |-- irradiance update re-bake the SH volume once a changed box/grid
        |                     holds still
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
| `objects` | `const RenderObjects*` | Borrowed from the `Visibility` product, which outlives the render: one object per Mesh, at its storage index, as parallel columns the cull wrote once - `models`, `bounds`, `draws` (mesh, material, bone count, whether the ocean is its surface) and `skinFirst` - and two lists of object indices. `visible` is what the camera sees, in object order (UNSORTED; the backend sorts and partitions). `scene` is every drawn mesh, camera or not, shadow casters first: the shadow pass draws its first `casterCount`, the reflection-probe and irradiance captures the opaque ones, because a capture looks every way - and the caster prefix again, a see-through transparent one cut by its alpha, into the key light's map a capture draws for itself. Every item a pass draws is an index into it; nothing copies a matrix. Never null once `build()` has run |
| `skinMatrices` | `const vector<mat4>*` | The frame's bone palettes (`PoseBuffer::palette()`, borrowed whole); each object carries its rig's range in them (`skinFirst`, and `ObjectDraw::skinCount`, 0 = not posed). Null when the frame posed nothing, which is what turns the backend's whole skinned half off |
| `lights` | `vector<LightData>` | Enabled lights with world transforms, each with its entity slot - which decides which lights get the scarce shadow tiles; the tile itself is the backend's to assign |
| `probes` | `vector<ProbeData>` | Reflection probes in the scene |
| `decals` | `vector<DecalData>` | Gathered scene-wide, not camera-culled: a decal's own material is usually its own, so the backend syncs it off this list |
| `particlesAdditive` / `particlesAlpha` | `vector<ParticleData>` | Billboards, split by blend and unsorted: the additive half is order-independent, and `GLParticlePass` sorts the alpha half back-to-front |
| `particleTextures` | `vector<TextureHandle>` | What the particles show, each once; a particle names its own by `params.z`, 1 + its place here, 0 for the soft disc. `GLParticlePass` draws a run of particles a texture |
| `ocean` | `OceanData` | `OceanSystem`'s sea at the frame's moment (`present` when the scene has one, and a camera draws it): its waves, its foam and the drift of a `waves` material's maps - see "The ocean" below |
| `irradianceVolume` / `hasIrradianceVolume` | `IrradianceVolumeData` + `bool` | The scene's one baked-GI volume, chosen by `findIrradianceVolume` and with its grid clamped by `IrradianceVolume::clampedResolution` on the way here. A value and a flag rather than a list, the way `Visibility` carries its camera - see "A scene has one irradiance volume" below |
| `settings` | `RenderSettings` | Pass toggles + per-effect params, copied each frame |
| `environment` | `Environment` | The scene's sky (HDR or procedural), night sky and fog, copied whole |
| `ui` | `const UIDrawData*` | The UISystem's screen-space draw list, borrowed; independent of the camera, so it survives the no-camera path. Null when the UISystem has not run |
| `splash` | `SplashFrame` | The startup logo over black, with the line of its caption if it has one, or nothing once the sequence is over. Survives the no-camera path for the same reason |
| `worldEpoch` | `uint64_t` | `Scene::epoch()` at build time. A replaced world reuses the slots and poses of the one before it, so a backend cache of what a *place* looked like - a baked probe, a baked SH volume - cannot tell on its own; `GLBackend::onWorldReplaced` is the one place that does |

The frontend does **not** sort what it draws - `visible` is in object order.
All sorting and partitioning happens in the backend: `partitionDrawables` splits
opaque from transparent, `GLInstanceBatcher` groups by (skinned, mirrored,
material, mesh) for instancing - skinned only on a frame that posed something,
where there is a second program to sort towards; mirrored for an instance whose
transform has a negative determinant, which reverses its winding, so its draw
turns the front face clockwise for itself or culling would show its inside -
and `GLForwardPass` drives the
depth-writing classes (Opaque, AlphaMask, Unlit) before the back-to-front
transparent run. The transparent forward phase snapshots the opaque scene for
refraction, so opaques must already be drawn.

## RenderSettings and RenderMode

`RenderSettings` (in `render_settings.h`) is plain data owned by the Engine and
carried on `FrameContext::render`: mutated by the editor's Render Settings panel
or a game's settings screen, read by the visibility pass, and copied into the
view each frame.

- **Toggles:** `gtao`, `screenShadows`, `bloom`, `probes`, `ssr`, and the editor grid's axes
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
- **`occlusionCulling`:** the GPU occlusion cull of the opaque batch, which the
  depth prepass runs (`GLOcclusionCull`,
  [visibility.md](visibility.md#occlusion-culling-on-the-gpu-as-an-option)). Off
  by default, and a project's choice: the tiers leave it as the project sets it.
- **`renderMode`:** composite output selector - `Default` (final image) or a debug
  view: `Wireframe` (the opaque and alpha-masked draws again as lines, over the
  shaded frame), `LightingOnly` (every surface a white dielectric), the material
  views `Albedo`, `Roughness` and `Metalness` (the surface as the forward pass
  samples it, written raw, the sky black), `Normals`, `Depth`, `AmbientOcclusion`,
  `ScreenShadows` (the key light's screen-space shadow, white where lit), `GiOnly`, `DirectOnly`, `Clusters` (Forward+ light-count heatmap), `Bloom`,
  `ShadowAtlas` and `Fog`. The `MODE_*` constants the composite shader
  switches on are written out of this enum by `GLBackend::shaderConstants` into
  the prelude every stage is compiled with - there is no generated file and
  nothing to include.

**Presets.** A settings screen offering one quality choice renders with
`withPreset(project, GraphicsPreset::Low / Medium / High / Ultra)`: the
project's settings with what costs frame time - `msaaSamples`,
`shadowResolution` and `textureAnisotropy` - from a table beside it, each tier
at least the one under it, and `gtao`, `screenShadows`, `ssr` and `bloom` on only where both the
project and the tier have them. The look (an effect the project turned off, the
tonemap, the exposure, each effect's tuning, the culling) stays the project's,
and since each tier is built from the project's settings, choosing High after
Low brings back what Low turned off. A player who then turns one setting by
hand has changed that field alone.

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
image lookups, and the renderer of the editor's own ImGui - are on the second
seam, `EditorRenderHooks`, which a backend opts into by overriding
`editorHooks()`. The window reaches the API through a third, `WindowSurface`: the
host creates the window with the backend's surface (`GLWindowSurface`), which
hints the context, makes it current, loads GL and presents. One backend exists
(OpenGL); a second is planned - see
[engine.md](../guides/engine.md#4-what-has-already-been-decided).

## OpenGL backend

`GLBackend` owns:

- `Vkm::GL::Context` - GLEW state + draw helpers (from vkmGL)
- `GLView` - the GPU resource synchronizer, the `GLMeshPool` every mesh lives in, and the
  frame's streaming memory (`stream()`, a `Vkm::GL::StreamRing`): every per-frame list -
  draw lists, matrices, bone palettes, particles - is written into a persistently mapped
  buffer of three fenced regions, so the CPU neither waits on the GPU nor has the driver
  copy. What every pass reads - the matrices, flags and palettes - is then copied once into
  video memory (`DeviceStream`). `GLBackend::render` fences the region at the frame's end
- Render targets: `m_sceneHDR` (the geometry target: colour + depth + G-buffer),
  `m_sceneMS` (multisample twin when MSAA is on), `m_postA`/`m_postB`
  (colour-only post ping-pong scratches), `m_ao` (GTAO, sized by its pass once
  the pass runs), `m_bloom`. A frame with an empty viewport draws nothing. A target
  nothing but its pass reads is that pass's own rather than the backend's: the
  GTAO pass keeps the linear-depth mip chain it prefilters and its raw,
  undenoised result, and the reflection
  pass the lit frame's mip chain and its per-pixel hits
- `m_shadowAtlas` + `m_shadowData`, `m_ibl` + `m_iblBaker`, `m_atmosphere` (the
  procedural sky's tables), `m_clusterGrid`,
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

**Depth.** Clip z runs 0..w (`glClipControl`), and the eye's depth is
reversed - 1 at the near plane, 0 at the far - in a 32-bit float buffer
(`convention/gl_depth.h`). Perspective packs depth toward the near plane and a
float packs its precision toward zero, so reversed the two cancel: two surfaces
a centimetre apart a kilometre out stay apart, where a 24-bit buffer rounds them
to one depth past a few hundred metres. The engine's projections are glm's;
`GLCamera` remaps the camera block's matrices on the way in, and every shader
reads depth through `shaders/depth.glsl` (`FAR_DEPTH`, `isFarDepth`,
`nearerDepth`, ...). Shadow maps keep near 0, far 1 - the depth they always
wrote, through `GLDepth::shadowProjection` - since most are orthographic, whose
depth is linear and gains nothing reversed.

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
| 1 | Shadow | Renders directional CSM + spot + point-cube depth maps into the atlas. A spot's tile or a point light's face is redrawn only when what it holds changed - its matrix, or a caster in it moved, was re-uploaded or is posed - and the sun's cascades, which follow the camera, every frame, with their depth clamped so a caster nearer the sun than a cascade's near plane still shadows. Culling and grouping are **not** done here - `GLShadowData::build` does both on the thread pool. The pass uploads the drawn tiles' lists of objects, and a draw command per run of casters sharing a mesh, into one `GLDrawList` - the transforms are the frame's object buffer - then draws each tile as one multi-draw per program and vertex layout - its runs are keyed static meshes first, then skinned ones drawn as stored, then posed ones (`ShadowRun::key`), so neither alternates - skinned casters included, through programs a frame that posed nothing never binds (see [animation.md](animation.md#the-gpu-path)), and alpha-masked and see-through transparent ones, per material, through programs that cut the shadow by it ([lighting.md](lighting.md#shadow-atlas)). A tile with no casters is still cleared |
| 2 | DepthPrepass | Clears the scene target; early-Z for opaque geometry + writes the G-buffer (an oct view-normal, two channels, which GTAO, the decals, the Normals view and the reflection trace and resolve read). Draws `ctx.opaqueBatch`, the shared batch the forward pass reuses, one multi-draw per material run, binding each run's material (a `waves` one moves its vertices by the ocean) and leaving culling off for a double-sided material's runs. Two programs (`prepass` / `prepass_skinned`), switched once at the skinned boundary. With `RenderSettings::occlusionCulling` on it runs the GPU occlusion cull (`GLOcclusionCull`): it draws what the last cull showed, reduces that depth into a farthest-depth pyramid (`shaders/cull/pyramid`), tests every instance against it (`shaders/cull/instances`) and draws what was hidden before and is shown now; the forward pass then draws what was shown (`GLFrameContext::drawOpaque`) |
| 3 | ResolveDepth | MSAA only: one draw resolving depth and the G-buffer into `m_sceneHDR`, sample 0 of each |
| 4 | GTAO | Full-res ground-truth AO + bent normal into `m_ao`. First folds the scene depth into a linear-depth mip chain of its own (`shaders/gtao/prefilter`); the horizon search then reads each step from the level its pixel length picks, which is what keeps a wide radius in cache. The search writes a target of the pass's own, and one compute dispatch (`shaders/gtao/denoise`) averages its visibility over a 5x5 neighbourhood on each pixel's own plane into `m_ao` - edge-aware and spatial only, with no history - and only then shapes it by intensity and power |
| 5 | ScreenShadows | Compute: the key light's screen-space shadows into the AO target's alpha, from the scene depth - wavefronts of pixels in lines toward the sun, each marching a contact's length along its ray ([lighting.md](lighting.md#screen-space-shadows)). Clears the target to unoccluded first when GTAO did not run; nothing without a key light that casts |
| 6 | ClusterCull | Compute: culls lights into the Forward+ cluster grid SSBO |
| 7 | FogCompute | Compute: froxel light inject + front-to-back integration (allocates the volumes on the first fog frame). Before anything is lit, because everything lit fogs itself through it - see [Fog](#fog) |
| 8 | Atmosphere | Compute, under the procedural sky: the sky-view table the skybox draws the sky from and the aerial-perspective volume every lit pass lays over what it draws, both from the atmosphere's air tables, every frame ([lighting.md](lighting.md#image-based-lighting-ibl)). Each half is skipped when nothing reads it - the skybox hidden, `aerialPerspective` at 0 |
| 9 | Skybox | Fills the background before geometry so transparents blend over it: the procedural sky from this frame's sky-view table, with its sun, moon and stars, or an HDR sky's env cube. Only the froxel fog lies over it, at the far plane. With fog on and no sky to show it still draws, black, so the fog lies in front of the background too |
| 10 | Ocean | Under a scene's ocean: draws its ripples into the three repeating fields a `waves` surface's pixels read (`GLOceanFields::drawRipples`, `shaders/ocean_ripples.glsl`), and their levels; and captures its shore, how far the sea ahead of the eye is from anything breaking it (`GLOceanFields::captureShore`). After the sky and GTAO, whose texture units the forward pass reads them on ([The ocean](#the-ocean)) |
| 11 | Forward | The PBR ubershader: opaque (depth-primed), alpha-mask (writes depth, alpha-to-coverage under MSAA), then back-to-front transparents sampling an opaque snapshot for refraction; one multi-draw per material in each, since its textures are bound per material. Every surface takes the sky's air and the fog in the shader, at its own depth. Beside the colour, while `ssr` is on, it writes each pixel's reflection inputs - the environment reflection's weight and roughness, and the reflection itself as it was added (weight times radiance), both dimmed by the fog as the colour is - into colour attachments 2 and 3 of the scene target, which the Reflections pass reads, and which the target does not carry at all with `ssr` off; a transparent surface dims both under it by its own opacity ([lighting.md](lighting.md)). Two programs (`pbr` / `pbr_skinned`) sharing one fragment file and one per-frame uniform set |
| 12 | Particles | CPU billboard particles into the scene target, depth-tested, never depth-writing, the alpha-blended ones lit at their centres ([Particles](#particles)), each fogged in the shader at its own depth, and soft: faded over its authored size (half its width) where the scene behind comes near and over half a metre from the near plane, against a copy of the scene's depth the pass blits first (the target's own is attached as it draws). Alpha ones sorted far to near by view depth. Into the reflection inputs too, as zero at the particle's opacity, so smoke dims the reflection behind it as a transparent surface does |
| 13 | ResolveColor | MSAA only: one draw resolving colour and the reflection inputs, all tonemap-weighted alike, into `m_sceneHDR`, and depth again when alpha-mask drew |
| 14 | Reflections | Screen-space reflections on the resolved frame. Copies the lit colour into a mip chain and filters it down; traces each pixel smoother than `ssrMaxRoughness` along its G-buffer normal's reflection through this frame's depth, rejecting surfaces seen from behind; then, through the reflection weight and the environment reflection the forward pass wrote beside the colour, replaces the probe / sky reflection with the traced colour - a glossy pixel averaging its neighbours' rays on the same surface (chain: src -> dst) |
| 15 | Decals | Projected decal boxes blended into the post colour chain, sampling depth + G-buffer, faded where the surface turns from the projector and over the last fifth of each half of the box's depth, lit as the surface they land on is lit diffusely - by every light of the pixel's cluster through its shadow (an area light by its form factor, `areaFormFactor` in `shaders/ltc.glsl`, which the forward pass shares), and by the irradiance volume, the reflection probes or the sky under GTAO (`shaders/ambient.glsl`, which the fog reads too, without the probes) - and fogged at its depth. After the reflections, so a glossy floor's reflection does not paint over what is stuck to it. With the reflections on, the chain is already off the geometry target and the decals blend in place; with them off, the pass first copies the frame into the chain (`GLPass::promoteColorChain`) |
| 16 | DoF | Circle-of-confusion disk blur driven by the camera's focus distance / amount, with a radius of at most `Camera::dofMaxBlur` of the viewport's height, so it looks the same at any resolution (chain: src -> dst) |
| 17 | Bloom | Compute, one dispatch per level (a framebuffer bind and a draw cost the CPU about three times as much). Bright-pass + mip-chain down/upsample off the chain, the first level capped and cleared of NaNs; composite adds it |
| 18 | Composite | The bloom added at `bloomStrength` - it holds only the light past the threshold, so nothing else is dimmed - then the `exposure`, then the `tonemap` curve and the exact sRGB encode (`shaders/color.glsl`, which the UI pass shares) to the backbuffer viewport, dithered by a step of triangular noise after the encode (or a debug buffer per `renderMode`) |
| 19 | Grid | The editor's world grid: grids on the XZ, XY and ZY planes and the three axis lines, one fullscreen draw blended into the backbuffer viewport. After the tonemap, so the axes keep `Math::AXIS_COLORS` as the gizmos show them. Each pixel's ray finds its point on each plane and its nearest point on each axis, each tested against the scene's depth in the shader, and they blend far to near |
| 20 | UI | Screen-space in-game UI overlay drawn flat on top (no-op when empty). See [ui.md](ui.md) |
| 21 | Splash | The startup logo over black, covering the whole surface, and its caption's line small at the foot. A no-op once the sequence is over |

The Splash pass is last because a splash is not drawn on top of the frame -
it is what is on screen instead of one. It covers the whole surface rather
than the viewport rect, so in the editor it hides the panels as well, and
`EditorSystem` stands aside while it is up rather than painting chrome over
it. What it draws arrives on `RenderView::splash` as paths - the logo's and
its caption's - and an opacity, not as pixels: the engine core cannot decode
a file, so `SplashSystem` decides which logo is up and how faded, and the
pass - which links the loaders - reads them, uploading once per image rather
than once per frame. The engine's own mark is captioned with its notice
(`assets/logo/vkm_engine_notice.png`), so every game made with it shows it.

IBL is **not** a pass: the persistent `GLIBLBaker` re-bakes inside `render()`
when `environment.sky.hdrPath` changes or, for the procedural sky, when the sun
angles or a sky parameter change - a step a frame while the sun only drifts, more
while it runs ahead ([lighting.md](lighting.md#image-based-lighting-ibl)) - producing the irradiance
and prefilter products the forward pass samples. The BRDF/DFG LUT beside them
depends on no environment, so the backend integrates it once at `init` and every
bake leaves it alone. A scene that names no sky drops the baked one. A
reflection probe is baked at frame end when it is new or bumped, or once a
moved or resized one has held still for `CAPTURE_SETTLE_FRAMES` frames - a probe
dragged across the scene bakes where it is put down, not on every frame it
crosses, and one that never holds still, riding a moving platform, keeps its last
bake as its box follows it - and the baked ones are bound per frame into a probe UBO; the SH
irradiance volume re-bakes, before the probes, at once when its bake version
changes, or once a changed box or grid has held still for as long, and a probe
also re-bakes when the volume it was lit by does
([lighting.md](lighting.md#what-a-capture-is-lit-by)). Both follow the lights and the
sky with no bake: when either changes, the volume's surfels are lit again (below), and a
probe captured under other light is refreshed a step a frame - a face of the capture,
then a share of the convolution (`GLCubeConvolver::slices`) - into a layer of the arrays
no probe holds, copied into the probe's after the last step, so no frame samples half a
probe. One probe refreshes at a time: the one that has waited longest for how near the eye
it is (`GLProbeManager::REFRESH_NEAR`), and never one away from where it was captured, which
keeps its last capture as its box follows it. A bake in between ends the refresh, which begins
again in its turn. A frame with no camera bakes neither and keeps both.

**A scene has one irradiance volume**, and which one is `findIrradianceVolume` -
the lowest-slot entity carrying an `IrradianceVolume` and a `Transform`, the
same rule that decides the eye, the key light and the ear. The backend holds a
single probe grid and the forward pass places a fragment in a single box, so a
second volume is not a second source of indirect light; it is one no frame ever
reads, which is why the Inspector's card says so on the ones it did not pick.
`RenderView` therefore carries `irradianceVolume` and `hasIrradianceVolume`
rather than a list, the way `Visibility` carries its camera.

**What an irradiance probe sees is kept as surfaces, and lit apart.** The bake draws
one small cube around each probe - `GLIrradianceBaker::FACE_SIZE` texels a face,
`shaders/irradiance/surfels` - into the probe's tile of an atlas that stays on the
GPU: through every texel, the nearest surface's bounce colour, normal, distance and
emission (`shaders/irradiance/surfel.glsl`), or nothing, the sky. Lighting it is a
compute pass, `shaders/irradiance/relight`, one workgroup a probe: every surfel lit
as a diffuse surface, then every texel integrated against SH-L1, weighted by the
solid angle it covers, so a small bright patch reaches every probe that sees it.
The bake's gathers ([lighting.md](lighting.md#what-a-capture-is-lit-by)) are that pass
run again, and so is a relight when the lights or the sky change: the scene is never
drawn round the probes again for a change of light alone. A surfel reads the
directional lights and those listed for its probe cell - the lights whose reach meets
the cell, listed on the CPU each gather (`GLIrradianceBaker::listLights`) - or, outside
the box, those reaching out of it: never every light, which in a scene of a few hundred
small lamps would be most of a gather.

**A probe inside a wall is refused, not shipped.** The surfel cube is drawn with
culling off, so a probe inside a solid sees the backs of the faces enclosing it; one
seeing backs over more than a quarter of its sphere is marked refused in the alpha of
its first coefficient, since what it sees is the room on the far side - light a
trilinear fetch would blend into the near one. The verdicts depend on where probes
stand, not on the light, so the bake reads the first gather's back once, and
`probeFillWeights` (`system/render/irradiance_dilation.h`) turns them into a fixed
blend of trusted neighbours for each refused probe, spreading one cell per round;
`shaders/irradiance/fill` applies it after every gather of every bake and relight,
with nothing read back. The repair comes before anything samples the grid because
the forward pass samples it with hardware trilinear filtering and so cannot skip a
probe. A volume where *no* probe was trusted is not marked ready at all, so the frame
falls back to the global IBL rather than to a grid of guesses.

**A capture is shadowed by the key light.** The frame's cascades are fitted to
the camera, so a probe capture or an irradiance gather draws a map of its own: the key
light's depth over the region the bake describes - a probe's influence box, the
volume's box - across the light, and along it every caster standing over that
region, since a roof well above a room still shades its floor. Without it the
sun would light indoor floors through their ceilings in the only indirect
diffuse there is. The other lights capture unshadowed. The map is 2048 texels
across the region and read through the ordinary hard kernel, installed as a
one-cascade ShadowBlock the next frame's own upload replaces. Two are kept, each for
the key light and region it was drawn for: a probe capture draws its own, while a
relight reuses the volume's until the key light or the box changes, so a probe being
refreshed and the volume being relit in the same frames do not draw each other's away.

**A bake is frame time.** All three run inside `render()`, so the frame that
notices the change is the frame that pays - but for the procedural sky's IBL
following a drifting sun, spread a step a frame, and the irradiance volume's relight,
spread a gather a frame: an irradiance bake draws six cube faces per probe once and
runs its gathers at once, a relight runs the gathers alone. That is why
`IrradianceVolume::MAX_RESOLUTION` and `MAX_PROBES` exist and why `RenderView` clamps
the grid to them - the grid is a product, so its bake and its atlas are cubic in
numbers a scene file can hold anything in.

### Fog

The froxel volume (`GLFogPass`: `shaders/fog/inject`, then `integrate`) holds,
for each froxel of the camera's frustum out to `FogSettings::maxDistance`
(metres, the far plane if nearer - `GLFogVolume::depth`), the light the medium
scatters toward the eye up to that froxel's far bound, and the transmittance of
the light from behind it. The slices are exponential between the near plane and
that reach, so a reach nearer than the far plane spends them where fog is seen;
a point past it takes the last slice's value - the fog accumulated up to the
reach, and nothing scattered beyond. What it scatters is every clustered light
through the medium, each through its own shadow - the sun's cascades and a
spot's tile on the 3x3 kernel, a point light's cube in one hard compare - so a
lamp does not glow through the wall in front of it - by a phase of the authored
Henyey-Greenstein lobe with a share scattered back (`BACK_SHARE` at `BACK_G` in
`shaders/fog/inject`), so sunlit fog keeps its light with the sun behind the
eye, which a single forward lobe all but empties. And the environment's own
light: the sky's irradiance, or the irradiance volume's where one covers the
froxel (`shaders/ambient.glsl`, as the decals read it but without the reflection
probes: the inject reads it three times a froxel), facing away from the eye
for a forward phase and blended to the mean of up and down as the phase's mean
cosine (the authored lobe with its back share) nears zero, so fog in shade or
indoors is lit as the walls there are rather than black. No pass applies it to
the finished frame. Everything drawn through the medium fogs itself in its own
shader, at its own depth, as it is drawn - the skybox at the far plane, every
surface of the forward pass, the particles, the decals - through
`shaders/fog.glsl`, given the volume by `GLPass::bindFog`. A pass over the
finished frame could fog a pixel only by the opaque surface behind it: a muzzle
flash a metre away in thick fog would vanish, and a near window would be fogged
as if it were the street behind it. Across the screen the volume is read cubic
B-spline filtered (`shaders/bspline.glsl`, four trilinear reads), through its
slices trilinear, so a lamp's glow a few froxels wide shows no froxel's square.
The B-spline smooths rather than passes through its texels, so a glow one froxel
wide comes out broader and dimmer at its peak ([engine.md](../guides/engine.md#4-what-has-already-been-decided)).

**The sky's air** lies in front of everything too, out to kilometres: under the
procedural sky the Atmosphere pass's aerial-perspective volume holds, for each
froxel of the view, the light the atmosphere scatters toward the eye and its
mean transmittance ([lighting.md](lighting.md#image-based-lighting-ibl)).
`shaders/fog.glsl` takes both at once, at the same depth, the air beneath the
fog - `(colour * T_air + S_air) * T_fog + S_fog`, as one `S` and `T` (`fogAt`) -
and `GLPass::bindFog` gives the air with the fog to every program but the
skybox's, which takes the fog alone: the sky holds its own air. So what follows
of how a colour takes the fog holds for the air as well.

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
and the editor preview take neither - `u_hasFog` and `u_hasAir` read 0 in their
programs - because each volume describes this camera's frustum, not a probe's.
The Grid and UI draw after everything and are overlays, so they are not fogged
either.

### The ocean

Wind-driven waves for a scene's sea, made as Unreal's water system makes them:
a sum of Gerstner waves. The scene's `Ocean` (`findOcean`) is a spectrum, not
a list: `OceanSurface::at` draws `waveCount` waves from JONSWAP - shortened by
TMA's correction where `depth` is set (Horvath 2015) - one to each frequency
band, the bands even in log between `maxWavelength` and `minWavelength`, each
wave as tall as its band's energy and turned off the wind by a cos^2s spread.
Each crest draws the water `choppiness` amplitudes toward it, held where the
slopes add past one, so the surface never folds over.

A surface whose material is `waves` rides them - in a scene with an ocean,
windless or not; without one it is an ordinary surface, and casts as one. The
depth prepass and the forward pass move each vertex by `shaders/ocean.glsl`'s
sum, both, so depth matches under GEQUAL, and hand the fragment the surface's
tangents there. The spectrum on below `minWavelength`, down to `shortestRipple`
(`OceanRipples`), is too short for any vertex: the Ocean pass draws it once a
frame into three repeating fields of 512 texels square, the band split evenly
between them in log, each repeating every 7.93 of its longest ripples
(`GLOcean`, `GLOceanFields`). As an FFT ocean's cascades are (Tessendorf 2001),
each field is a grid of wavenumbers, every one in its band a ripple - a Gaussian
of `OceanRipples::density` there, rolling a whole number of turns in
`OceanRipples::LOOP` so float phases stay exact - transformed to the surface in
shared memory, along X a row a group (`shaders/ocean/ripples`), then along Z a
column a group (`shaders/ocean/ripples_columns`): thousands of ripples a field,
for the cost engine.md's ocean row gives. A texel holds the slopes, the squeeze
and the slope squared, so the levels a far pixel reads keep the variance it
spans (LEAN mapping, Olano and Baker 2010) and the shading takes it as roughness
- a far sea grows rough, not noisy. Six anisotropic reads a pixel, the longest
field's four B-spline filtered: the light on the water is as fine as the screen.

The water lights as water: the material's `subsurfaceColor` times `subsurface`
is what it scatters back (after Atlas's ocean, GDC 2019) - through a backlit
crest, in proportion to its height in the sea's significant heights, toward the
viewer, on the lit side, and from the sky. Where the crests squeeze the surface
- its Jacobian below one - it whitens: `foam` says how readily, against a
squeeze three standard deviations of the sum's, widened by how fast it changes
across the pixel so it never steps, and foam takes `foamColor` and a rough
finish. A crest's foam lingers: a Gerstner wave carries its water back to its
rest point, so each vertex looks back a few seconds at how squeezed its water
was (`oceanLingering`) and the foam fades from there, trailing the crest. The
long and middle ripples' squeeze tears its edge into patches, and it thins from
dense to lace as it fades. Where anything stands in the water it foams too,
`waterlineFoam` metres out, read off the ocean's shore: a square of sea
`Ocean::WATERLINE_SQUARE` (128 metres) on an edge ahead of the eye
(`GLOceanFields::captureShore`). The Ocean pass draws everything but the water
looking straight down over it twice, against the water's height off a coarse
grid of `oceanHeightAt` (the inversion `OceanSurface::heightAt` makes). Under
the water it keeps the topmost thing a texel: a face turned up is something
under the surface, a face turned away the inside of something breaking it. Over
the water it keeps the lowest: a face turned up is the top of something whose
inside the surface is in. Either alone misses a post - from above its walls draw
nothing - the one when a seabed hides its foot, the other when a roof hides its
top. A distance transform, a row and then a column at a time (Felzenszwalb and
Huttenlocher), turns that into metres to the nearest thing breaking the water,
its depth under counted thrice as distance. Drawn in the world, it rings what
the camera cannot see as well as what it can, and costs the same however much
sea is in view. The middle and short ripples tear the foam into suds, frayed
half a width past its edge and no further, and a passing crest widens it, though
never past twice the width (at most 8 metres) the transform looks round a point.
The ripples and the shore take the units of the env cube and GTAO's depth chain,
both done with by the forward pass. The waves fade out between `calmRadius` and
`stillRadius` from the ocean's Transform.

Gameplay can raise up to four surges (`Ocean::surges`, `OceanSurge`): straight
ridges of water a cosine across the way each rolls, their ends tapered, which only
lift the water and are not faded by `calmRadius`. They are session state, set as the
game runs and never saved; the same sum moves the drawn surface (`oceanSurgeAt`) and
answers gameplay, and each crest foams (`oceanSurgeFoam`). A game that floats things
on the wind's sea alone, under a surge passing over them, asks `withoutSurges()`.

`OceanSystem` builds the scene's sea once a tick and once a frame, at
`NetSession::worldTime` - inside a tick on the authority that tick's own, in the
frame the moment drawn, and on a client the server's tick it draws others at -
and publishes it as `ctx.ocean` for the cull, the renderer and gameplay alike, so
the sea stops with the game and every end of a networked game draws the one the
server floats things on. A behavior asks `ocean()`: its `surface` answers
`displacement` for the water resting at a point, `heightAt`, `normalAt` and
`sampleAt` for a point fixed in the world, found by four fixed-point steps back
to the water over it, above `level`. The phases are wrapped in double before
`GLOcean` uploads them (`OceanBlock`, once a frame), so the water stays smooth
for days. The ripples carry nothing afloat.

A `waves` surface under an ocean casts no shadow, as a held shadow tile would
picture one moment of it; the cull grows its box by as far as the sea carries
water (`OceanSurface::farthestReach`); the offline captures and the material
preview have no sea, so draw it as an ordinary surface; a skinned mesh is shaded
as the water but not moved; and waterlines past the shore's square fade out, the
shore taking the water to rest at the ocean's Transform's height. Its mesh wants
vertices closer than half of `minWavelength` where the waves are whole.

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

Five things about it are load-bearing and easy to get wrong:

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
- **`additive` also says what the colour is.** An additive particle emits -
  fire, sparks, a muzzle flash - so its colour is the radiance it adds. An
  alpha-blended one scatters the light reaching it - smoke, dust, steam - so its
  colour is an albedo, lit at its centre in the vertex stage: by each light of
  its cluster as a diffuse card facing that light, an area light as the small
  source it is from a distance, through the light's shadow - the hard lookups,
  for a point with no surface (`pointShadow`) - and by the environment's mean
  radiance, the irradiance averaged over the six axes, since a puff has no
  normal. Smoke in a building's shadow is dark, and smoke beside a lamp - a
  fire's `Light`, not its additive flames, which light nothing - takes its light.
  At its centre, not its corners: a corner lies off the puff, past a wall or in a
  lamp's reach the puff is not, and turns with the view and the puff's spin.

A one-off puff - a spark shower, an impact - is an emitter with `emitting` off,
a `burst` and `destroyWhenSpent`: the next update spawns the burst whole, as far
as `maxParticles` allows, and clears it, and once the last of them has died the
system destroys the entity and everything under it. A game fires one and forgets
it; one set up idle, to be fired by gameplay later, waits for its burst - an
emitter is spent only once it has fired. `halfExtents` gives an emitter a box,
world axes about it, to spawn anywhere in rather than at a point, so a body sheds
from all over itself without its emitter being moved.

Everything else is authored per emitter: `rate`, `lifetime`, `maxParticles`, the
initial `velocity` plus a per-axis random `spread` (drawn from a generator the
system owns and reseeds with each world, so a world loaded again draws the same
sequence), a constant `acceleration`,
and the colour/size ramp that `RenderView::build` evaluates at each particle's
age, so the pass draws each billboard as it is handed. `softness`
is the billboard's edge falloff - 1 is a soft blob, 0 a hard-edged disc.

An emitter with a `texture` draws it instead of the disc, tinted by the
particle's colour. `sheetColumns` by `sheetRows` read it as a sheet of frames,
left to right from the top, played once over each particle's life or looping at
`sheetFps` (`sheetFrameAt`); `randomFrame` starts each on a frame of its own.
`RenderView::build` hands each particle its frame's rect, and the shader keeps
its reads half a texel of the coarser level it reads inside it, so a filtered
read takes nothing of the frame beside - down to the mip level whose texels
straddle two frames, which holds both already, so a sheet whose frames are a
power of two across keeps them apart furthest. `stretch` streaks a particle
along its velocity: its quad runs from its head back by that many seconds of its
travel, capped at both ends by half its width, along the trail as the view sees
it - a spark coming at the eye is a dot. One that does not streak turns by
`spin` radians a second, from an angle of its own with `randomRotation`. The
random draws are made only for an emitter that asks for them, so one without
draws the same sequence whatever these fields hold elsewhere. `GLParticlePass`
draws a run of particles showing one texture at a time: the additive half
grouped by texture, the alpha half in its far-to-near order, broken where the
texture changes - so two overlapping alpha emitters of different textures cost a
draw for each change in that order, up to one a particle. `viewmodel` draws an
emitter as a Viewmodel mesh is drawn ([The viewmodel](#the-viewmodel)).

### The viewmodel

A first-person game's arms and weapon are `Mesh::seenBy = Viewmodel`: the eye's
alone, like `EyeOnly`, and drawn through a projection of their own - the
camera's `viewmodelFovY`, or its `fovY` at 0, with a near plane of
`Camera::VIEWMODEL_Z_NEAR` - so they keep their look whatever the world's field
of view, and a weapon held close is not cut by the world's near plane. They
are lit and shadowed where they are; only the clip position differs.

The cull flags each as `ObjectDraw::viewmodel`; `GLObjectBuffer` uploads a word
of flags an object beside its matrix (`INSTANCE_FLAGS`), and the prepass and
forward vertex shaders project through `eyeClip` (`shaders/viewmodel.glsl`),
which for a flagged object takes `CameraBlock::viewmodelViewProjection` and
squashes its depth into the nearest `VIEWMODEL_DEPTH_SHARE` of the range
(`gl_object_buffer.h`, which the shader prelude carries). Only what lies within
about that share of the near plane beyond it can then draw over it, and the near
plane sits inside the character's capsule, so a weapon pushed into a wall stays
in front of it. The same formula in both passes keeps their depths equal under
`GEQUAL`.

What reads the depth afterwards reads the squashed one, as about the near
plane, and each reader is answered:

- **Fog** is the near plane's, which is none worth counting.
- **A decal** box would have to reach within that near band to land on it, and
  **a particle** behind it fails the depth test, as behind any surface.
- **GTAO** would take it for an occluder a few centimetres from the eye and
  darken the wall it is held against round its outline, so its depth prefilter
  reads the band as sky: the weapon takes no AO of its own and gives the wall
  none.
- **Depth of field** holds the band sharp rather than blurring it as if at the
  lens.
- **The reflection trace** skips the band: a ray from the near plane would start
  in the wrong place, so a weapon keeps the probe or sky reflection the forward
  pass gave it.
- **Screen-space shadows** read the band as far: a weapon neither casts on the
  world nor takes the world's.
- **The light clusters**: the forward fragment finds a viewmodel pixel's cluster
  from its world position through the world's projection, not from where its
  own projection drew it, so a lamp beside the weapon lights it.

GTAO, depth of field, the reflection trace and the screen-space shadows find the
band by `inViewmodelBand` (`shaders/camera.glsl`). An orthographic camera has none:
depth there is linear, so the band would be the nearest `VIEWMODEL_DEPTH_SHARE` of
the whole scene, and everything in it would lose its occlusion, its depth of field and
its reflections. So `intoViewmodelBand`, which both `eyeClip` and a viewmodel
particle squash through, leaves a viewmodel at its own depth there, as the readers
take it.

Nothing else draws a viewmodel mesh: it casts no shadow and no capture takes it.
An emitter with `viewmodel` set - a muzzle flash on the barrel - is projected
the same way, so it lines up with the weapon and shows in front of it, and
skips the soft fades, which its squashed depth and the weapon's would take to
nothing. A decal does not land on a weapon.

### GLView - GPU sync

Holds four tables keyed by handle id - mesh, material, texture, font atlas - each
slot remembering the version and handle generation it was uploaded at.
`sync(view, resources)` walks every list on `RenderView` that names a handle -
the objects the camera sees (mesh, material and the material's textures), the
scene-wide objects (mesh, and for the shadow casters among them the material
and its textures too, since a cutout casts through its map), decals (material
and its textures), the particles' textures and the UI draw commands (font atlas,
and each image's texture) - and
`ensure()`s each asset, uploading only when the version moved on or the
generation says the slot was recycled (see [resources.md](resources.md) for
the version mechanism). Each sync bumps a stamp, and a slot remembers the stamp
it was last checked under, so an asset a thousand objects share is looked up
once a sync and every later encounter is one compare. Skin data needs nothing
here: it rides on `MeshAsset` through the same version gate, and the palette is a
per-frame array carrying no handles at all.

That list is the whole rule, and it has to be, because three of the four are
gathered scene-wide rather than from the visible set: an off-screen occluder's
mesh and material and a decal's own material need never appear among what the
camera sees, and every pass answers a GPU object it cannot resolve by silently
skipping the draw. A scene load or play-stop restore swaps the whole asset graph
and restarts its handles and versions, which no per-asset gate can see, so the
backend calls `invalidate()` and the next sync repopulates. The per-frame UBOs
and the shadow / IBL sets are not here - GLBackend owns those. All materials
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
target's colour attachment of that number), the compute work-group sizes a
`local_size` and a dispatch count must agree on (`GROUP_*`), and the light and
material types the shaders switch on (`LIGHT_*`, `MAT_*`, written from
`LightType` and `MaterialType`). The image units a compute pass binds for
itself - the fog and SH-projection volumes, the sky-view table and the
aerial-perspective volume, and the level of a mip chain being written - are the
pass's own and stay literal. Vertex attributes (`ATTR_*`, from
`GLBindings::VertexAttributes`) are per-vertex position/normal/uv/tangent (slots
0-3) plus one per-instance uint at slot 4 (binding 4, divisor 1) naming the
instance's object - its index into the storage buffers every object's model
(binding 10) and first bone (binding 6) went up in, once for the frame. Every
instanced draw, in every pass, takes that shape: a batch, a shadow tile and a
capture differ only in the list of objects they hand it. On a skinned mesh only,
bone indices and weights at slots 8/9 in a **second stream at divisor 0**,
parallel to the vertices.

**Every mesh lives in one pool** (`GLMeshPool`, owned by `GLView`): one index
buffer, and one vertex array per layout - static, or skinned with its second
stream - over buffers every mesh of that layout shares, each `GLMesh` a range in
them. That is what lets a run of different meshes go out as one
`glMultiDrawElementsIndirect` (core in 4.3): a `GLDrawList` holds the object
indices and a command per run - the mesh's `firstIndex` and `baseVertex`, and a
`baseInstance` that slices the object list through the divisor-1 attribute,
since 4.5 core has no `gl_BaseInstance`. A pass binds program and material state
between multi-draws and nothing between the commands of one, which is the point
on a driver whose cost is the validation after each state change. A full stream
grows by doubling and copies on the GPU; a mesh's range is offsets, so nothing
that names it moves. `Vertex` stays 48 bytes: see [animation.md](animation.md)
for why the skin rides beside it rather than in it. Storage binding 5 carries
the frame's bone palettes, and binding 0 the lights, a storage buffer because
the list outgrows a uniform block. UBO binding points cover the Material,
Camera, Shadow and Probe blocks. The Camera block (`shaders/camera.glsl`,
`CameraUBO` in `gl_camera.h`) carries every fact about the eye a pass reads -
view, projection and their inverses, the position, the viewport size, near and
far, and what a viewmodel draws through - so no pass sets one as a uniform of
its own; the scene capture and the editor preview fill their own. Texture slots
cover the PBR material maps plus
the shadow depth (one tiled 2D atlas, then one cube per point-light slot), IBL
set (irradiance / prefilter / BRDF LUT / env cube), the GTAO factor, the scene
colour/depth/G-buffer samplers, the froxel fog volume, the SH irradiance volume,
and the atmosphere's tables (`BAKE_SLOT_TRANSMITTANCE`,
`BAKE_SLOT_MULTISCATTERING`, `SKY_SLOT_VIEW`, `POST_SLOT_AERIAL_PERSPECTIVE`).
