# Rendering System

The renderer has two halves with one seam between them. The engine half builds a
backend-agnostic `RenderView` each frame; the backend half syncs its GPU resources to that view
and runs a **fixed, ordered list of passes**. There is no render graph, and passes (`GLPass`) are
the backend's own - the engine never sees one. Lighting and shadows are
[lighting.md](lighting.md), what is drawn is decided by [visibility.md](visibility.md), and water
is [water.md](water.md).

The renderer has no TAA ([decisions.md](decisions.md#what-has-been-decided)); FXAA, motion blur
and lens flare are not built.

The sections up to [How it works inside](#how-it-works-inside) are what a game sets and the
rules that are easy to get wrong; the rest is the frame, for whoever changes the renderer.

## RenderSettings and RenderMode

`RenderSettings` (`render_settings.h`) is the project's look and what it costs, all of it shipped
in `project.json`'s `render` block; each field's meaning is on the field. What the scene looks
like - sky, night, fog, IBL intensity - is the scene's `Environment` instead, and what a host
shows in place of the picture - a debug view (`RenderMode`), the editor's grid - is the host's
(`HostChrome::Display`): the editor's viewport sets it, and a run shows one by
`vkm_runtime --view <name>`, by the names the editor lists (`"Lighting Only"`).

**A game reads and changes them through `Graphics`** (`core/graphics.h`, a behavior's `graphics()`):
`settings()` is what the frame draws with, `set()` changes it from the next frame, and `setQuality`
applies one of the `GraphicsPreset` tiers. An effect the project has off stays off however it is
asked (`Graphics` names them), so a game can never turn on what the project's look leaves out. A
player's choices - what a tier sets, the occlusion cull, and the window mode `setWindowMode` asks
for - are kept in their `graphics.json` (`Graphics::keepChoicesIn`) and read back at the next start;
a bench, the editor and a server keep none. A packaged game opens fullscreen unless its player chose
a window, asked before its first frame so the desktop's switch lands under the splash; a run `vkm`
starts, and a bench, stay in a window (`vkm_runtime --windowed`), so their frames compare.

**`textureAnisotropy` is offered, not imposed:** a texture that states `Nearest` takes none
([resources.md](resources.md#textureasset)), and a map of normals or data takes at most 4x, as a
slant's specular shows less of it than its color.

**Render scale.** `renderScale` sizes every scene target (`RenderView::sceneWidth`). Below the
viewport's size, the Upscale pass widens the tonemapped frame rather than stretching it: an
edge-adaptive spatial upscale that keeps edges whole, then a contrast-adaptive sharpen of what it
softened (Lottes 2021, `shaders/opengl/post/upscale`). It is spatial, so it needs no
history. The UI, splash and grid draw after it at full resolution. A settings screen offering
resolutions lists `WindowManager::renderResolutions`; one is a `renderScale` of its height over
the viewport's.

### Color grading

The composite grades the frame through a 3D lookup table. `gradingLut` names a strip image in
the project (its layout is on `RenderSettings::gradingLut`), and `gradingStrength` mixes the
graded color over the ungraded; with no table, or a strength of 0, the lookup is skipped.

The table maps display values to display values: the frame after the tonemap and the sRGB
encode, before the dither - the frame as a screenshot holds it. So a grade is made in any image
editor:

1. In Render Settings, **Neutral** brings the engine's neutral strip (`content/grading`) into the
   project as `assets/grading/neutral.png` and grades through it, which changes nothing.
2. Take a screenshot of the frame.
3. Open the screenshot and the strip together. Grade the screenshot with color adjustments
   alone - curves, levels, hue and saturation, color balance, a gradient map - and apply the
   same adjustments to the strip. Anything that moves or blurs pixels breaks the table.
4. Save the strip under a name of its own, as a PNG with its colors unconverted, and **Pick** it.

A file that is no strip is reported, and the frame goes ungraded. The editor reads a strip saved
over again, as it does the shaders, so each pass at a grade shows as it is saved; a game never
polls. A game changes the grade from code, for a map or a moment: the `gradingLut` of the
settings it hands `graphics().set()`.

### Outlines

An `Outline` (`ecs/component/render/outline.h`) rims the Mesh on its entity in one color, so the
mesh reads against whatever lies behind it - a character against a busy scene. The rim is drawn
into the multisample scene, so it is antialiased as any edge; its `width` is in pixels of the
window at any distance; it takes no light, and its color shows as the tonemap shows that value
in any light. With `throughWalls` it is drawn wherever something stands in front too. With no
outline the pass costs nothing.

### Particles

`ParticleSystem` steps every `ParticleEmitter` on the CPU; the emitter's fields and what each
blend means are documented in `ecs/component/effect/particle_emitter.h`. What is easy to get
wrong:

- **Particles are world-space and never re-based.** A particle starts on the emitter's shape and
  moves on its own; moving the emitter afterwards leaves its particles where they are.
- **`blend` splits the draw.** Additive particles draw unsorted; alpha and distortion ones draw
  back to front, their billboards and then their strips, each sorted among themselves: a strip
  and a card of one blend do not interleave. A distortion particle bends a copy of the frame drawn
  so far - the additive and alpha particles, but no other haze.
- **Smoke draws at half resolution** once the alpha billboards in view would cover the scene
  `GLParticlePass::HALF_RESOLUTION_COVER` times over, and blends back over the scene as one
  particle, never bleeding onto what stands in front of it. Additive and distortion particles
  stay at full resolution.
- **A colliding particle never strikes its emitter's own entity.**

### The viewmodel

A first-person game's arms and weapon are `Mesh::seenBy = Viewmodel`: seen by the eye alone and
drawn through a projection of their own - the camera's `viewmodelFovY` (its `fovY` at 0) with a
near plane of `Camera::VIEWMODEL_Z_NEAR` - so they keep their look at any field of view and are
not cut by the world's near plane. They are lit and shadowed where they are, cast no shadow, no
capture sees them, and no decal lands on them. An emitter's `ParticleEmitter::seenBy` reads as a
mesh's, so a `Viewmodel` muzzle flash is projected the same way and lines up with the barrel.

The prepass and forward vertex shaders project through `eyeClip` (`shaders/opengl/include/viewmodel.glsl`)
and the particles through the `clipOnLayer` it calls (`shaders/opengl/include/camera.glsl`), which squashes
a viewmodel's depth into the nearest `GLDepth::VIEWMODEL_SHARE` of the range, so a weapon pushed into
a wall stays in front of it. Anything reading depth afterwards would take the
weapon for something at the lens, so GTAO and the reflection trace skip that band
(`inViewmodelBand`, `shaders/opengl/include/camera.glsl`). An orthographic camera has no band.

### Debug drawing

`DebugDraw` (`debug/draw/debug_draw.h`) is immediate-mode drawing in the world: lines and shapes that
anything submits each frame - a behavior through `debugDraw()`, a system through `ctx.debug`, the
editor. `debugDraw().sphere(center, radius, color)` is a whole call; a `DebugStyle` adds a width,
a lifetime, a depth mode and a category. The header says how long a drawing stands, which threads
may draw, and the cap. A shipped game draws none: its engine
([building.md](building.md#the-shipping-engine)) hands every behavior `DebugDraw::inert()` and
every system a null `ctx.debug`.

**Categories are the host's to show** (`DebugCategory`, `setShown`). `General`, gameplay's own, is
on from the start; the engine draws the others from the scene - `DebugViewSystem` its components,
`AudioSystem` the acoustics' rays ([audio.md](audio.md#acoustics)), `PhysicsSystem` the tick's
contacts. The editor sets the toggles from View > Show; a game with no editor around it is its own
host and turns on what it wants to see.

## How it works inside

### Per-frame flow

```
RenderSystem::update(FrameContext)
  |-- DebugDraw::collect          the frame's debug lines, RenderView::SceneLists::debug
  |-- RenderView::build(...)      engine side: borrow the Visibility product,
  |                               gather lights / probes / decals / particles / waves
  |-- backend.render(view, resources)              // GLBackend
        |-- onWorldReplaced   drop every cache of a replaced world or asset graph
        |-- GLAssetMirror::sync      upload changed GPU resources
        |-- programs still compiling: the chrome alone, and stop
        |-- air tables / bake IBL, or a drifting sun's step a frame
        |-- under a whole loading screen, the chrome alone and the object buffer the bakes
        |   read; once a world has arrived there, the world over a slice of all of it
        |   (warming: see below). Otherwise the world (GLBackend::drawWorld):
        |     |-- shadow plan       atlas slots; caster cull forked onto the thread pool
        |     |-- partitionDrawables, per-frame UBOs, skin palette, object buffer, opaque batch
        |     |-- the waves' swell for the frame (GLWaveFields::beginFrame)
        |     |-- run the passes in order
        |     |-- measure the exposure (autoExposure)
        |-- bakes: the irradiance volume, the lightmaps, then the probes
```

`GLBackend::render` (`src/backend/opengl/gl_backend.cpp`) is the authority for this order.

**Warming.** The driver builds a program for the state of its first draw, so the frame that first
shows the sea or a burst of sparks would pay for it. A world that arrives behind a whole loading
screen is drawn there whole, a slice of its objects a frame (`WARM_SLICES`, `gl_backend.cpp`) so no
frame of the screen holds long: every object the scene draws as if the eye saw it, and a particle
and a strip of each blend before the eye (`GLBackend::warmView`), with the screen drawn over it.
The screen waits for it: the slices are steps the backend owes (`RenderReadiness`), so a world that
is quick to ready still shows drawn whole. A host with no loading screen (the editor's Play) warms
nothing. What no slice draws is made where it is first drawn: smoke thick enough to draw at half
size, a decal or an outline the eye's view does not hold, and the shadow program of a kind of
caster - skinned, cut by its alpha - that no tile near the eye holds.

### RenderView - the contract

The entire engine-to-backend interface besides the asset graph (`ResourceManager`, which
resolves the handles it names); a backend consumes exactly this. `build()` refills each frame
what it gathers from the scene, reusing the vectors' capacity, and borrows the cull's and the
frame's other products by pointer; `RenderSystem::update` sets the rest. What crosses: the
scene's rect in the backbuffer and the backbuffer's size; the frame's and the world's clocks, the
bake budget and whether the world is still arriving; the camera `Visibility` culled for and that
product, borrowed; the lists gathered through the camera - lights, probes, decals, outlines,
particles and their strips, the irradiance volume, the waves, the bone palettes and the debug
lines; the settings, the host's display and the scene's environment; what draws over the world -
the UI, the splash and the loading screen, with the pointer that stirs its water; and the world's
epoch, which tells a backend's captures their world was replaced (`GLBackend::onWorldReplaced` is
the one place that checks). Each field is documented on the struct, in
`system/render/render_view.h`.

Besides grouping the decals by material, the engine sorts nothing it draws; the backend does.
`partitionDrawables` splits opaque, alpha-mask and transparent; `GLInstanceBatcher` groups
instances by program, winding, material and mesh; the forward pass draws the depth-writing
buckets, then transparents back to front, over a snapshot of the opaques when one of them
refracts (`transmission`).

### RenderBackend - the seam

The engine's view of the backend is `RenderBackend` (`render_backend.h`), each member documented
there; the editor's offscreen renders go through `EditorRenderHooks` (`editorHooks()`). The seams
are [architecture.md](architecture.md#what-everything-else-stands-on)'s, and which files may name a
graphics API is [decisions.md](decisions.md#what-must-stay-true)'s. One backend exists, OpenGL
([decisions.md](decisions.md#what-has-been-decided)).

### OpenGL backend

`GLPreview` is a separate minimal forward + tonemap path for the editor's previews - thumbnails
and the asset windows' viewports; it does not run the pass list. A target nothing but its pass
reads is that pass's own.

**Targets and the color chain.** `m_sceneHDR` is the geometry target (color, depth, G-buffer);
`m_sceneMS` its multisample twin while MSAA is on. Post passes never blit back into it: a pass
samples `colorSrc`, writes `colorDst` and calls `flipColor()`, over the `m_postA`/`m_postB`
scratches. Depth and the G-buffer are always sampled from the geometry target.

**MSAA.** The geometry passes draw into `m_sceneMS` and `GLResolvePass` resolves it; the post
chain is single-sample. Depth and the G-buffer take one sample (an average of two surfaces'
depths is neither); color and the reflection inputs are averaged tonemap-weighted, so one bright
sample does not outweigh its pixel.

**Depth is reversed for the eye:** 1 at the near plane, 0 at the far, in a 32-bit float buffer;
shadow maps keep near 0, far 1. How a pass keeps to it is rule 9 of
[Writing a pass](#writing-a-pass).

### The passes (fixed order)

The `m_passes` list in `gl_backend.cpp`, run top to bottom; each pass's header says what it does.
A new pass goes after everything it reads.

| # | Pass | Does |
|---|------|------|
| 1 | DepthPrepass | Clears the scene target; early-Z for opaques and the G-buffer. Runs `GLOcclusionCull` when that is on |
| 2 | ResolveDepth | MSAA only: depth and G-buffer |
| 3 | GTAO | Ground-truth AO and bent normal, at half size, with no history |
| 4 | ClusterCull | Compute: lights into the Forward+ cluster grid |
| 5 | Shadow | What moves under the sun and spot tiles into the atlas, point cubes, and what the eye uncovered into the sun's held layers ([lighting.md](lighting.md#shadow-atlas)); after the passes that read no shadow, so the caster cull runs beside them |
| 6 | FogCompute | Compute: the froxel fog volume, before anything lit reads it ([Fog](#fog)) |
| 7 | Atmosphere | Compute, procedural sky only: sky-view table and aerial perspective ([lighting.md](lighting.md#image-based-lighting-ibl)) |
| 8 | Water | The ripple fields every water reads, and the shore its waterlines foam by ([water.md](water.md#how-it-works-inside)) |
| 9 | Skybox | The background, before geometry so transparents blend over it |
| 10 | Forward | The PBR ubershader: opaque, alpha-mask (alpha-to-coverage under MSAA), then transparents; writes the reflection inputs while `ssr` is on |
| 11 | Outline | The rims an `Outline` asks for ([Outlines](#outlines)); through walls it marks the color target's alpha, which no later pass may read |
| 12 | ResolveColor | MSAA only: color and reflection inputs, and depth again now that alpha-masked geometry is in it |
| 13 | Particles | Billboards and strips into the resolved scene, depth-tested, never depth-writing, soft against scene depth ([Particles](#particles)) |
| 14 | Reflections | Replaces the probe or sky reflection where a screen-space trace hits |
| 15 | Decals | Projected boxes into the color chain, lit as the surface under them through the live shadow maps alone: the G-buffer holds no lightmap coordinates, so a decal past a light's live reach is lit where a baked floor is shadowed |
| 16 | Bloom | Compute: bright-pass and mip down/upsample; the composite adds it |
| 17 | Composite | Bloom, exposure, tonemap, sRGB encode and the color grade to the viewport, or the host's debug view |
| 18 | Upscale | A scene smaller than the viewport, upscaled and sharpened into it ([Render scale](#rendersettings-and-rendermode)) |
| 19 | Grid | The editor's grid and axes, after the tonemap so the axes keep `DebugPalette::AXES` |
| 20 | DebugLines | The debug lines over the tonemapped frame ([Debug drawing](#debug-drawing)) |
| 21 | UI | The screen-space UI ([ui.md](ui.md)) |
| 22 | Splash | The startup logo over black, covering the frame |
| 23 | Loading | The loading screen: its backdrop (the engine's water, or black), the project's screen (`RenderView::cover`, through the UI pass) and its bar; backdrop and bar while no splash shows |

**The forward programs.** One fragment file, `shaders/opengl/scene/forward/pbr`, built from modules by job -
the fragment's frame (`shaders/opengl/scene/forward/shading.glsl`), the material's lobes (`lobes.glsl`), the
direct lights (`direct_light.glsl`), the environment's light (`environment_light.glsl`) and what
it writes (`output.glsl`) - and compiled in the fixed set of variants `GLForwardPrograms`
builds; water draws with a program of its own from the same modules. Within a program, features
are runtime switches on the material's values. Helpers the passes share are one include each
under `shaders/opengl/include/` (`basis.glsl`, `objects.glsl`, ...) - reuse them rather than copy a
function into a shader.

### Writing a pass

Passes are a backend detail and stay in `src/backend/opengl/`
([decisions.md](decisions.md#what-the-engine-refuses)):

1. **Subclass `GLPass`** (`pass/gl_pass.h`) and override `execute(GLFrameContext&)`. Before every pass
   and after the last, the backend resets the state `Context::setDefaultState` tracks, then sets
   the pass depth test with culling off, so there is no epilogue to write for it. The viewport,
   the scissor rectangle, the clear color and the bindings - framebuffer, program, textures - are
   not reset: a pass sets the ones it uses.
2. **Gate yourself in the first lines of `execute()`** - `if (!ctx.view.settings->bloom) return;`.
   A frame that draws the world runs every pass; one that draws the chrome alone
   ([Bakes](#bakes)) runs only `GLBackend::drawChrome`'s.
3. **Register it in the ordered list** in `gl_backend.cpp`, after the passes that write its
   inputs. The list is the schedule - no graph.
4. **What you produce for a later pass goes on `GLFrameContext`,** the backend's own per-frame
   carrier. The engine never sees it.
5. **Open no top-level profile zone** - the loop already opens one on your registered name
   ([building.md](building.md#profiling-with-tracy)).
6. **Count what you bind and draw**, which is what a pass costs. The driver validates each
   framebuffer bind and each draw after a state change. A mip chain is walked in compute, a
   dispatch a level. Scene geometry goes out through a `GLDrawList` as one multi-draw per change
   of program, winding, material or vertex layout, never a draw per mesh.
7. **Per-frame data goes through the frame's stream** (`ctx.mirror.stream()`, a
   `Vkm::OpenGL::StreamRing`), not a buffer of the pass's own refilled each frame; a buffer the GPU
   writes stays its owner's.
8. **OpenGL is vkmOpenGL's.** A texture, buffer, vertex array, program or framebuffer is a
   vkmOpenGL object, edited by name through its methods - a pass binds only to draw or sample - and
   pipeline state goes through `Vkm::OpenGL::Context`'s cache. What vkmOpenGL cannot do yet is added
   to vkmOpenGL, with a test, not written as raw `gl*` calls in the backend; inside vkmOpenGL every
   call is under `VKM_OPENGL_CHECK`.
9. **Depth has one convention, reversed for the eye.** Clip z runs 0..w (`glClipControl`), tested
   with `GL_GREATER`; the engine's projections stay glm's, and `GLCamera` remaps the camera
   block's matrices. Clear, test and read depth through `convention/gl_depth.h` and
   `shaders/opengl/include/depth.glsl` (`EYE_CLEAR`, `EYE_TEST`, `isFarDepth`, `nearerDepth`...; the prelude's
   `NEAR_DEPTH` and `FAR_DEPTH`), never a literal `1.0` or `GL_LESS`; a shadow map keeps near 0,
   far 1 through `GLDepth::shadowProjection`.

A new shader folder uses the loader's filenames ([building.md](building.md#shaders)). Check
[decisions.md](decisions.md#what-has-been-decided) before adding an effect.

### Fades

What fades and when is the cull's ([visibility.md](visibility.md#the-cull)). Only the eye's batches
draw a fade (`MeshOf::Eye`): an object on a share of each pixel's samples, and across a hand-over
the next level on the rest, an instance each (`shaders/opengl/scene/forward/fade.glsl`,
`gl_draw_list.h`). Under MSAA the resolve averages that into a cross-fade; at one sample it thins
the pixels.

### Bakes

IBL, reflection probes, the lightmaps and the irradiance volume are not passes: they run inside
`render()`, so the frame that notices a change pays for it, and a long bake goes on over frames
as `RenderView::bakeBudget` allows. What each bakes, and when, is
[lighting.md](lighting.md#what-a-capture-is-lit-by) and
[lighting.md](lighting.md#baked-shadows); the rules every bake keeps:

- **A frame with no camera bakes no probe or volume** and keeps what it has.
- **Nor does a world that just arrived and still waits on its meshes, textures or sky's
  image** (`RenderView::worldArriving`): a capture taken then would be of a world part-made.
- **A scene has one irradiance volume**, the lowest-slot entity with an `IrradianceVolume` and a
  `Transform` (`findIrradianceVolume`); `IrradianceVolume::MAX_RESOLUTION` and `MAX_PROBES` cap
  its grid, because the bake and its atlas are cubic in it. Until a bake ends, the frame reads the
  grid it had over the box that grid was baked for - none in a replaced world - and the
  reflection probes wait for it ([lighting.md](lighting.md#what-a-capture-is-lit-by)).

`RenderBackend::readiness()` counts what a loading screen waits on: first the programs the backend
built at startup, then the slices of a world drawn whole behind it and the steps of the bakes
begun as of the last frame - none with no camera, and one while the world waits on its meshes or
textures, so the cover stays until its bakes have begun. The driver compiles the programs on its
own threads (`Vkm::OpenGL::ShaderBase::compileInParallel`); until the last has compiled and been
checked, a frame draws the chrome alone - the splash and the loading screen - over black
(`GLBackend::drawChrome`), as a world pass would wait on its program and hold the window. After
that a program built later is waited for where it is first used, and only a whole loading screen
draws the chrome alone ([ui.md](ui.md#the-loading-screen)).

The splash's images are made on a worker: the splash pass decodes each when its path changes,
works out the ident's shape and glow there too, and only uploads them. Until they are drawn
`RenderReadiness::splashPending` says so, and `SplashSystem` holds its clock at the black its entry
starts from, so a slow decode delays the splash rather than cutting into it.

### Fog

`GLFogPass` fills a froxel volume over the camera's frustum out to `FogSettings::maxDistance`,
scattering every clustered light through its own shadow and the environment's light - the sky and
the irradiance volume - so fog in shade is lit as the walls there are.

**No pass fogs the finished frame.** Everything drawn through the medium fogs itself at its own
depth as it is drawn, through `shaders/opengl/include/fog.glsl` (bound by `GLPass::bindFog`) - a pass over the
frame could only fog by the opaque surface behind, and a muzzle flash in thick fog would vanish.
A new program that draws into the scene must take the fog too. Under the procedural sky, `fogAt`
also applies the aerial perspective beneath the fog. How each blend mode takes it is in the
header comment of `shaders/opengl/include/fog.glsl`.

Unfogged: the shading-split debug views, the grid, the debug lines and UI, and the offline
captures and editor previews, since each volume describes this camera's frustum, not a probe's.

### GLAssetMirror - GPU sync

`GLAssetMirror` mirrors the mesh, material, texture and font-atlas assets a frame references, uploading
each again only when its version moves ([resources.md](resources.md#versioning)). `sync` walks
every `RenderView` list that names a handle, the scene-wide ones too - an off-screen caster's
mesh, every material, every level a fixed object may draw - and, as it walks them, every mesh,
material and texture the asset graph holds, as a pass silently skips a draw whose GPU object it
cannot resolve, and a frame that first turned to an object, picked another level or spawned a
prefab would pay for the upload. A scene load from a file swaps
the whole asset graph, which no per-asset check sees, so the backend drops every mirror
(`GLAssetMirror::invalidate`) when `ResourceManager::epoch()` moves.

**Every mesh lives in one pool** (`GLMeshPool`): one index buffer, and one vertex array per layout
over shared buffers, each `GLMesh` a range in them. That lets a run of different meshes go out as
one `glMultiDrawElementsIndirect` (`GLDrawList`). See [animation.md](animation.md) for why skin
data rides in a second stream rather than in `Vertex`; the static layout's lightmap coordinates
ride in one of their own for the same reason.

### Shader binding contract

Every binding point and texture unit is one row of `VKM_GL_BINDINGS` in `convention/gl_bindings.h`.
The table expands into the `GLBindings` constants C++ binds with and into the `#define`s shaders
read them by (`UBO_CAMERA`, `SSBO_LIGHTS`, `MATERIAL_SLOT_ALBEDO`, ...), through the prelude
([lighting.md](lighting.md#limits-and-the-shader-prelude)) - every sampler takes its unit from a
`layout(binding = ...)`, never from C++. Fragment outputs (`OUT_*`), the UI quad's attributes
(`UI_ATTR_*`), work-group sizes (`GROUP_*`) and the light and material types (`LIGHT_*`, `MAT_*`)
arrive the same way. An image unit, and an output of a target one pass owns, is that dispatch's or
that pass's own, numbered where it is bound beside the one shader that reads it.

Every instanced draw has one shape: per-vertex attributes (`ATTR_*`) plus one per-instance uint
naming the object - its index into the storage buffers the frame's models, world boxes and bone
ranges went up in, read through `instanceObject()` (`shaders/opengl/include/instancing.glsl`, over
`objects.glsl`), with its fade above it ([Fades](#fades)). The Camera block
(`shaders/opengl/include/camera.glsl`, `CameraUBO` in `gl_camera.h`) carries every fact about the
eye, so no pass sets one as a uniform of its own.

## Key files

- `src/engine/system/render/render_system.h` - `RenderSystem`, owns the backend
- `src/engine/system/render/render_view.h` - `RenderView`
- `src/engine/system/render/data/` - what `RenderView` gathers (`LightData`, `ProbeData`, `DecalData`,
  ...); the camera and the cull's product are `core/frame/`'s (`CameraData`, `RenderObjects`)
- `src/engine/io/project/render_settings.h` - `RenderSettings`
- `src/engine/core/render_mode.h` - `RenderMode`, the debug views
- `src/engine/core/graphics.h` - `Graphics`, `GraphicsPreset`: the frame's settings and the tiers
- `src/engine/system/render/render_backend.h` - `RenderBackend`
- `src/engine/core/frame/render_readiness.h` - `RenderReadiness`
- `src/engine/system/render/editor_render_hooks.h` - `EditorRenderHooks`
- `src/engine/debug/draw/debug_draw.h` - `DebugDraw`; `debug_line.h` its record, `debug_category.h` its
  categories, `debug_palette.h` their colors and the axes'
- `src/engine/system/debug_view/debug_view_system.h` - `DebugViewSystem`
- `modules/vkmPlatform/src/vkm_platform/window/window_surface.h` - `WindowSurface`
- `src/engine/ecs/settings/environment.h` - `Environment`
- `src/backend/opengl/gl_backend.cpp` - the frame and the pass list
- `src/backend/opengl/convention/` - `gl_bindings.h`, `gl_depth.h`
- `src/backend/opengl/pass/` - one file per pass
- `src/backend/opengl/offline/` - the bakers, `GLSceneCapture` and `GLCaptureShadow`
