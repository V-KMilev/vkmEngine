# Lighting

How a surface is lit: three light types (Directional, Point and Spot) as data-only `Light`
components, evaluated in the forward PBR shader against a Forward+ cluster grid; shadows from a
shared atlas, layers held as the eye moves and, for what never moves, lightmaps; and the ambient
from the sky, reflection probes and an irradiance volume. Where each of these runs in the frame
is [rendering.md](rendering.md#the-passes-fixed-order).

The sections up to [How it works inside](#how-it-works-inside) are what a scene sets; the rest is
for whoever changes the renderer.

## Lights

`Light` and its fields are documented in `src/engine/ecs/component/light/light.h`. Position
comes from the entity's `Transform`; direction is the entity's forward, its rotated -Z. A spot
shines along it.

`sourceRadius` is the light's size - a directional light's angular radius in radians, a point or
spot's emitter radius in meters. It sizes the highlight and the baked shadow's penumbra; a live
shadow's filter is the renderer's ([Filtering](#shadow-atlas)).

`flicker` wavers a point or spot light as a fire does, by the world's clock (`flickerWave`). It
scales the light as it is shaded, never `intensity`, so gameplay reads and the scene saves the
steady value, and the probes and the irradiance volume take the light steady and never rebake
for it.

`movable` off marks a light that never moves: the shadows fixed meshes cast in it are baked
([Baked shadows](#baked-shadows)). `inCaptures` off leaves a light out of every capture: a
carried torch or a floating lantern near a probe would keep it capturing again for good.

The **key light** is `findKeyLight`: the lowest-slot enabled directional light. The sun a shadow
is drawn for is `lowestSlotDirectionalCaster`, the lowest-slot directional light that casts - not
always the key light.

### The sun and the sky

**The sun's place is authored on the `Environment`** (`sunElevation`, `sunAzimuth`), because the
sky must work with or without a directional light. While the sky is procedural, `SkySystem`
writes the key light's rotation, color and intensity every frame - by day from `sky.lightColor`
and `sky.lightIntensity`, reddened at a low sun; by night from the `night.moonlight*` pair. Author
those on the environment, not the `Light`: an edit to the light is overwritten next frame
([editor.md](editor.md#inspector-cards)). Shadow settings stay the light's.

**One sun lights the scene and the sky.** The sky's radiance is the same sun
(`Atmosphere::solarIlluminance`) through the atmosphere, with no constant between them, so a
brighter sun brightens the sky with it. `sky.intensity` scales the skybox, the ambient and the
air's light. Below the horizon a skyglow and a moon take over across
`NightSkySettings::TWILIGHT_DEGREES` either side of it, and the key light fades across the same
band. An HDR sky (`sky.hdrPath`) stands in for the procedural one.

## Where the ambient comes from

- **Reflection:** the reflection probes covering the point, parallax-corrected, smallest box
  first (so a room's probe wins over the yard's), over the global sky. A probe's `intensity`
  scales its reflection and irradiance alike.
- **Diffuse:** the irradiance volume (SH-L1, `irradianceVolumeLookup`) wherever it covers the
  point, fading over `blendDistance` from its faces; elsewhere the reflection's own source.
  Inside the volume the reflection is normalized by the ratio of the irradiance here to what its
  capture saw, so a floor in shade does not reflect the open sky at full strength.
- **No sky baked:** no reflection, and a flat diffuse floor (`FLAT_AMBIENT` in
  `shaders/opengl/include/ambient.glsl`); probes and the volume still apply where they cover.
- **Screen-space reflections** go over all of it on near-mirrors - water, polished floors - whose
  opaque material is smoother than `GLReflectionPass::MAX_ROUGHNESS`: where a pixel's ray hits,
  the scene's color replaces the probe or sky reflection. The material's own roughness decides,
  so a far sea stays traced; alpha-masked cutouts never are. The environment's intensity scales
  the probes, the volume and the sky, not the traced reflection - so captures are taken at unit
  intensity, or they would carry it twice.

A probe or the volume bakes when it is new or its `bakeVersion` is bumped, and once a moved or
resized one has held still (`CAPTURE_SETTLE_FRAMES`); a change of the lights or the sky lights
it again without a bake ([What a capture is lit by](#what-a-capture-is-lit-by)). A volume filling
a room wants its box out to the middle of the walls and a `blendDistance` shorter than their
half-thickness; longer, the room's surfaces near a face take part of the sky.

## Baked shadows

A light that never moves (`Light::movable` off, casting) has the shadows fixed meshes cast in it
baked into lightmaps, read in place of its shadow map where they fall on fixed meshes. The live
map is still drawn, every caster in it, for whatever is not baked. The sky is baked much as such
a light, into every receiver whether or not a light is fixed: the share of it each texel sees
past the fixed meshes, which occludes the ambient ([Shading model](#shading-model)).
`GLLightmapBaker` holds the sizing and the schedule.

- **What is baked.** A receiver is a fixed mesh (`Mesh::movable` off), a terrain chunk or a
  terrain plant, if its mesh carries lightmap coordinates (`MeshAsset::lightmapUVs`; a chunk's are
  its grid) and its material is opaque; a cut-out, glass, water and what moves keep the live
  path. Casters are the fixed meshes, a cut-out cut by its alpha. The fixed lights that cast take
  a channel each, the first `GLLightmaps::MAX_CHANNELS` by entity slot; another stays live.
  Receivers and casters are drawn as the meshes they were given, not the levels of detail the eye
  picks, so the bake does not depend on where the eye stood.
- **When.** On the GPU, at load and once a change to a fixed mesh or a fixed light has held still,
  wherever the eye is; under the loading cover in steps, while the game plays in small pieces of
  them. The frames read the last bake until the new one ends. Nothing is saved. A fixed light
  that moves is drawn live while it moves, and once it holds still only its own channel bakes
  again.
- **Reading it.** A light reads its channel, and a receiver its square, only while it is as it was
  baked: a receiver still fixed, where it stood, and drawing its given mesh or a level whose
  lightmap coordinates are among that mesh's (`GLAssetMirror::readsChartsOf`); otherwise it is lit live.
  Where the bake says lit, the live map decides, so what moves still casts on a baked floor;
  where it says shadow the bake is read, but in the cascaded sun's baked penumbra, which stays
  live. The rule is `directLight` in `shaders/opengl/scene/forward/direct_light.glsl`.
- **The sky** is the share of the dome each texel sees: 1 open, 0 roofed over, a surface's view
  below the horizon counting as seen. It is the sky's share, not a near occlusion: maps drawn
  from outside cannot see into a room, so a room wants a reflection probe or the irradiance
  volume. Each probe's center is baked the same way, facing up.

## How it works inside

### Shading model

The forward fragment shader (`shaders/opengl/scene/forward/pbr/`, its lobes in
`shaders/opengl/scene/forward/lobes.glsl`, BRDF terms in `shaders/opengl/include/brdf.glsl`):

- **Direct light:** Lambertian diffuse; GGX, height-correlated Smith and Schlick specular, scaled
  for multiple scattering from the split-sum LUT so a rough metal is not too dark. Roughness never
  goes below `MIN_ROUGHNESS`. The sun is a disc and a point or spot light a sphere of its
  `sourceRadius` (Karis), so a mirror reflects a bulb as wide as the bulb.
- **Optional lobes**, switched at runtime by the material's values: transmission and volume
  absorption, clearcoat, subsurface, sheen, alpha test. Sheen follows its glTF extension (import
  takes its factors, not its textures); subsurface is a wrapped back-light for foliage. A material
  using none of these but alpha test draws with a plain program compiled without them; water
  draws with its own ([rendering.md](rendering.md#the-passes-fixed-order)).
- **Double-sided materials** draw with culling off, and a back face is lit as its own (the
  tangent frame flips on `gl_FrontFacing`).
- **Ambient occlusion:** the indirect term takes the strongest of the material's AO, GTAO and, on
  a fixed surface, the sky its lightmap sees - they usually describe one crease, so they are not
  multiplied. GTAO adds what moves and what is finer than the bake's texels; only the opaque
  bucket takes it, as it reads the depth prepass, which draws nothing else. Under the reflection
  probes the bake takes off only the sky their centers see and the texel does not, as their
  captures already hold the rest, so a probe in a closed room leaves its floor lit while one in
  the open keeps the occlusion at a wall's foot. Inside the irradiance volume, whose grid already
  holds what its box shuts out, the bake gives way.
- **Horizon occlusion** fades a normal-mapped reflection that points below the geometric surface.

### What a capture is lit by

The irradiance volume and the reflection probes are pictures of the scene (`GLSceneCapture`), so
whatever lights the captured surfaces is what they pass on: a wall lit by the open sky would light
every probe in a sealed room through the plaster. So a captured surface takes its ambient from
the volume wherever the volume covers it, and from the sky only outside it:

- **The volume keeps what each probe sees as surfaces, lit apart** (`GLIrradianceBaker`): a change
  of light relights them and never draws the scene again.
- **It is gathered in bounces** (`GLIrradianceBaker::BOUNCES`). In the first, surfaces inside the
  box take no ambient - only lights, the sun through its shadow, and emission; each later gather
  lights them from the grid the one before made. A direction that meets no geometry sees the sky,
  so the sky enters a room through its doorways and windows only. How many gathers:
  [decisions.md](decisions.md#what-has-been-decided).
- **The volume follows its lights and sky.** When the lights that reach its box
  (`lightingDigest`) or the sky differ from what it was lit by, it is relit a gather a frame, the
  result swapped in whole.
- **Probes are captured after the volume**, lit by it where it covers them, and baked again at
  once when the volume is. A probe captured under other light (`CaptureLighting`) is refreshed a
  step a frame, swapped in whole, then rests (`GLProbeManager::REFRESH_REST`): under a light that
  never holds still, its capture follows a little behind. The lights compared are those that reach
  within half its width of a probe's box, and every directional, so a lamp moving across the world
  refreshes the probes it can light, not every one.
- **Shadows:** a capture is shadowed by `lowestSlotDirectionalCaster` alone, hard - the
  convolution blurs a penumbra away - through a map of its own over the region it describes
  (`GLCaptureShadow`), since the frame's cascades fit the camera; other lights capture
  unshadowed.

### Shadow atlas

Three depth stores, their counts in `render_limits.h`, and `RenderSettings::shadowResolution` the
edge of a held layer, the sharpest of them:

- **The held layers** hold what is fixed under the sun (`ObjectDraw::fixed`), a layer a cascade.
- **The 2D atlas** holds what moves under the sun, a tile a cascade (`Config::NUM_CASCADES`), and
  spot shadows, as power-of-two tiles. The sun comes first; spots and point lights go by
  `shadowPriority`, then, only while more ask than fit, by distance to the camera. A spot or point
  light whose reach misses the view takes no tile or layer; a seen spot's tile is sized by how
  much screen its cone covers (`GLShadowPlan::build`).
- **A cube-map array** holds a layer per shadowed point light, and per spot too wide for one
  square map (`spotCastsThroughCube`), whose rim a map would leave unshadowed.

`GLShadowPass` draws them. Casters come from the **whole scene**, not the camera's visible set,
culled per tile and face on the thread pool against the light's frustum and by size: a caster
whose box's diagonal spans fewer than `MIN_CASTER_TEXELS` of the tile's texels is left out, its
shadow there a smudge the filter would blur away, while a pole or a rail keeps its length. A tile
or cube face whose casters and matrix did not change is kept (`ShadowCasterBatch::signature`), so
a still view redraws no shadow; a replaced world or a shader reload forgets every tile and layer
held (`forgetHeld`). An alpha-masked or see-through caster cuts its shadow by its alpha
(`GLMaterial::cutsItsShadow`).

**The sun's held layers.** A cascade covers a square of the light's own grid around the eye, and
the grid is fixed in the world, so a fixed caster's shadow never swims and a turning eye draws
nothing. As the eye walks, only the strip it uncovers is drawn (`GLShadowPlan::HeldLayer`): what
a frame draws is in proportion to how far the eye went. A layer the eye has left whole, or every
layer when a fixed caster changes, is drawn at once; a sun that keeps moving redraws a layer a
frame, each read under the sun it was drawn with. A scene with no fixed caster never touches
them.

**The sun's movers' tiles.** What moves draws into a tile of the atlas around the eye, drawn again
when something in it moved or the eye crossed one of its texels, and then with what moves alone,
so a mover's cost is its own triangles. A pixel takes the darker of the tile and the layer.

**Bias** is one pair of values in texels of each light's own map at the receiver
(`Light::SHADOW_BIAS`, `SHADOW_NORMAL_BIAS`), for every light type, range and cascade
([decisions.md](decisions.md#what-has-been-decided)).

**Filtering.** Every live shadow is filtered by Castano's PCF: a box of hardware compares,
smoothed by each compare's bilinear weights, its taps placed and weighted by where the point lies
in its texel so an edge glides rather than steps (`filterAxis` in `shaders/opengl/include/shadows.glsl`). The
filter is three texels wide for every light, whatever its size; the sun's movers' tile takes it
only for a sun with a size while `RenderSettings::softShadows` is on, and one compare otherwise
([decisions.md](decisions.md#what-has-been-decided)). Widths are in texels of the map at the
point, so the sun's edge spans about as many pixels near as far. Nothing is rotated per pixel -
that is grain only a temporal filter removes, and there is none. A live edge does not harden
toward its caster's contact; a fixed light's bake is as soft as its source makes it.

**Cascades.** `shadowDistance` sets how far from the eye the sun's cascades reach (spot and point
use `radius`); the shadow fades out toward its reach, and cascades blend across a band at each
split. The split is logarithmic, anchored at a fixed world distance (`CASCADE_NEAR`) rather than
the camera's near plane, so raising `shadowDistance` costs the far field density, not the
foreground sharpness. A point's cascade goes by its distance from the eye, not its view depth, so
what a pixel reads does not change as the eye turns.

### Image-based lighting (IBL)

`GLIBLBaker` (called from `GLBackend::render`, not a pass) keeps the IBL set showing the scene's
sky: an **environment cube** (what an HDR sky's skybox and the captures draw; shading never reads
it), an **irradiance cube**, a **prefilter cube** (one roughness per mip) and the **BRDF LUT**
(integrated once at startup). An HDR sky's image is decoded on a worker when its path changes,
never in a frame, and bakes once it lands; until then the sky keeps the light it had, and only
the newest path named is baked. The procedural sky bakes when a sky parameter changes or the sun
or moon moves. A sun that only drifted is re-baked a step a frame into a second set of cubes,
swapped in when complete, more steps a frame while it runs behind (`GLIBLBaker::stepsFor`), so
the ambient trails a moving sun by about one bake; anything bigger bakes whole at once. A scene
with no sky bakes nothing - it does not keep the last scene's sky; an HDR that fails to load is
logged, and the sky keeps the light it had.

**The procedural sky** is Hillaire's sky and atmosphere, from four tables in `GLAtmosphere`.
Transmittance and multiple scattering depend on the air alone and are redrawn when it changes; the
aerial-perspective volume and the sky-view table follow the camera and sun, computed by the
Atmosphere pass every frame, the sky-view table only while the skybox shows. Surfaces take the
aerial perspective with the fog ([rendering.md](rendering.md#fog)). The moon sits opposite the
sun, tilted by `MOON_TILT`; the night is in `shaders/opengl/include/night_sky.glsl`, shared by
the bake and the skybox, and stars are drawn by the skybox alone, as the prefiltered cubes would
smear them.

### Forward+ clustering

The view frustum is diced into `CLUSTER_X` x `CLUSTER_Y` screen tiles by `CLUSTER_Z` exponential
depth slices. The ClusterCull pass lists the lights touching each cluster (a point as its sphere,
a spot as its cone), and the forward pass shades a pixel against its cluster's list rather than
every light - which is why `MAX_LIGHTS` can be generous. A list holds at most
`MAX_LIGHTS_PER_CLUSTER`; lights past it are dropped, which shows as a cut, and the Light Clusters
view draws such a cluster magenta. `Config::CLUSTER_X` says what finer or coarser clusters cost.

### Limits and the shader prelude

Cross-cutting limits live in `src/engine/system/render/render_limits.h` (`Config::`), and GLSL cannot read a
C++ header. `shaderConstants` (`gl_shader_constants.h`) writes them out as GLSL from the C++ constants
themselves, and `Vkm::OpenGL::setShaderPrelude` puts that text under the `#version` of every stage,
graphics and compute. So a shader just uses `MAX_LIGHTS`, with nothing to include and nothing to
keep in step. The same carries the bindings
([rendering.md](rendering.md#shader-binding-contract)), the render-mode and tonemap enums, and
constants such as the twilight band and the atmosphere's geometry and table sizes.

Never re-define a prelude constant in a shader - GLSL rejects the redefinition, which is what
keeps a shader from holding its own copy. The spot and point shadows' near plane (`SHADOW_NEAR`, in
the shadow plan) is not in the prelude; the shader reads it from the shadow block.

## Key files

- `src/engine/ecs/component/light/light.h` - `Light`, `findKeyLight`
- `src/engine/system/render/data/light_data.h` - `LightData`, `lowestSlotDirectionalCaster`
- `shaders/opengl/scene/forward/pbr/fragment.shader`, `shaders/opengl/scene/forward/lobes.glsl`, `shaders/opengl/include/brdf.glsl` - the shading model
- `shaders/opengl/include/shadows.glsl` - shadow sampling, bias, filtering, cascades
- `shaders/opengl/include/ambient.glsl`, `shaders/opengl/include/probes.glsl`, `shaders/opengl/include/irradiance_volume.glsl` - the ambient
- `src/backend/opengl/frame/light/gl_shadow_plan.h` - `GLShadowPlan`
- `src/backend/opengl/storage/light/gl_shadow_atlas.h` - `GLShadowAtlas`, the atlas, the cubes and the held layers
- `src/backend/opengl/offline/` - `GLIBLBaker`, `GLIrradianceBaker`, `GLProbeBaker`, `GLSceneCapture`, `GLCaptureShadow`, `GLLightmapBaker`, and `GLCasterDepth`, which both offline shadow maps draw their casters through
- `src/backend/opengl/storage/light/gl_lightmaps.h` - `GLLightmaps`
- `shaders/opengl/include/lightmap.glsl` - reading the bake
- `src/backend/opengl/storage/light/gl_atmosphere.h` - `GLAtmosphere`
- `src/engine/system/sky/` - `SkySystem`, `Atmosphere`
- `src/engine/system/render/render_limits.h` - the limits
