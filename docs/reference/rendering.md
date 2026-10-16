# Rendering System

The renderer has two halves with one seam between them. The engine half builds a
backend-agnostic `RenderView` each frame; the backend half syncs its GPU
resources to that view and runs a **fixed, ordered list of passes**. There is no
render graph, and passes (`GLPass`) are the backend's own - the engine never
sees one. Lighting and shadows are [lighting.md](lighting.md); what is drawn
is decided by [visibility.md](visibility.md).

The renderer has no TAA ([engine.md](../guides/engine.md#4-what-has-already-been-decided));
FXAA, motion blur and lens flare are not built.

## Per-frame flow

```
RenderSystem::update(FrameContext)
  |-- RenderView::build(...)      engine side: borrow the Visibility product,
  |                               gather lights / probes / decals / particles
  |-- backend.render(view, resources)              // GLBackend
        |-- onWorldReplaced   drop every cache of a replaced world or asset graph
        |-- GLView::sync      upload changed GPU resources
        |-- air tables / bake IBL, or a drifting sun's step a frame
        |-- shadow plan       atlas slots; caster cull forked onto the thread pool
        |-- per-frame UBOs, partitionDrawables, skin palette, object buffer, opaque batch
        |-- run the passes in order
        |-- measure the exposure (autoExposure)
        |-- irradiance volume: bake, or relight a gather a frame
        |-- probes: bake, or refresh a step a frame
```

`GLBackend::render` (`src/backend/opengl/gl_backend.cpp`) is the authority for
this order.

## RenderView - the contract

The entire engine-to-backend interface besides the asset graph (`ResourceManager`,
which resolves the handles it names); a backend consumes exactly this.
`RenderSystem::update` sets the viewport, surface, settings and frame time, and
`build()` refills the rest each frame, reusing the vectors' capacity.

| Field | Type | Notes |
|-------|------|-------|
| `viewportX` / `viewportY` / `viewportWidth` / `viewportHeight` | `uint32_t` | Scene render rect |
| `surfaceWidth` / `surfaceHeight` | `uint32_t` | The backbuffer the viewport sits within |
| `frameSeconds` | `float` | Wall-clock seconds since the last frame, for what adapts over time |
| `camera` | `CameraData` | The view `Visibility` rendered through - the scene's camera or a host's ([visibility.md](visibility.md#where-the-view-comes-from)) |
| `hasCamera` | `bool` | Without one the scene's lists stay empty, so a backend keeps what it baked |
| `objects` | `const RenderObjects*` | Borrowed from `Visibility`. `visible` is what the camera sees, unsorted; `scene` is every drawn mesh, casters first (`casterCount`). Passes draw indices into it |
| `skinMatrices` | `const vector<mat4>*` | The frame's bone palettes; each object carries its range (`skinFirst`, `ObjectDraw::skinCount`). Null when nothing is posed |
| `lights` | `vector<LightData>` | Enabled lights, world space, each with its entity slot |
| `probes` | `vector<ProbeData>` | Reflection probes |
| `planarReflectors` | `vector<PlanarReflectorData>` | Enabled planar reflectors, world space; the backend picks which it draws |
| `decals` | `vector<DecalData>` | Scene-wide, not camera-culled |
| `particlesAdditive` / `particlesAlpha` / `particlesDistortion` | `vector<ParticleData>` | Billboards and streaks split by blend, unsorted |
| `stripPoints` | `vector<StripPoint>` | Every ribbon's and trail's points, each strip's run in turn |
| `stripsAdditive` / `stripsAlpha` / `stripsDistortion` | `vector<ParticleStrip>` | Each strip's run of `stripPoints`, split by blend |
| `particleTextures` | `vector<TextureHandle>` | Each particle texture once; a billboard names its own by `params.z` and its normal map by `uvNext.w`, a strip by `ParticleStrip::texture` (0 is none: the soft shape, or no normal map) |
| `ocean` | `OceanData` | The sea at the frame's moment - [The ocean](#the-ocean) |
| `irradianceVolume` / `hasIrradianceVolume` | `IrradianceVolumeData` + `bool` | The scene's one baked-GI volume (`findIrradianceVolume`) |
| `settings` | `RenderSettings` | Copied each frame |
| `environment` | `Environment` | Sky, night sky and fog, copied whole |
| `ui` | `const UIDrawData*` | The UISystem's draw list; drawn even with no camera |
| `splash` | `SplashFrame` | The startup logo, or nothing once it is over |
| `worldEpoch` | `uint64_t` | `Scene::epoch()`. A replaced world reuses slots, so a cache of a *place* (a baked probe or volume) cannot tell on its own; `GLBackend::onWorldReplaced` is the one place that checks |

The engine does **not** sort what it draws; the backend does it all.
`partitionDrawables` splits opaque, alpha-mask and transparent;
`GLInstanceBatcher` groups by (skinned, mirrored, material, mesh); the forward
pass draws the depth-writing buckets, then transparents back to front over a
snapshot of the opaques for refraction.

## RenderSettings and RenderMode

`RenderSettings` (`render_settings.h`) is plain data on `FrameContext::render`,
edited by the editor's Render Settings panel or a game's settings screen and
copied into the view each frame. It is the project's: everything but
`renderMode` and the grid axes ships in `project.json`
(`visitShippedRenderFields`). Scene-look settings - sky, night, fog, IBL
intensity - live on the scene's `Environment` instead. Each field's meaning is on
the field; the ones that need more:

- **`exposure`** is in stops: the composite scales the frame by 2^exposure
  before the `tonemap`. **`autoExposure`** adds stops adapted to the frame's
  brightness: the backend counts the lit scene into a histogram of log2
  luminance and reads it back a few frames late without waiting;
  `system/render/exposure.h` turns that into stops.
- **Texture filtering** is offered, not imposed: a texture that states
  `Nearest` keeps it ([resources.md](resources.md#textureasset)).
- **`occlusionCulling`**: [visibility.md](visibility.md#occlusion-culling-on-the-gpu-as-an-option).
- **`renderMode`** picks the composite's output: the final image or a debug
  view (the `VKM_RENDER_MODES` list). The shaders see the enums as `MODE_*` and
  `TONEMAP_*` constants in the prelude.

**The presets.** A settings screen offering one quality choice renders with
`withPreset(projectRender(), GraphicsPreset::...)`: the project's settings with
the frame-time fields (MSAA, shadow resolution, anisotropy, render scale,
half-res SSR) taken from the tier, and GTAO, screen shadows, SSR, bloom and soft
shadows on only where both the project and the tier have them. The look stays
the project's. Build a tier over `projectRender()`, never over `render()`: a
tier built over another keeps what that one turned off.

**Render scale.** `renderScale` sizes every scene target
(`RenderView::sceneWidth`/`sceneHeight`); the composite stretches the frame over
the viewport, and the UI, splash and grid draw after it at full resolution. A
settings screen offering resolutions lists `WindowManager::renderResolutions` and
converts one with `RenderSettings::renderScaleFor`.

## RenderBackend - the seam

The engine sees only `RenderBackend` (`render_backend.h`) and never includes a
`gl_*` header: `init`, `render(view, resources)`, `reloadChangedShaders`,
`maxAnisotropy`, `holdsPixels` and `readFrame`, which reads back the viewport rect `render` just
drew (a screenshot is the game's view without the editor's panels). The editor's
offscreen renders go through a second seam, `EditorRenderHooks`
(`editorHooks()`), and the window reaches the API through a third,
`WindowSurface`. One backend exists, OpenGL
([engine.md](../guides/engine.md#4-what-has-already-been-decided)).

## OpenGL backend

`GLBackend` owns the GL context, the `GLView` (GPU resources, the mesh pool and
the frame's streaming memory), the render targets, and the persistent stores
and bakers: shadow atlas, IBL, atmosphere, cluster grid, fog volume, irradiance
volume, reflection probes and the exposure histogram. `GLPreview` is a separate minimal forward +
composite path for editor thumbnails; it does not run the pass list. A target
nothing but its pass reads is that pass's own.

Per-frame lists go through `GLView::stream()` (a `Vkm::GL::StreamRing`), and what
every pass reads is copied once into video memory. Those copies and the asset sync
follow `RenderObjects::changed`
([visibility.md](visibility.md#what-it-produces)); the shadow casters' keys are
kept only while nothing changed, and any change works them all out again.

**Targets and the colour chain.** `m_sceneHDR` is the geometry target (colour,
depth, G-buffer); `m_sceneMS` its multisample twin while MSAA is on. Post passes
never blit back into it: a pass samples `colorSrc`, writes `colorDst` and calls
`flipColor()`, over the `m_postA`/`m_postB` scratches. Depth and the G-buffer
are always sampled from the geometry target.

**MSAA.** The geometry passes draw into `m_sceneMS` and `GLResolvePass` resolves
it; the post chain is single-sample. Depth and the G-buffer take sample 0 (an
average of two surfaces' depths is neither). Colour and the reflection inputs
are averaged tonemap-weighted, so one bright sample does not outweigh its pixel.

**Depth is reversed.** Clip z runs 0..w (`glClipControl`); the eye's depth is 1
at the near plane and 0 at the far, in a 32-bit float buffer, tested with
`GL_GREATER`. The engine's projections stay glm's: `GLCamera` remaps the camera
block's matrices, and C++ goes through `GLDepth` (`convention/gl_depth.h`). The
prelude carries its planes as `NEAR_DEPTH` and `FAR_DEPTH`, and every shader reads
depth through `shaders/depth.glsl`'s helpers (`isFarDepth`, `nearerDepth`, ...) -
never compare depth to 0 or 1 by hand. Shadow maps keep
near 0, far 1 (`GLDepth::shadowProjection`).

### The passes (fixed order)

The `m_passes` list in `gl_backend.cpp`, run top to bottom. A new pass goes
after everything it reads.

| # | Pass | Does |
|---|------|------|
| 1 | Shadow | Sun cascades, spot tiles and point cubes into the atlas; a tile or cube face whose contents did not change is kept ([lighting.md](lighting.md#shadow-atlas)) |
| 2 | DepthPrepass | Clears the scene target; early-Z for opaques and the G-buffer (oct view-normal). Runs `GLOcclusionCull` when that is on |
| 3 | ResolveDepth | MSAA only: depth and G-buffer, sample 0 |
| 4 | GTAO | Ground-truth AO and bent normal into `m_ao`, spatially denoised, no history |
| 5 | ScreenShadows | Compute: the sun's screen-space shadows into the AO target's alpha ([lighting.md](lighting.md#screen-space-shadows)) |
| 6 | ClusterCull | Compute: lights into the Forward+ cluster grid |
| 7 | FogCompute | Compute: the froxel fog volume, before anything lit reads it ([Fog](#fog)) |
| 8 | Atmosphere | Compute, procedural sky only: sky-view table and aerial perspective ([lighting.md](lighting.md#image-based-lighting-ibl)) |
| 9 | PlanarReflections | Each seen `PlanarReflector`, at most `GLPlanarReflectionPass::MAX_DRAWN`: the opaque scene from the mirrored camera, clipped at its plane, with every light, the shadows, probes and sky, filtered into a chain ([Planar reflections](#planar-reflections)) |
| 10 | Skybox | The background, before geometry so transparents blend over it; with no sky, the fog over a black background |
| 11 | Ocean | The sea's ripple fields and its shore capture ([The ocean](#the-ocean)) |
| 12 | Forward | The PBR ubershader: opaque, alpha-mask (alpha-to-coverage under MSAA), then transparents. Writes the reflection inputs while `ssr` is on or a planar reflector is |
| 13 | Particles | Billboards and strips, depth-tested, never depth-writing, soft against scene depth: additive, alpha, then distortion over a copy of the colour ([Particles](#particles)) |
| 14 | ResolveColor | MSAA only: colour and reflection inputs, tonemap-weighted, and depth again now that alpha-masked geometry is in it |
| 15 | Reflections | Replaces the probe / sky reflection with a planar reflector's picture on its rectangle, else where a screen-space trace hits (chain) |
| 16 | Decals | Projected boxes into the colour chain, lit diffusely as the surface under them. After the reflections, so a glossy floor's reflection does not cover them |
| 17 | DoF | Disk blur from the camera's focus distance (chain) |
| 18 | Bloom | Compute: bright-pass and mip down/upsample; the composite adds it |
| 19 | Composite | Bloom, exposure, tonemap and sRGB encode to the backbuffer viewport, or a debug buffer per `renderMode` |
| 20 | Grid | The editor's grid and axes, after the tonemap so the axes keep `Math::AXIS_COLORS` |
| 21 | UI | The screen-space UI ([ui.md](ui.md)) |
| 22 | Splash | The startup logo over black, covering the frame. Gets paths and an opacity from `SplashSystem`, since the engine core cannot decode a file |

**The forward programs.** One fragment file, `shaders/forward/pbr`, built four
ways: `pbr` and `pbr_skinned` (differing only in the vertex stage), and a plain
variant of each compiled without the optional features, for a material that
uses none (`GLMaterial::plain` names them). Within a program, features are
runtime switches on the material's values. Helpers the passes share are one
include each under `shaders/` (`area_light.glsl`, `basis.glsl`,
`parallax.glsl`, `ocean_water.glsl`, `objects.glsl`, ...) - reuse them rather
than copy a function into a shader.

### Bakes

IBL, reflection probes and the irradiance volume are not passes: they run inside
`render()`, so the frame that notices a change pays for it; a change of light
alone is spread over frames ([lighting.md](lighting.md#what-a-capture-is-lit-by)).
A frame with no camera bakes no probe or volume and keeps what it has.

- **IBL** (`GLIBLBaker`): [lighting.md](lighting.md#image-based-lighting-ibl).
- **A reflection probe** is baked at frame end when new or bumped, or once a
  moved or resized one has held still for `CAPTURE_SETTLE_FRAMES`.
- **The irradiance volume** re-bakes, before the probes, when its bake version
  changes or a changed box has held still as long. A scene has one:
  `findIrradianceVolume` picks the lowest-slot entity with an `IrradianceVolume`
  and a `Transform`, and the Inspector says so on any other.
  `IrradianceVolume::MAX_RESOLUTION` and `MAX_PROBES` cap the grid, because the
  bake and its atlas are cubic in it.

Both captures follow the lights and the sky without a new bake
([lighting.md](lighting.md#what-a-capture-is-lit-by)).

**What an irradiance probe sees is kept as surfaces, lit apart.** The bake draws
a small cube around each probe (`shaders/irradiance/surfels`) into an atlas kept
on the GPU: per texel, the nearest surface's bounce colour, normal, distance and
emission, or nothing for sky. `shaders/irradiance/relight` lights those surfels
and projects them to SH-L1, one workgroup a probe; each gather of the bake, and a
relight when the light changes, is that pass run again - the scene is never
redrawn for a change of light. A surfel reads the directional lights and those
listed for its cell (`GLIrradianceBaker::listLights`).

**A probe inside a wall is refused.** One seeing backfaces over more of its sphere
than `BACKFACE_LIMIT` (`shaders/irradiance/relight`) is replaced by a fixed blend of trusted neighbours
(`probeFillWeights`, applied by `shaders/irradiance/fill`), since the forward
pass samples the grid trilinearly and cannot skip one. A volume with no trusted
probe is not marked ready, and the frame falls back to the global IBL.

**A capture is shadowed by the sun the frame shadows**
(`lowestSlotDirectionalCaster`, [lighting.md](lighting.md#lights)). The
frame's cascades fit the camera, so a capture draws the sun a map of its own over
the region it describes (`GLCaptureShadow`); without it the sun would light
indoor floors through their ceilings. A relight reuses the map kept for the same
sun and region, so lights that move do not redraw the casters. Other lights
capture unshadowed.

### Fog

`GLFogPass` fills a froxel volume over the camera's frustum out to
`FogSettings::maxDistance`: per froxel, the light scattered toward the eye and
the transmittance behind it. It scatters every clustered light through its own
shadow, and the environment's light (the sky and the irradiance volume), so fog
in shade is lit as the walls there are.

No pass fogs the finished frame. Everything drawn through the medium fogs
itself at its own depth as it is drawn, through `shaders/fog.glsl` (bound by
`GLPass::bindFog`) - a pass over the frame could only fog by the opaque surface
behind, and a muzzle flash in thick fog would vanish. A new program that draws
into the scene must take the fog too. Under the procedural sky, `fogAt` also
applies the aerial perspective beneath the fog. How each blend mode takes it is
in the header comment of `shaders/fog.glsl`.

Unfogged: the shading-split debug views, the Grid and UI, and the offline
captures and editor preview (`u_hasFog` and `u_hasAir` read 0), since each
volume describes this camera's frustum, not a probe's.

### The ocean

A scene's sea (`Ocean`, `findOcean`) is a wave spectrum: `OceanSurface::at` draws
`waveCount` Gerstner waves from it, held so the surface never folds over. A mesh
whose material is `waves` rides them in a scene with an ocean, and is an ordinary
surface without one. The depth prepass and the forward pass both move its
vertices by `shaders/ocean.glsl`, bit for bit, so depth matches under `GL_GEQUAL`.
The spectrum too short for any vertex is drawn by the Ocean pass into three
repeating ripple fields (`GLOceanFields`) that each pixel reads for its
normal, roughness and foam. Foam also gathers where anything breaks the water,
read off a shore distance field captured ahead of the eye
(`GLOceanFields::captureShore`). Each field is described on `Ocean` and
`MaterialAsset`.

`OceanSystem` builds the sea once a tick and once a frame at
`NetSession::worldTime` and publishes it as `ctx.ocean`, so the cull, the
renderer and gameplay agree, and every end of a networked game sees the sea the
server floats things on. A behavior asks `ocean()`; its `surface` answers
`heightAt`, `normalAt` and `sampleAt` at a world point, `displacement` for the
water resting at one. The ripples carry nothing afloat. Gameplay can raise up to
four `OceanSurge`s (`Ocean::surges`): session state, never saved; a game that
floats things on the wind's sea alone asks `withoutSurges()`.

Limits: a `waves` surface casts no shadow (a held tile would freeze one moment);
the cull grows its box by `OceanSurface::farthestReach`; the captures and the
material preview draw it as an ordinary surface; a skinned mesh is shaded as
water but not moved. Its mesh wants vertices closer than half `minWavelength`.

### Particles

`ParticleSystem` steps every `ParticleEmitter` on the CPU into pools of its own,
published as `FrameContext::particles`; `RenderView::build` evaluates each
particle's size curve and colour gradient at its age and flattens them into the
lists the pass reads. The fields are documented in
`ecs/component/render/particle_emitter.h`. What is easy to get wrong:

- **Particles are world-space and never re-based.** A particle starts on the
  emitter's shape, in its resolved world frame, and moves on its own; moving the
  emitter afterwards leaves its particles where they are.
- **`blend` splits the draw and says what the colour is.** An additive particle
  emits (fire, sparks): unsorted, its colour the radiance it adds, past 1 a
  bloom, and it lights nothing - a fire's light is a `Light`. An alpha one
  scatters (smoke, dust): its colour an albedo lit by its cluster's lights
  through their shadows and by the environment, plus the share it gives off; a
  card is lit as a ball, a strip as a card. A distortion one (heat haze) adds no
  colour: it bends a copy of the frame drawn so far - the additive and alpha
  particles, but no other haze - and leaves the reflection under it be. Alpha
  and distortion draw back to front, their billboards and then their strips,
  each sorted among themselves: a strip and a card of one blend do not
  interleave.
- **A ribbon or trail is a strip, not a card.** A ribbon runs through an
  emitter's particles newest first; a trail runs behind each particle through
  where it was. Each is widened square to the view along its length.
- **Collision is a ray a step.** A colliding particle asks the physics world's
  bodies along its step's travel, so a thin body is not passed through, and the
  emitter's own entity is never struck.
- **A sub-emitter is a child.** A child entity with a `ParticleEmitter` and a
  `ParticleTrigger` spawns where its parent's particles are born, die or strike,
  and never by its own rate or burst; no entity reference is saved.
- **A flipbook** is a texture read as a sheet of frames. `sheetBlend` fades
  each frame into the next, so a sheet of a few dozen frames plays slowly
  without stepping. An alpha emitter's `normalMap` is a second sheet, frame for
  frame, that lights each puff by its own folds instead of as a ball.
- **A one-off puff** is an emitter with `emitting` off, a `burst` and
  `destroyWhenSpent`: the burst spawns whole, and the entity is destroyed once
  the last particle dies. An emitter is spent only once it has fired.

### Planar reflections

A `PlanarReflector` is a rectangle facing its entity's +Y. While the camera sees
it from its front, glossy pixels lying on it - within the depth's resolution of
the plane, facing its way - take its picture: the scene mirrored in the plane,
read at the pixel's own place on the screen, shifted by how far the pixel's
normal leans off the plane's (a ripple, a normal map) and blurred by its
roughness. It shows what the screen-space trace cannot: what is behind the eye,
off the screen, or facing away, which is any mirror that faces the viewer.

- **It is a second drawing of the scene.** Opaque and cut-out meshes only, lit
  by every light rather than the camera's clusters, without GTAO, screen
  shadows, fog or particles; at `resolution` of the frame's size. A frame draws
  the few covering the most of the screen; the rest fall back to the trace and
  the probes.
- **The plane is the entity's position.** Put it on the reflecting face, not
  inside a slab: what lies a centimetre in front of the plane and behind is
  clipped, and the face itself is in the clipped part.

### The viewmodel

A first-person game's arms and weapon are `Mesh::seenBy = Viewmodel`: seen by
the eye alone and drawn through a projection of their own - the camera's
`viewmodelFovY` (its `fovY` at 0) with a near plane of
`Camera::VIEWMODEL_Z_NEAR` - so they keep their look at any field of view and are
not cut by the world's near plane. They are lit and shadowed where they are.

The prepass and forward vertex shaders project through `eyeClip`
(`shaders/viewmodel.glsl`), which squashes a viewmodel's depth into the nearest
`VIEWMODEL_DEPTH_SHARE` of the range, so a weapon pushed into a wall stays in
front of it. Anything reading depth afterwards would take the weapon for
something at the lens, so GTAO, depth of field, the reflection trace and the
screen-space shadows skip that band (`inViewmodelBand`, `shaders/camera.glsl`).
An orthographic camera has no band.

A viewmodel casts no shadow, no capture sees it, and no decal lands on it. An
emitter's `ParticleEmitter::seenBy` reads as a mesh's, so a `Viewmodel` muzzle
flash is projected the same way and lines up with the barrel.

### GLView - GPU sync

`GLView` mirrors mesh, material, texture and font-atlas assets, each slot
remembering the version and handle generation it was uploaded at.
`sync(view, resources)` walks every list on `RenderView` that names a handle -
visible and scene-wide objects, decals, particle textures, UI commands - and
uploads only what moved on ([resources.md](resources.md) has the versions).
Every list must be walked, because some are gathered scene-wide (an off-screen
caster's mesh, a decal's material) and a pass silently skips a draw whose GPU
object it cannot resolve. A scene load from a file swaps the whole asset graph,
which no per-asset check sees, so the backend drops every mirror
(`GLView::invalidate`) when `ResourceManager::epoch()` moves
([resources.md](resources.md#versioning)).

**Every mesh lives in one pool** (`GLMeshPool`): one index buffer, and one vertex
array per layout over shared buffers, each `GLMesh` a range in them. That lets a
run of different meshes go out as one `glMultiDrawElementsIndirect`
(`GLDrawList`). See [animation.md](animation.md) for why skin data rides in a
second stream rather than in `Vertex`.

### Shader binding contract

Every binding point and texture unit is one row of `VKM_GL_BINDINGS` in
`convention/gl_bindings.h`. The table expands into the `GLBindings` constants C++
binds with, and, through `GLBackend::shaderConstants`, into `#define`s in the
prelude every stage is compiled with (`UBO_CAMERA`, `SSBO_LIGHTS`,
`MATERIAL_SLOT_ALBEDO`, ...) - every sampler takes its unit from a
`layout(binding = ...)`, never from C++. Fragment outputs (`OUT_*`), work-group
sizes (`GROUP_*`) and the light and material types (`LIGHT_*`, `MAT_*`) arrive
the same way; see also [lighting.md](lighting.md#limits-and-the-shader-prelude).

Every instanced draw has one shape: per-vertex attributes (`ATTR_*`) plus one
per-instance uint naming the object - its index into the storage buffers the
frame's models, world boxes and bone ranges went up in (`shaders/objects.glsl`).
The Camera block (`shaders/camera.glsl`, `CameraUBO` in `gl_camera.h`) carries
every fact about the eye, so no pass sets one as a uniform of its own.

## Key files

- `src/engine/system/render/render_system.h` - `RenderSystem`, owns the backend
- `src/engine/system/render/render_view.h` - `RenderView`
- `src/engine/system/render/data/` - the frame structs (`CameraData`, `RenderObjects`, `LightData`, ...)
- `src/engine/system/render/render_settings.h` - `RenderSettings`, `RenderMode`, `withPreset`
- `src/engine/system/render/render_backend.h` - `RenderBackend`
- `src/engine/system/render/editor_render_hooks.h` - `EditorRenderHooks`
- `src/engine/platform/window/window_surface.h` - `WindowSurface`
- `src/engine/ecs/environment.h` - `Environment`
- `src/backend/opengl/gl_backend.cpp` - the frame and the pass list
- `src/backend/opengl/convention/` - `gl_bindings.h`, `gl_depth.h`
- `src/backend/opengl/pass/` - one file per pass
- `src/backend/opengl/offline/` - the bakers, `GLSceneCapture` and `GLCaptureShadow`
