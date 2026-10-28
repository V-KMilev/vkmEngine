# Lighting

How a surface is lit: five light types (Directional, Point, Spot, and the Rect
and Disk area lights) as data-only `Light` components, evaluated in the forward
PBR shader against a Forward+ cluster grid; shadows from a shared atlas; and the
ambient from the sky, reflection probes and an irradiance volume. Where each of
these runs in the frame is [rendering.md](rendering.md#the-passes-fixed-order).

## Lights

`Light` and its fields are documented in
`src/engine/ecs/component/render/light.h`. Position comes from the entity's
`Transform`; direction is the entity's forward, its rotated -Z. A spot shines
along it; an area light emits from the opposite face, along `-direction` (both
faces when `twoSided`).

`sourceRadius` is the light's size - a directional light's angular radius in
radians, a point or spot's emitter radius in metres. It sizes both the highlight
and the shadow's penumbra; zero is a point source with hard shadows.

`flicker` wavers a point, spot or area light as a fire does, by the world's
clock (`flickerWave`). It scales the light as it is shaded, `LightData::wave`,
never `intensity`: gameplay reads and the scene saves the steady value, and a
capture takes the light steady, so the probes and the irradiance volume, which
rebake when a light changes, do not rebake for it.

`movable` off marks a light that never moves: the shadows fixed meshes cast in
it are baked ([Baked shadows](#baked-shadows)).

The **key light** is `findKeyLight`: the lowest-slot enabled directional light.
The sun a shadow is drawn for is `lowestSlotDirectionalCaster`, the lowest-slot
directional light that casts - not always the key light.

## Shading model

The forward fragment shader (`shaders/forward/pbr/`, BRDF terms in
`shaders/brdf.glsl`):

- **Direct light:** Lambertian diffuse; GGX, height-correlated Smith and
  Schlick specular, scaled for multiple scattering from the split-sum LUT so a
  rough metal is not too dark. Roughness never goes below `MIN_ROUGHNESS`. The
  sun is a disc and a point or spot light a sphere of its `sourceRadius`
  (Karis), so a mirror reflects a bulb as wide as the bulb.
- **Optional lobes**, switched at runtime by the material's values:
  transmission and volume absorption, clearcoat, anisotropy, subsurface, sheen,
  parallax, alpha test. Sheen and anisotropy follow their glTF extensions
  (import takes their factors, not their textures); subsurface is a wrapped
  back-light for foliage. A material using none of these but alpha test draws
  with a plain program compiled without them
  ([rendering.md](rendering.md#the-passes-fixed-order)). An anisotropic
  direction needs a tangent field that runs the way the material does.
- **Double-sided materials** draw with culling off, and a back face is lit as its
  own (the tangent frame flips on `gl_FrontFacing`).
- **GTAO** occludes the indirect term, taking the stronger of it and the
  material's AO (the two usually describe one crease). Only the opaque bucket
  takes it - GTAO reads the depth prepass, which draws nothing else.
- **Horizon occlusion** fades a normal-mapped reflection that points below the
  geometric surface.

### Where the ambient comes from

- **Reflection:** the reflection probes covering the point, parallax-corrected,
  smallest box first (so a room's probe wins over the yard's), over the global
  sky. A probe's `intensity` scales its reflection and irradiance alike.
- **Diffuse:** the irradiance volume (SH-L1, `irradianceVolumeLookup`) wherever
  it covers the point, fading over `blendDistance` from its faces; elsewhere the
  reflection's own source. Inside the volume the reflection is normalised by the
  ratio of the irradiance here to what its capture saw, so a floor in shade does
  not reflect the open sky at full strength.
- **No sky baked:** no reflection, and a flat diffuse floor (`FLAT_AMBIENT` in
  `shaders/ambient.glsl`); probes and the volume still apply where they cover.
- **Screen-space reflections** go over all of it on surfaces smoother than
  `ssrMaxRoughness`: where a pixel's ray hits, the scene's color replaces the
  probe or sky reflection. No temporal filter: the ray follows the G-buffer's
  interpolated normal (a normal map is noise at one ray a pixel), and
  alpha-masked cutouts are never traced. The environment's intensity scales the
  probes, the volume and the sky, not the traced reflection - so captures are
  taken at unit intensity, or they would carry it twice.

### What a capture is lit by

The irradiance volume and the reflection probes are pictures of the scene
(`GLSceneCapture`), so whatever lights the captured surfaces is what they pass
on: a wall lit by the open sky would light every probe in a sealed room through
the plaster. So a captured surface takes its ambient from the volume wherever the
volume covers it, and from the sky only outside it:

- **The volume is gathered in bounces** (`GLIrradianceBaker::BOUNCES`). In the
  first, surfels inside the box take no ambient - only lights, the sun through
  its shadow, and emission. Each later gather lights them from the grid the one
  before made. A direction that meets no geometry sees the sky, so the sky
  enters a room through its doorways and windows only. Inside the box there is
  no fade at the faces. How many gathers:
  [engine.md](../guides/engine.md#4-what-has-already-been-decided).
- **The volume follows its lights and sky.** When the lights that reach its box
  (`lightingDigest`) or the sky (`GLIBL::version`) differ from what it was lit
  by, its surfels are relit a gather a frame, the result swapped in whole.
- **Probes are captured after the volume**, lit by it where it covers them, and
  baked again at once, as a new probe is, when the volume is re-baked
  (`GLIrradianceVolume::bakeId`). A probe captured under other light
  (`CaptureLighting`) is refreshed a step a frame (`GLProbeBaker::advance`),
  swapped in whole, then rests (`GLProbeManager::REFRESH_REST`): under a light
  that never holds still, its capture follows a little behind. The lights
  compared are those whose radius reaches within half a probe's width of its
  box, and a directional: a lamp moving across the world refreshes the probes it
  can light, not every one. A light with `Light::inCaptures` off is in no
  capture and compared by none: a carried torch or a floating lantern near a
  probe would keep it capturing again for good.
- **Shadows:** a capture is shadowed by `lowestSlotDirectionalCaster` alone,
  hard: the convolution blurs a penumbra away
  ([rendering.md](rendering.md#bakes)).

A volume filling a room wants its box out to the middle of the walls and a
`blendDistance` shorter than their half-thickness; longer, the room's surfaces
near a face take part of the sky.

## Area lights

Rect and Disk lights (`shaders/area_light.glsl`, `shaders/form_factor.glsl`) cast
no shadow.

- **Diffuse** is the exact form factor: Hill's edge integral over the emitter's
  outline (a disk as a polygon, `N_DISK` sides in `shaders/form_factor.glsl`),
  clipped to the horizon.
- **Specular** is Karis's representative point: the surface's own GGX lobe toward
  the emitter point nearest the reflection ray, renormalised by
  `(alpha / alpha')^2`, `alpha'` the lobe broadened by the emitter's size - so a
  wide emitter spreads its highlight rather than brightening it.

`intensity` is point-equivalent: radiance is `intensity / area`, so far away an
area light lights like a point light of the same intensity. `radius` only windows
the light to zero.

## Shadow atlas

Two depth stores, their counts in `engine_config.h` and the largest tile's edge
`RenderSettings::shadowResolution`:

- **The 2D atlas** holds the sun's cascades and spot shadows as square,
  power-of-two tiles. The sun (`lowestSlotDirectionalCaster`) takes
  `Config::NUM_CASCADES` of them; spots share the rest by `shadowPriority`, then,
  only while more ask than fit, by distance to the camera. A spot or point light
  whose reach misses the view takes no tile or layer; a seen spot's tile is sized
  by how much screen its cone covers. `GLShadowData::build` plans this;
  `GLShadowAtlas::layout` packs the tiles, the same sizes into the same places.
- **A cube-map array** holds a layer per shadowed point light, and per spot too
  wide for one square map (`spotCastsThroughCube`), whose rim a map would leave
  unshadowed; its lightmap bakes six faces likewise.

`GLShadowPass` draws them. Casters come from the **whole scene**, not the
camera's visible set, culled per tile and face on the thread pool against the
light's frustum (`GLShadowData::cullCasters`); a tile culled against the same
matrix, no fixed caster changed, keeps its fixed survivors and tests only what
moves (`ShadowCasterBatch::fixedSurvivors`). A tile or cube face is kept
between frames when its batch's signature (matrix and casters,
`ShadowCasterBatch::signature`) matches, so a still view redraws no shadow; the
fixed casters are signed all at once by `RenderObjects::fixedSerial`, the moving
ones each, and a posed caster leaves a batch unsigned. The sun's last cascade,
which reaches farthest and so draws the most, keeps its fixed casters' depth in
a layer of its own (`Shadow2DJob::layered`): while the layer holds, the tile
starts from it (`shaders/shadow/restore`) and draws only what moves. That
cascade is fitted a margin wider than its slice (`HOLD_MARGIN`) and keeps its
place while the slice stays inside, so the layer holds as the view walks; a
frame whose fit moves - every frame of a turning sun - draws the tile whole
instead. A replaced world or a shader reload forgets every tile and the layer
(`forgetHeld`). An alpha-masked or see-through transparent caster cuts its
shadow by its alpha (`shadow/depth_masked`, `GLMaterial::cutsItsShadow`).

**Bias.** Every light reads `shadowBias` and `shadowNormalBias` in texels of its
own map at the receiver, scaled by the light's angle to the surface (`biasSin`,
`biasSlide` in `shaders/shadows.glsl`). One pair of values holds for every light
type, range and cascade, so the defaults rarely need touching
([engine.md](../guides/engine.md#4-what-has-already-been-decided)).

**Soft shadows.** Sun, spot and point lights cast percentage-closer soft shadows
sized by `sourceRadius`: a blocker search, a penumbra from the blocker distance,
and a fixed Vogel disk of hardware compares. The disk is not rotated per pixel -
that is grain only a temporal filter removes, and there is none. A source of
zero, or `RenderSettings::softShadows` off, gives hard shadows. The sun's
penumbra is no wider than the surface's own cascade filters (`SOFT_REACH` of its
texels), so its edge spans about as many pixels near as far. A spot or point
map stops at `SOFT_SPOT_REACH`.

**Cascades.** `shadowDistance` sets how far the sun's cascades reach (spot and
point use `radius`); the shadow fades out toward its reach (`SHADOW_FADE`).
Cascades blend across a band at each split (`CASCADE_BLEND`). The split is
logarithmic, anchored at a fixed world distance (`CASCADE_NEAR`) rather than the
camera's near plane, so raising `shadowDistance` costs the far field density,
not the foreground sharpness.

The atlas is read both compared and as raw depth (blocker search, the
`ShadowAtlas` view), so the comparison lives on a sampler object:
`GLShadowAtlas::bind2D` and `bind2DRaw` bind the two.

### Baked shadows

A light that never moves (`Light::movable` off, casting) has the shadows fixed
meshes cast in it baked into lightmaps, read in place of its shadow map where
they fall on fixed meshes. The live map is still drawn, every caster in it, for
whatever is not baked.

- **What is baked.** A receiver is a fixed mesh (`Mesh::movable` off), a
  terrain chunk or a terrain plant whose mesh carries lightmap coordinates
  (`MeshAsset::lightmapUVs`; a chunk's are its grid) and whose material is
  opaque; a cut-out, glass, a sea riding an ocean and what moves keep the live
  path. Each receiver takes a square of the pages of its own, every instance of an
  unwrapped plant too, at least `GLLightmapBaker::MIN_SIDE` a side: a densely
  planted terrain spends pages on its plants, and its plants grown again bake
  again. Casters are the fixed meshes, a cut-out cut by its alpha. The fixed
  lights that cast take a channel of the pages each, the first
  `GLLightmaps::MAX_CHANNELS` by entity slot; another stays live.
- **How.** Each light draws its fixed casters into maps of its own: the sun's
  tiled over the receivers, a spot one map, a point light (or a spot too wide
  for one map, `spotCastsThroughCube`) six faces each wider than a quarter
  turn. Each page's receivers are drawn by their lightmap coordinates, and each
  texel takes the share of the light's source it sees
  (`shaders/lightmap/shadow`), so the penumbra is as wide as the source makes
  it. Receivers and casters are drawn as the meshes they were given
  (`RenderObjects::bases`), not the levels of detail the eye picks, so the bake
  does not depend on where the eye stood.
- **When.** On the GPU, a step a frame, at load and once a change to a fixed
  mesh or a fixed light has held still, wherever the eye is: each fixed
  object's given mesh is uploaded with the scene, not when the eye first draws
  it. The frames read the last bake until the new one ends. Nothing is saved.
  `GLLightmapBaker` holds the sizing and the schedule.
- **Reading it.** A light reads its channel, and a receiver its square, only
  while it is as it was baked: a receiver still fixed, where it stood, and
  drawing its given mesh or a level whose lightmap coordinates are among that
  mesh's - a decimation, a terrain chunk's coarser level
  (`GLView::readsChartsOf`). Otherwise it is lit live: a moved receiver until
  its re-bake lands; one made movable, or drawing a level laid out apart, for
  as long as it is. Where the bake says shadow the forward pass takes it;
  where it says lit, the live map decides, so what moves still casts on a
  baked floor; and the cascaded sun's own filter softens its baked edges
  within its shadow distance. The rule is in `shaders/lightmap.glsl` and the
  forward fragment shader.

### Screen-space shadows

`GLScreenShadowPass` finds in the depth buffer what the cascades are too coarse
for - a foot on the floor, the crease under a ledge - for the cascaded sun alone
(`GLShadowData::sunLight`). It is Bend Studio's method (`shaders/screen_shadow`,
under its Apache-2.0 notice): each pixel marches toward the light the pixels a
contact's reach spans at its depth (`CONTACT_REACH`), at most `SAMPLE_COUNT`
([engine.md](../guides/engine.md#4-what-has-already-been-decided)).

It is a contact shadow, not a replacement for the cascades. The viewmodel band
casts and takes nothing; an `EyeOnly` mesh, which the shadow maps never see, does
cast here. The result goes into the AO target's alpha; the forward pass
multiplies the sun's visibility by it for the opaque bucket, and decals use it
for the sun on the surface under them. `screenShadows` turns it off.

## Image-based lighting (IBL)

`GLIBLBaker` (called from `GLBackend::render`, not a pass) keeps the IBL set
showing the scene's sky: an **environment cube** (what an HDR sky's skybox and
the captures draw; shading never reads it), an **irradiance cube**, a
**prefilter cube** (one roughness per mip) and the **BRDF LUT** (integrated once
at startup). An HDR sky (`Environment.sky.hdrPath`) bakes when the path changes;
the procedural sky when a sky parameter changes or the sun or moon moves past
`SkyParams::SAME_DIRECTION`.
A scene with no sky, or an HDR that fails to load, bakes nothing - it does not
keep the last scene's sky.

**Following the sun.** A sun that only drifted (within `SkyParams::DRIFT`) is
re-baked a step a frame into a second set of cubes, swapped in when complete
(`GLIBL`), more steps a frame while it runs behind
(`GLIBLBaker::MAX_CATCH_UP_STEPS`); the sky and the key light never trail.
Anything bigger bakes whole at once.

**The procedural sky** is Hillaire's sky and atmosphere, from four tables in
`GLAtmosphere`. Transmittance and multiple scattering depend on the air alone and
are redrawn when it changes (`GLIBLBaker::bakeAir`); the sky-view table and the
aerial-perspective volume follow the camera and sun and are computed every frame
by the Atmosphere pass. Surfaces take the aerial perspective with the fog
([rendering.md](rendering.md#fog)), scaled by `sky.aerialPerspective`.

**One sun lights the scene and the sky.** The sky's sun is
`Atmosphere::solarIlluminance`, from `sky.lightColor` and `sky.lightIntensity`,
and the sky's radiance is that times the atmosphere's integral with no constant
between them, so a brighter sun brightens the sky with it. `sky.intensity`
scales the skybox, the ambient and the air's light.

**The sun's place is authored on the `Environment`** (`sunElevation`,
`sunAzimuth`), because the sky must work with or without a directional light.
While the sky is procedural, `SkySystem` writes the key light's rotation, color
and intensity every frame - by day `Atmosphere::sunlight`, which reddens at a
low sun; by night the `night.moonlight*` pair. Author those on the environment,
not the `Light`: an edit to the light is overwritten next frame
([editor.md](editor.md#inspector-cards)). Shadow settings stay the light's.

**Night.** Below the horizon a skyglow and a moon lobe take over across
`NightSkySettings::TWILIGHT_DEGREES` either side of it (`shaders/sky.glsl`,
shared by the bake and the skybox), and the key light fades across the same
band. The moon sits opposite the sun, tilted by `moonTilt`. Stars are drawn by
the skybox only; the prefiltered cubes would smear them.

## Forward+ clustering

The view frustum is diced into `CLUSTER_X` x `CLUSTER_Y` screen tiles by
`CLUSTER_Z` exponential depth slices. The ClusterCull pass lists the lights
touching each cluster (a point as its sphere, a spot as its cone), and the
forward pass shades a pixel against its cluster's list rather than every light -
which is why `MAX_LIGHTS` can be generous. A list holds at most
`MAX_LIGHTS_PER_CLUSTER`; lights past it are dropped, which shows as a cut, and
the Light Clusters view draws such a cluster magenta. The cull is one invocation
per cluster testing every light, so finer clusters cost the cull; coarser ones
put more lights in each list, nearer the cap.

## Limits and the shader prelude

Cross-cutting limits live in `src/engine/core/engine_config.h` (`Config::`), and
GLSL cannot read a C++ header. `GLBackend::shaderConstants` writes them out as
GLSL from the C++ constants themselves, and `Vkm::GL::setShaderPrelude` puts that
text under the `#version` of every stage, graphics and compute. So a shader just
uses `MAX_LIGHTS`, with nothing to include and nothing to keep in step. The same
carries the bindings ([rendering.md](rendering.md#shader-binding-contract)), the
render-mode and tonemap enums, and constants such as the twilight band and the
atmosphere's geometry and table sizes.

Never re-define a prelude constant in a shader - GLSL rejects the redefinition,
which is what keeps a shader from holding its own copy. `Config::NUM_CASCADES`
and `Config::SHADOW_NEAR` are not in the prelude; the shader reads them from the
shadow block.

## Key files

- `src/engine/ecs/component/render/light.h` - `Light`, `findKeyLight`
- `src/engine/system/render/data/light_data.h` - `LightData`, `lowestSlotDirectionalCaster`
- `shaders/forward/pbr/fragment.shader`, `shaders/brdf.glsl` - the shading model
- `shaders/area_light.glsl`, `shaders/form_factor.glsl` - area lights
- `shaders/shadows.glsl` - shadow sampling, bias, soft shadows, cascades
- `shaders/ambient.glsl`, `shaders/probes.glsl`, `shaders/irradiance_volume.glsl` - the ambient
- `src/backend/opengl/frame/gl_shadow_data.h` - `GLShadowData`
- `src/backend/opengl/storage/gl_shadow_atlas.h` - `GLShadowAtlas`
- `src/backend/opengl/offline/` - `GLIBLBaker`, `GLIrradianceBaker`, `GLProbeBaker`, `GLSceneCapture`, `GLCaptureShadow`, `GLLightmapBaker`, and `GLCasterDepth`, which both offline shadow maps draw their casters through
- `src/backend/opengl/storage/gl_lightmaps.h` - `GLLightmaps`
- `shaders/lightmap.glsl` - reading the bake
- `src/backend/opengl/storage/gl_atmosphere.h` - `GLAtmosphere`
- `src/engine/system/sky/` - `SkySystem`, `Atmosphere`
- `src/engine/core/engine_config.h` - the limits
