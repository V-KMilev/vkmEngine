# Lighting

The engine supports five light types: **Directional**, **Point**,
**Spot**, **Rect** (rectangular area light), and **Disk** (disk area
light). All are data-only ECS components; light evaluation lives in
the PBR shader. Shadows go through a shared shadow atlas (2D, for
directional and spot) and a cube map per shadowed point light. Image-based
lighting is baked by a persistent `GLIBLBaker` helper (not a pass) from an
HDR environment map or the procedural sky, and sampled by the shader
through irradiance, prefilter, and BRDF LUT maps.

## Light component

`src/engine/ecs/component/render/light.h`:

```cpp
enum class LightType {
    Directional = 0,    // sun, no position, only direction
    Point       = 1,    // omnidirectional, has position and radius
    Spot        = 2,    // cone, has position, direction, and angles
    Rect        = 3,    // rectangular emitter; faces along -direction
    Disk        = 4     // disk emitter;        faces along -direction
};

struct Light {
    LightType type           = LightType::Directional;
    glm::vec3 color          = {1, 1, 1};
    float     intensity      = 1.0f;

    // Point / Spot / area
    float     radius         = 10.0f;     // cutoff distance / attenuation radius

    // Spot
    float     innerConeAngle = 0.5f;      // radians; full brightness
    float     outerConeAngle = 0.785f;    // radians; falloff edge

    // Area (Rect, Disk)
    float     areaWidth      = 1.0f;      // Rect: width along local X
    float     areaHeight     = 1.0f;      // Rect: height along local Y
    float     areaRadius     = 0.5f;      // Disk: radius
    bool      twoSided       = false;     // emit from both faces

    // Shadows
    bool      castShadows    = true;
    float     shadowBias     = 0.5f;      // compare slid toward the light, in shadow texels, more grazing
    float     shadowNormalBias = 1.0f;    // point moved off the surface, in shadow texels, none head-on
    float     shadowDistance = 100.0f;    // directional only: cascade coverage distance (world units)
    float     sourceRadius   = glm::radians(0.5f); // directional: disc radius (radians); spot, point: emitter radius (m) - the penumbra

    bool      enabled        = true;
};
```

Position comes from the entity's `Transform` for Point, Spot, and area
lights. Direction is the entity's forward, its rotated -Z axis. A spot
shines along `direction`; an area light emits from the opposite face,
along `-direction` - toward the rotation's local +Z.

## Lighting model

The PBR fragment shader (`shaders/forward/pbr/`) implements:

- **Lambertian diffuse** with energy conservation.
- **Cook-Torrance specular**: GGX (Trowbridge-Reitz) for normal
  distribution, height-correlated Smith for visibility, Schlick for
  Fresnel. The direct lobe is scaled by the energy its microfacets bounce
  more than once (`1 + f0 (1 / Ess - 1)`, read from the split-sum LUT), so
  a rough metal is as bright under a lamp as under the sky. Shading never
  goes smoother than `MIN_ROUGHNESS` (0.045, `shaders/brdf.glsl`), which keeps
  the GGX denominator off zero; the peak there, 1 / (PI alpha^2), is about
  77,600 and is not floored, so a polished floor shows a point light's glint
  at full height. The sun's is lower, because its lobe is widened by its disc
  (below): about 13,100 at the default `sourceRadius` of half a degree. The key
  light is not a point but a disc (`Light::sourceRadius`, the angular radius
  its shadows are softened by too): its lobe, and its clear coat's, is widened by
  the disc's tangent with no renormalisation, so a mirror reflects a disc as
  bright as the irradiance it brings rather than a pinpoint. What the pass
  writes is held under the RGBA16F maximum, so no channel reaches infinity
  for bloom to spread. A point or spot light is a sphere of its `sourceRadius`
  (Karis 2013): its highlight is shaded toward the sphere's point nearest the
  reflection ray, the lobe renormalised by `(a / a')^2` with `a' = a + r / 2d`,
  so a bulb's glint on a polished floor is as wide as the bulb's reflection
  rather than a sub-pixel firefly no temporal filter averages, and one radius
  sizes the highlight and the penumbra alike. A radius of zero is the point.
- **Optional lobes**, gated at runtime by each material's feature flags
  (one shared PBR program, no compiled `#ifdef` variants): transmission,
  volume (absorption), clearcoat, anisotropy, subsurface, sheen,
  parallax/height, alpha test. Transmission blends only what the surface does
  not reflect toward the scene behind it, itself dimmed by the reflected share
  (Filament's split), so glass keeps its reflections and highlights. Subsurface is Unreal's two-sided foliage, a wrapped
  back-light through a broad lobe about the light's direction, so a leaf glows
  looking toward the sun and not with it behind.
- **Double-sided materials** (`MaterialAsset::doubleSided`, glTF's
  `doubleSided`): their runs draw with culling off in the depth prepass and the
  forward pass, and a back face is lit as its own - the shader turns the whole
  tangent frame on `gl_FrontFacing`, so the normal map mirrors with it, and the
  prepass writes the turned normal.
- **IBL**: prefiltered specular cube + irradiance cube + split-sum
  BRDF LUT. The LUT integrates Schlick's Fresnel over the lobe (A over
  `1 - Fc`, B over `Fc`), so the reflected energy is `f0 * A + B`; a
  view-angle Fresnel in place of `f0`, as the multiple-scattering paper's
  listing has it, would count the Fresnel twice. The diffuse takes what is left,
  `1 - (FssEss + Fms Ems)` (its section 4). A rough lobe is read along its
  dominant direction, which bends from R toward N as roughness grows (Lagarde,
  *Moving Frostbite to PBR* 4.9.3), so a rough floor at grazing does not
  reflect the bright horizon R points at. The split-sum term is followed by a **multiple-scattering** lobe (Fdez-Aguera), built from the two
  DFG channels already sampled: single scattering only accounts for light that
  leaves the microsurface after one bounce, and at high roughness most of it
  leaves after several, which is why a rough metal without it renders visibly
  too dark. Under a uniform environment a perfect metal returns exactly the
  energy it received at every roughness; a dielectric gains under a thousandth
  and roughness 0 is untouched, so it brightens rough metal and nothing else.
  The LUT is integrated with the same height-correlated visibility the
  direct lights use, so the two lobes are one lobe.
- **Where the ambient comes from**: the reflection is the covering
  reflection probes, parallax-corrected, over the global sky. They are taken
  smallest box first, each covering only what the ones before it left (Unreal's
  rule), so a room's probe wins inside the room over the yard's around it,
  whatever their scene order. The diffuse is the irradiance volume wherever one covers the point -
  fading in over `blendDistance` metres from each face of its box -
  and the reflection's own source (probe, else sky) elsewhere - a probe's
  irradiance is one sample at its centre, the volume's is taken here - offset
  off the surface (`irradianceVolumeLookup`) along its geometric normal, so a floor reads the
  room above it rather than the probes inside its own slab (DDGI's surface
  bias). The volume stores SH-L1; its lookup adds the quadratic zonal term L1
  predicts (Activision's ZH3), so light bounced from one side gives a pillar's
  two sides the contrast L1 alone flattens. Inside
  the volume the reflection is **normalised** (Lazarov, *Black Ops 2*): it
  is dimmed by the ratio of the irradiance here to the irradiance its capture
  saw, so a floor in shade stops reflecting the open sky at full strength.
  The ratio never exceeds one: a sunlit floor's extra light is the sun's, and
  the sun is not in the reflection.
  With no sky baked there is no reflection to read and the diffuse is a flat
  floor (`FLAT_AMBIENT` in `shaders/ambient.glsl`); probes and the volume
  still replace both wherever they cover the point, since they were baked
  from the scene rather than from the sky.
- **A probe's `intensity`** scales what it contributes, its reflection and its
  irradiance alike; how much of the point it covers is its box's alone.
- **Screen-space reflections** over both, on surfaces smoother than
  `ssrMaxRoughness`, in the Reflections pass after the frame is lit. The
  forward pass writes, beside the colour, how much of the environment's
  reflection each pixel shows (the split-sum weight - not occluded, since a ray
  that hits has found the occluder) with its roughness, and the reflection as it
  added it - weight times specular occlusion times the environment's radiance,
  stored as that product so the pass subtracts what was added whatever the
  weight rounded to. Under MSAA the resolve averages
  both with the weights it averages the colour with (Karis's, in [rendering.md](rendering.md)),
  so the reflection it subtracts is the one the resolved colour holds; a plain
  average there leaves a dark fringe along every bright silhouette. The pass traces each such pixel's ray through this
  frame's depth, and where it meets a surface facing it, adds weight x traced
  minus that product: the scene's colour replaces the probe or sky reflection
  there, and nothing changes where the ray found nothing. The colour is read from a mip chain of the lit frame at the level the
  glossy lobe spans at that distance, and a glossy pixel averages its
  neighbours' rays on the same surface, which is what makes one ray per pixel
  a smooth lobe and a soft edge without a temporal filter. The ray follows the
  G-buffer's interpolated normal, not the normal map, because one ray per pixel
  reads a normal map as noise. A transparent surface or an alpha-blended
  particle dims the weight and the reflection under it by its own opacity, so
  a floor seen through glass or smoke keeps its reflection at the strength
  the glass or smoke lets through; an additive particle hides nothing and
  leaves both alone. An alpha-masked cutout reflects its probe and the sky
  and is never traced: the prepass does not draw it, so the G-buffer under it
  is the surface behind, and its ray would leave along that surface's normal. The environment's intensity scales the
  probes, the irradiance volume and the sky where they are read, not the
  traced reflection, which is the scene as lit - and so the probe and volume
  captures are taken at unit intensity, or they would carry it twice. With
  no sky baked there is no environment reflection to replace: the forward
  pass still writes the weight, with nothing added under it, and the traced
  reflection is all a glossy surface shows.
- **GTAO**: full-res ground-truth ambient occlusion computed by
  `GLGTAOPass`, then denoised by an edge-aware 5x5 blur on each pixel's own
  surface - spatial only, since nothing here keeps a history. The blur
  averages the integrated visibility as it is, and only then are intensity,
  power and the clamp to one applied: a slice meeting an open surface
  obliquely integrates to more than one, its neighbours' to less, and clamped
  first the average would darken surfaces nothing occludes. The stronger of it and the material's AO occludes the
  indirect term - they usually describe the same crease, so multiplying them
  would darken it twice. Diffuse takes it with what bounces back out of the
  crease (Jimenez's multi-bounce fit, in the visibility and the albedo, so a
  white corner darkens less than a black one), and specular takes the view-
  and roughness-aware form of it (Lagarde). GTAO's bent normal is carried onto the shading normal
  as an offset, so a normal map still shapes the indirect light. Only the
  opaque bucket takes it: GTAO reads the depth prepass, which draws nothing
  else, so under an alpha-masked or transparent pixel it describes the surface
  behind - or the sky - rather than the one being shaded.
- **Horizon occlusion**: a normal map can turn a reflection below the
  geometric surface, where the environment holds light nothing there could
  reflect; the specular occlusion fades it out.

### What a capture is lit by

The irradiance volume and the reflection probes are captures of the scene
(`GLSceneCapture`): the opaque scene and the sky drawn into a cube around each
probe, which the volume projects to SH-L1 and a reflection probe convolves
(how, in [rendering.md](rendering.md#the-passes-fixed-order)). Whatever lights the
captured surfaces is what the probes pass on, so a wall lit by the open sky
would light every probe in a sealed room through solid plaster. A captured
surface takes its ambient from the volume wherever the volume covers it, and
from the sky only outside its box:

- **The volume is gathered in bounces**, the recursion DDGI runs across frames
  (Majercik et al., *Dynamic Diffuse Global Illumination with Ray-Traced
  Irradiance Fields*: a ray's hit is shaded by the probes' previous
  irradiance), run here to a fixed count at bake time. `GLIrradianceBaker`
  gathers the grid `GLIrradianceBaker::BOUNCES` times. In the first, the
  captured surfaces inside the box read a grid that is black, so a wall is
  lit only by what reaches it - the lights, the key light through its shadow,
  its own emission. Each later gather lights them from the grid the gather
  before produced, kept in a second volume, and adds one bounce. A direction
  that meets no geometry sees the sky itself, so the sky reaches a room
  through its doorway and windows and nowhere else, and a sealed room under a
  noon sky stores nothing. How many gathers, and what they cost:
  [engine.md](../guides/engine.md#4-what-has-already-been-decided).
- **Inside the box the volume is all there is**: a capture reads it with no
  fade at the box's faces, so a wall on a face takes none of the sky, and the
  gathers read it at unit intensity, since the frame scales the finished grid
  by `intensity` and a gather lit at it would compound it once per bounce. A
  captured glossy surface's reflection of the sky is normalised by the volume
  as the frame's is (Lazarov, above), so in the first gather it reflects none.
- **Only the first gather judges** which probes stand inside geometry; the
  verdict depends on where a probe stands, not on the light, so the later
  gathers skip those probes and the backface mask, and dilation repairs the
  refused cells after every gather.
- **The reflection probes are captured after the volume**, lit by it where it
  covers them, at its `intensity` and with no fade, so a room's probe reflects
  its walls as the frame lights them rather than lit by the sky. A probe
  remembers which bake of the volume lit it (`GLIrradianceVolume::bakeId`) and
  is captured again when the volume is.

The frame does fade the volume in over `blendDistance` from each face of its
box, toward the light outside it. A volume filling a room wants its box out to
the middle of the walls and a fade shorter than their half-thickness; with a
longer one, the room's surfaces near a face of the box - its corners above all -
take part of the sky.

### Area lights: LTC + representative point

Rect and Disk are evaluated using two industry-standard tricks:

1. **Diffuse via LTC (Linearly Transformed Cosines).** The shader sums
   Hill's stable edge integral over the emitter's outline into its vector
   form factor (`shaders/ltc.glsl`), then clips it to the shading horizon by
   Hill's approximation, the sphere with the same vector (close while the
   emitter is wholly above the horizon, zero once it is wholly below). No
   matrix LUT is sampled, and the frame the outline is taken into is built
   from the normal alone: the cosine lobe has no preferred direction about it.
   - Rect uses 4 vertices.
   - Disk is approximated by a 12-vertex polygon. The error is
     negligible for the budget.
2. **Specular via Karis representative-point.** For each shaded pixel,
   pick the point on the emitter closest to the perfect reflection
   direction, then evaluate the surface's own GGX lobe toward it,
   renormalised by `(alpha / alpha')^2` - `alpha'` the lobe broadened by the
   emitter's size - so a wide emitter spreads its highlight rather than
   brightening it. That factor stands in for the broadened lobe's own
   normalisation, so the lobe itself is evaluated at `alpha`.

`intensity` is the emitter's point-equivalent intensity: its radiance is
`intensity / area`, so far away an area light lights like a point light of
the same intensity and near it the form factor takes over. `radius` only
windows the light to zero; the falloff is the emitter's own geometry.

`twoSided` enables emission from both faces. By default an area light
only emits from the side its `-direction` normal points along; which side
a point is on is a plane test, not the sign of the integral.

Area lights cast no shadow: the shadow planner fits cascades, spot tiles
and cube faces, and nothing for Rect or Disk.

## Shadow atlas

The shadow system has two depth stores sized by `engine_config.h`:

- **2D atlas** (`Config::MAX_SHADOW_CASTERS_2D` tiles) holds
  directional and spot shadow maps. It is one depth `Texture2D`, not a
  texture array, `SHADOW_ATLAS_BLOCKS` x `SHADOW_ATLAS_BLOCKS` square
  blocks each the largest tile (`shadowResolution`) across; a caster's slot
  indexes one square tile and is sampled through a per-tile UV
  offset/scale. The first shadow-casting directional light takes
  `Config::NUM_CASCADES` consecutive slots for a CSM (cascaded shadow
  maps) split; spot lights take the slots left over. Tiles differ in size,
  each a power of two (`GLShadowData::build`): the sun's first
  `FULL_CASCADES` cascades take the largest tile and the rest take half,
  whose texels already span more of the world; a spot whose reach misses the
  view takes no slot at all, and one that is seen takes
  `SPOT_TEXELS_PER_PIXEL` texels per screen pixel its cone's bounding sphere
  covers (Unreal's `r.Shadow.TexelsPerPixelSpotlight`), from
  `SHADOW_ATLAS_MIN_TILE_RES` to the largest, halving only once it needs
  well under its tile so it does not flip between two sizes. When the
  tiles would not fit, the largest spot halves until they do.
  `GLShadowAtlas::layout` packs them largest first into the smallest free
  square, quartering squares as it goes, which never fails while their area
  fits; the same sizes land in the same places, so a held tile stays held.
- **A cube-map array** (`Config::MAX_SHADOW_CASTERS_CUBE` layers) holds
  the six faces of each point-light shadow, a layer per light, so every point
  light reads through one unit and its slot is a coordinate rather than a
  sampler index.

Shadow rendering goes through `GLShadowPass`, which:

- Runs whenever there is a shadow job, casters or none: a tile or face
  with nothing in it is still cleared, so it reads as lit, and the atlas
  and cubes start cleared to the far plane for the same reason. The 2D
  half returns early only on an empty job list and the cube half loops
  over whatever cube jobs exist. A spot tile or a cube face is kept between frames when it would
  be the same picture: the cull signs each batch with a hash of the
  matrix it was culled against and every survivor's mesh id, geometry upload
  (`GLMesh::uploadId`, so a mesh re-cooked under the same handle is a new
  picture) and model - and for an alpha-masked survivor its material's and
  albedo map's uploads, since they say which texels cast
  (`ShadowCasterBatch::signature`) - the atlas remembers
  the signature a tile was last drawn from - recorded as the tile is
  cleared (`GLShadowAtlas::beginTile` / `beginCubeFace`), not when it is
  asked about (`tileHolds` / `faceHolds`), so a tile is held only once
  something drew it - and a match skips the clear and the draws. A replaced world
  or asset graph makes the atlas forget every tile (`forgetHeld`, from
  `GLBackend::onWorldReplaced`), and so does a shader reload
  (`GLBackend::reloadChangedShaders`), since a held tile was drawn by the old
  program. A posed survivor leaves the
  batch unsigned, since its picture is in its bones, and the sun's
  cascades are never signed: they move with the camera, so one is the same
  picture only while the camera holds still, and a frame that must afford
  redrawing them as it moves saves nothing it budgets for when it does not.
  A cascade is drawn with `GL_DEPTH_CLAMP` on and culled with no near plane,
  so a caster nearer the sun than the cascade's near plane is flattened onto
  it and still shadows, rather than being clipped away.
  On a still map with static lights only the sun's cascades redraw;
  `Shadow/TilesDrawn` plots how many tiles a frame draws.
- Cuts an alpha-masked caster's shadow by its material: such a caster draws
  through `shadow/depth_masked` (and its skinned twin), which tests the
  material's alpha times its albedo map's against `alphaCutoff` exactly as
  the forward pass does, so a leaf card shadows as a leaf and not as a quad.
  The cull groups those casters by (material, mesh) rather than mesh alone,
  so a run binds one material; cascades, spot tiles, cube faces and the
  captures' key-light map all draw them this way. A transparent caster is
  drawn solid - its shadow is its whole shape - and one that should cast
  none turns `Mesh::castShadows` off.
- Takes the first `casterCount` objects of `RenderObjects::scene` - the
  casters, which the gather lists first - from the **whole scene** rather than
  the camera's visible set (see
  [Visibility](visibility.md)), then
  culls that list per atlas tile against the tile's own light frustum in
  `GLShadowData::cullCasters`, so a tile draws only what can reach it.
- Draws against per-caster `lightVP` matrices, biases, and atlas tile rects
  that `GLShadowData::uploadAndBind` writes into the ShadowBlock UBO before
  the passes run. The forward pass samples a single tiled `sampler2DShadow`
  atlas (`u_shadowAtlas`) - mapping each caster's UV into its tile rect
  and taking a 3x3 kernel of hardware depth compares, each of them
  bilinear over a 2x2 texel neighbourhood, so one tap returns a fraction
  rather than 0 or 1 - plus the point lights' cube array, through a
  `samplerCubeArrayShadow` (`u_shadowCube`, the light's slot as its layer). A cube
  face is an ordinary perspective depth map, drawn by the same programs as
  the atlas tiles, and a lookup rebuilds the face's projected depth from the
  major axis of the direction and takes a hardware compare over 2x2 texels
  of the face - a fraction, like a 2D tap.

  **Bias.** Every light reads `shadowBias` and `shadowNormalBias` one way, in
  texels of its own map at the receiver (`biasSin` and `biasSlide` in
  `shaders/shadows.glsl`, after Castano's summary for The Witness): the point
  moves off its surface along the normal by the normal bias times the sine of
  the light's angle to it - nothing head-on, all of it where the light grazes -
  and the compare slides toward the light by the depth bias times 1 + the
  tangent, capped. A texel is the map's world size at the receiver: a
  cascade's own, a spot's grown with the distance from the light, a cube face's
  `2 * axis / SHADOW_CUBE_RES`. So one pair of values holds for every light
  type, range, cascade and distance, and the defaults rarely need touching
  (why these, [engine.md](../guides/engine.md#4-what-has-already-been-decided)).

  **Soft shadows.** The sun and spot lights cast percentage-closer soft
  shadows (Fernando 2005), sized by the light's own `sourceRadius` - the one
  control, with no switch beside it: a source of zero is a hard shadow and
  takes the 3x3 path. A 16-tap blocker search through the raw
  atlas, each tap a gather of four texels weighted bilinearly so the result
  does not step from texel to texel, finds the average depth of what
  shadows the point; the distance behind it, scaled by the source's size,
  is the penumbra; and a Vogel disk of hardware compares that wide filters
  it. A point far behind its blocker gets a wide, soft edge and a contact
  stays sharp, which is what a light of that size does. The taps grow with
  the disk - 12 for a narrow penumbra, up to 64 for the widest - so they stay
  `SOFT_TAP_SPACING` (1.25) texels apart and each tap's 2x2 compare overlaps
  its neighbours'. The disk is the same at every pixel: turned per pixel on
  noise it would be grain that only a temporal filter averages away, and
  this engine has none, so neighbouring pixels read nearly the same taps and
  the edge is a smooth ramp. Each of the sun's taps
  compares against the receiver's own plane at that tap rather than its
  depth at the point (`receiverSlope`), so a surface the light grazes
  neither finds itself in the blocker search, which would pull every
  penumbra toward zero, nor shadows itself under a wide filter. A spot's taps
  do the same through its perspective tile (`receiverSlopePerspective`), and
  so do the hard path's nine. Each search reads its centre too, so a caster
  a texel thin is not lost between taps.

  How wide a penumbra can be is derived, not set. A disk wider than `SOFT_REACH`
  (5.5) texels - the most 64 taps cover at that spacing - would spread its taps
  into dots, so the sun's path (`sampleCSMSoft`) runs its search and its filter
  each in the finest cascade that holds the point and fits the disk in that many
  texels: a penumbra too wide for the point's own cascade is drawn from a
  coarser one, whose texels are about the fourth root of `shadowDistance` times
  larger (three at the default; twice that for the last, half-size cascade), and
  which loses only detail narrower than the penumbra blurring it. The search
  reaches one cascade radius above the point, and the penumbra is capped at the
  radius it searched: past that rim the point reads lit, so a wider filter would
  tear a jagged edge into the penumbra. Whatever cascade is read, the slide is
  the point's own in metres - a depth unit is a cascade's whole span, so the
  coarse one's would lift the point clear of anything a few metres above it -
  while the normal offset is the read tile's, which its texels need against
  acne; and a search the coarse tile saw nothing in falls back to the point's
  own tile, so a caster too thin for the coarse map still shadows; that fallback
  keeps its full width, faint enough to hide the hard compare that admits it.
  A spot has one map and nothing coarser, so its search and filter stop at
  `SOFT_SPOT_REACH` (7) texels, its taps a little further apart, each compared
  against the receiver's plane where it falls (`receiverSlopePerspective`: a
  perspective tile's depth is affine in its uv over a plane, so two points of
  the plane give the slope exactly).

  A point light is soft the same way, from its `sourceRadius` in metres. Its
  taps are laid on a disk across the light's ray, and each compares the
  receiver's plane where its own ray meets it (`cubeReference`), so a wide
  filter neither finds the receiver among its blockers nor shades it. The
  blocker search reads the same array a second time on its own unit
  (`u_shadowCubeRaw`, uncompared), as the atlas's does. A source of zero is a
  hard edge, filtered a texel wide rather than one 2x2 compare, which stair-steps
  once a texel outgrows a pixel. The fog's sun samples stay on the 3x3 kernel.

  A directional light's highlight is the same disc as its penumbra, and the
  procedural sky draws the sun at that size too (as Unreal's sky draws its
  light's source angle), at `SkySettings::sunDiscIntensity` times
  `sky.lightIntensity`, so one light sets the sun's size and brightness
  everywhere.

  The atlas is read both ways within a frame, so the comparison lives on
  a sampler object bound to a texture unit rather than on the texture.
  `GLShadowAtlas::bind2D` binds a comparing sampler on
  `ShadowTextureSlots::ATLAS_2D`; `bind2DRaw` binds a non-comparing one on
  `ShadowTextureSlots::ATLAS_2D_RAW`, for the soft path's blocker search and
  the `ShadowAtlas` debug view, both of which read stored depth through a plain
  `sampler2D`. Two units, because a sampler replaces
  every sampling parameter for the unit it is bound to - which is also
  why both samplers carry the atlas's own linear filtering and clamp.

`shadowDistance` controls how far the directional cascades cover in world
units, and the sun's shadow fades out over the last fifth of it
(`SHADOW_FADE`), so its reach ends in a ramp rather than a line across the
ground. It is ignored for spot, point, and area lights, which use `radius`
as their cutoff.

A cascade hands over to the next across the last tenth of its depth
(`CASCADE_BLEND` in `shadows.glsl`): a point there is read in both and the two
mixed, on the hard path, the soft one and the fog alike. Read alone up to its
split, a cascade would end in a line across the ground where the texel size
steps, and with no temporal filter that line stands still on screen. Only the
band pays for the second read.

The four cascades are split **logarithmically**, anchored at a fixed world
distance (`CASCADE_NEAR`, 1 unit) rather than at the camera's near plane.
The anchor is what stops `shadowDistance` from trading away foreground
sharpness: every boundary then grows as a fixed power of the distance - the
first as its fourth root - so the near cascade stays small as the dial rises:
its far edge moves 2.5 -> 4.9 units across a `shadowDistance` of 40 -> 600.
Anchored at the camera's near plane (0.2) instead, the split would put its
first boundary about a metre out and spend a whole cascade on the ground at
the viewer's feet.

Pulling the near cascade in leaves the outer three covering more range each,
so the far field loses density as `shadowDistance` grows: total coverage
against texel density is a fixed budget, and the logarithmic split spends it
evenly instead of concentrating the shortfall in the foreground.

## Image-based lighting (IBL)

A persistent `GLIBLBaker` keeps the IBL product set showing the scene's sky (a
helper invoked from `GLBackend::render`, not a pass). An HDR sky -
`Environment.sky.hdrPath`, an equirectangular image - is baked when the path
changes. The procedural sky is baked again when a sky parameter changes or the
sun or the moon moves by more than about half a degree
(`SkyParams::SAME_DIRECTION`), so a sun animated a fraction of a degree a frame
is not a bake a frame - and while the sun only drifts, a step a frame (see
"Following the sun" below). A scene that names
neither, and an HDR that fails to load, leave no environment baked: the ambient
term and the skybox then draw what a scene with no sky draws, rather than the
last scene's sky. The reflection probes and the irradiance volume are captures
of the scene under the sky they were baked with, and a sky change does not bake
them again; bumping their `bakeVersion` does - the volume's takes the probes
with it, since they are lit by it. It produces:

- The **environment cube**: what an HDR sky's skybox and every capture's sky
  draw, and what the other two cubes are convolved from. Shading never reads it:
  a mirror reflection is the prefilter cube's sharpest level.
- The **irradiance cube**: diffuse contribution.
- The **prefilter cube**: specular contribution, with one roughness
  level per mip.
- The **BRDF LUT**: split-sum lookup for the analytic specular term. It
  depends on no environment, so it is integrated once when the backend starts
  (`GLIBLBaker::integrateBrdf`), and a scene with no sky reads it too: the
  direct lights' energy compensation and the reflection weight the
  screen-space reflections replace through both come from it.

**The procedural sky** is Hillaire's production sky and atmosphere (EGSR 2020,
"A Scalable and Production Ready Sky and Atmosphere Rendering Technique"), as
Unreal's SkyAtmosphere renders it: a few small tables, from which the sky drawn,
the environment's lighting and the air in front of the scene are each
integrated. The four live in `GLAtmosphere`. Two depend on the air alone and are
drawn again only when it changes (`GLIBLBaker::bakeAir`): the atmosphere's
**transmittance** (`shaders/atmosphere/transmittance`,
`GLAtmosphere::TRANSMITTANCE_WIDTH` by `TRANSMITTANCE_HEIGHT`, Bruneton's
parameterisation of altitude and view angle), and from it the **multiple
scattering** (`shaders/atmosphere/multiscatter`,
`GLAtmosphere::MULTISCATTERING_SIZE` square, by altitude and the sun's angle):
Hillaire's Psi_ms, the second order gathered isotropically from a sphere of
directions and the orders past it summed as a geometric series. Two follow the
camera and the sun and are computed every frame by the Atmosphere pass
([rendering.md](rendering.md#the-passes-fixed-order)): the sky-view table and
the aerial-perspective volume, below.

The sky's radiance and the aerial perspective march the same way (`integrateAir`
in `shaders/atmosphere.glsl`): at each sample it adds the sun's light
single-scattered by Rayleigh and Mie, the sunlight read from the transmittance
table and zero where the planet hides the sun, plus the multiple-scattering
table times the air's scattering. Each step is integrated over its length
against the transmittance falling across it (Hillaire's energy-conserving step),
so a long step neither overshoots nor darkens. The sky (`skyRadiance`) marches
each view ray from the eye, at `Atmosphere::EYE_ALTITUDE`, in `SKY_STEPS` steps
spaced quadratically - short in the dense air near the eye, long where a horizon
ray runs a thousand kilometres. The air is Rayleigh, Mie and an ozone layer that
absorbs without scattering - a tent of density peaking at
`Atmosphere::OZONE_ALTITUDE` - which keeps the twilight sky overhead blue. A ray
that meets the planet ends there and sees the ground, diffuse at
`Atmosphere::GROUND_ALBEDO` in the sunlight that reaches it, through the air
between; below the horizon is that ground rather than black, so what faces down
has sky light too. The ground's albedo feeds the multiple scattering as well
(why its value,
[engine.md](../guides/engine.md#4-what-has-already-been-decided)).

**The sky drawn** is the **sky-view table** (`shaders/atmosphere/sky_view`,
`GLAtmosphere::SKY_VIEW_WIDTH` by `SKY_VIEW_HEIGHT`): the sky's radiance from
the eye for every view, in Hillaire's latitude/longitude parameterisation
(`skyViewUnit`). The sky is symmetric about the sun's vertical plane, so the
longitude is the azimuth from the sun's, over half the circle and densest toward
the sun; the latitude runs from the zenith to the horizon over half the table
and on to the nadir over the other, densest at the horizon from either side, so
the ground's edge stays sharp where a cube's texels would blur it. It is
computed every frame, so the sky drawn is always the current sun's, even while
the IBL is still following it. The skybox reads it and adds what night adds
(`skyNightGlow`), the sun disc, the moon and the stars. The env cube's bake
(`shaders/ibl/sky`) marches the same integral into the cube's texels.

**The air in front of the scene**, aerial perspective, is the
**aerial-perspective volume** (`shaders/atmosphere/aerial_perspective`,
`GLAtmosphere::AERIAL_PERSPECTIVE_SIZE` froxels across, down and deep -
Hillaire's camera volume): for each froxel of the view, the light the sky's air
scatters toward the eye in front of it and the mean of its transmittance. Its
slices are squared in view depth (`aerialSliceToViewDepth`) out to the camera's
far plane or `GLAtmosphere::AERIAL_PERSPECTIVE_REACH`, the nearer, so the near
ones are metres deep and the far ones kilometres. Each froxel marches from the
eye to its own depth through `integrateAir`, over its distance scaled by
`sky.aerialPerspective` (Unreal's aerial perspective distance scale: 1 is the
planet's air, 0 turns it off), and its light is the sun's at the sky's
`intensity`, as the skybox draws the sky. Every surface drawn through the air
takes it where it takes the froxel fog, at its own depth and beneath the fog
([rendering.md](rendering.md#fog)), so a distant hill fades into the horizon the
sky draws behind it (how much, and why the default,
[engine.md](../guides/engine.md#4-what-has-already-been-decided)). The sky holds
its own air and takes none, and an HDR sky has no atmosphere to apply.

**Following the sun.** The env cube and its convolutions are too slow for one
frame, so a procedural sky baked whole each time the sun moves half a degree
would drop a frame every few seconds of a day-night cycle. A sky whose sun and
moon have only drifted since the last frame - by no more than
`SkyParams::DRIFT`, every other term the same - is baked a step a frame instead
(more while the sun has run ahead of the sky shown: a step more per 1.5 degrees behind, up
to 12, so a time-lapse's light keeps pace),
as Unreal time-slices its real-time sky capture: the env cube's six faces, then
its mips with the prefilter's mirror level, then each irradiance face in
`GLIBLBaker::IRRADIANCE_SLICES` shares of its azimuths, added together, then
each prefilter level a face at a time, the largest in bands of
`GLIBLBaker::BAND_TEXELS` texels - `GLIBLBaker::slicedSteps` steps in all.
`GLIBL` holds two sets of the three cubes: shading reads one while a bake fills
the other, which is swapped in once complete, so no frame reads a half-made set,
and the next bake starts from wherever the sun is then. The ambient light and
reflections so trail a moving sun by up to two bakes' worth of frames, while the
sky drawn and the key light never do. The first sky, a changed value and a jump
past `DRIFT` are baked whole at once, as an HDR is: a probe or the irradiance
volume captured in the meantime would otherwise picture a sky the scene has
left. What each step costs, and the whole bake,
[engine.md](../guides/engine.md#4-what-has-already-been-decided).

**One sun lights the scene and the sky.** The sky's sun is
`Atmosphere::solarIlluminance`: `sky.lightColor` times `sky.lightIntensity`,
divided by the air's transmittance straight down - the sun above the air that,
overhead, arrives as the scene's key light. Sky radiance is that times the
atmosphere's integral, with no constant between them. So raising the sun
brightens the sky with it, and the sky's light on the ground keeps the share of
the sun's that the air gives it - measured, and why no constant,
[engine.md](../guides/engine.md#4-what-has-already-been-decided).
`sky.intensity` scales the skybox, the ambient and the air's light in front of
the scene.

**Where the sun is** is authored on the `Environment`, as `sunElevation` and
`sunAzimuth` in degrees, and nowhere else. The sky is scene-global and has to
work whether or not a scene has a directional light, so it cannot read one; and
a sky disagreeing with the light casting the shadows looks broken in a way that
is hard to diagnose. `SkySystem` (Simulation stage) resolves that by pointing
the scene's key light - `findKeyLight`, the lowest-slot entity carrying an
enabled directional `Light`, which is the one definition of that rule - from
those same angles. A disabled light is not the key: it lights nothing, and the
frame keys its shadows by the next directional, so that is the one the sky
aims.

So with the procedural sky on, the key light's **rotation, colour and
intensity** are the sky's: all three are written every frame, the rotation from
`sunElevation` / `sunAzimuth` and the other two from `sky.lightColor` /
`sky.lightIntensity` by day and the `night.moonlight*` pair after dark.

**The sunlight crosses the sky's air.** By day the colour written is
`Atmosphere::sunlight`, `sky.lightColor` times `Atmosphere::sunTransmittance`
(`system/sky/atmosphere.h`): the sun's transmittance from the eye through the
same Rayleigh, Mie and ozone layers the sky integrates, scaled by the scene's
`rayleigh` and `mie`, divided by the transmittance straight up. A sun overhead
is the authored colour; under the default haze at 50 degrees it has lost 3% of
its red and 9% of its blue, and at five degrees blue is down to a sixteenth and
red to about a third, so the light turns orange with the sky rather than staying
white under it. The skybox draws its sun disc in the same `sunlight`, so the
disc and the light it stands for agree. The atmosphere is stated once, in that
header: its geometry, ozone and ground reach `shaders/atmosphere.glsl` through
the prelude as `ATMOSPHERE_*`, and its coefficients under the scene's scales -
`Atmosphere::coefficients`, Mie's absorption folded into its extinction - as
uniforms, so the sky's integrals and the CPU's share every term of the
extinction they integrate. Over the twilight band the intensity still fades to
nothing at the horizon, where the light swaps to the moon. The direction is the
world's, so a key light parented under something turned has its parent's world
rotation divided out of the local rotation written. Author those, not the
`Light` - an edit typed into the light is gone before the next frame draws, and
the value the scene saves is the sky's. Shadow settings, the type and the
enabled flag stay the light's. The editor's Light card says this on the card
itself and greys the two fields it does not own, because the same `findKeyLight`
tells it which light the sky is driving.

Below the horizon is night: the atmosphere is nearly black there, so a skyglow
floor plus a moon lobe take over across a twilight band (`skyNightGlow` and
the rest of `shaders/sky.glsl`, which the env-cube bake and the skybox share,
so the two cannot disagree about the time of day). The
band is `NightSkySettings::TWILIGHT_DEGREES` either side of the horizon, and
the key light fades across the same band, so the sky darkens as its light
does. The
moon is derived - opposite the sun, tilted by `moonTilt` - so dropping the sun
raises it. Stars are drawn by the **skybox** only: at 512 with prefiltered mips
the env cube would smear them into a uniform glow.

These are bound to the dedicated IBL texture slots (see the binding note
in [Rendering](rendering.md)). The baker is skipped when nothing changed
(same env-map path; procedural sun/params unmoved): a comparison and an
early-out.

## Forward+ clustering

The view frustum is diced into `CLUSTER_X` x `CLUSTER_Y` screen tiles by
`CLUSTER_Z` exponential depth slices. A compute pass (`ClusterCull`)
culls the scene's lights into each cluster's list, capped at
`MAX_LIGHTS_PER_CLUSTER`; the forward pass then shades a pixel against its own
cluster's handful rather than the whole `MAX_LIGHTS` upload. That is why the
light cap can be generous. A point light is tested as its sphere against the
cluster's box; a spot as its cone too, against the box's bounding sphere
(Wronski's cone test), so it takes a slot only where it shines. A
cluster whose list is full drops the lights past it, which shows as a cut; the
Light Clusters view draws such a cluster magenta.

**The 32 x 18 split** puts a tile at roughly 60px on a 1080p-class viewport.
Coarser tiles make each pixel iterate lights that only clip a far corner of its
tile; finer ones cost grid memory without shortening the lists, because the
lights left in a cluster already overlap it. The cull pass itself is
insensitive to the split.

## Limits and the shader prelude

Cross-cutting limits live in `engine_config.h`, and GLSL cannot read a C++
header. `GLBackend::shaderConstants` writes them out as GLSL declarations from
the C++ constants themselves, and `Vkm::GL::setShaderPrelude` puts that text
under the `#version` of every stage the loader compiles - graphics and compute
alike. So a shader simply uses `MAX_LIGHTS`, and there is no file to include and
nothing to keep in step. The binding points and texture units of `gl_bindings.h`
arrive the same way, as `#define`s a layout qualifier can take
([rendering.md](rendering.md#shader-binding-contract)).

The same holds for constants that are not limits: the twilight band
(`SKY_TWILIGHT`), the procedural atmosphere's geometry, ozone and ground
(`ATMOSPHERE_*`, from `system/sky/atmosphere.h`) and the sizes of its
tables (`TRANSMITTANCE_LUT_SIZE`, `MULTISCATTERING_LUT_SIZE`,
`SKY_VIEW_LUT_SIZE`, from `GLAtmosphere`) reach the shaders that way too.

Do not re-define a prelude constant in a shader - GLSL rejects the redefinition,
which is the check that keeps a shader from quietly holding a copy of its own.

| C++ constant (`engine_config.h`) | Value | Consumed by |
|----------------------------------|-------|-------------|
| `Config::MAX_LIGHTS`             | 256   | light upload cap; `forward/pbr`, `cluster`, `fog/inject` |
| `Config::MAX_LIGHTS_PER_CLUSTER` | 64    | Forward+ per-cluster light list cap |
| `Config::CLUSTER_X/Y/Z`          | 32 x 18 x 24 | Forward+ cluster grid dimensions (see above) |
| `Config::MAX_SHADOW_CASTERS_2D`  | 6     | 2D shadow atlas tiles; `MAX_SHADOW_CASTERS_2D` in `shaders/shadows.glsl` |
| `Config::MAX_SHADOW_CASTERS_CUBE`| 2     | point-light cube maps; `MAX_SHADOW_CASTERS_CUBE` in `shaders/shadows.glsl` |
| `Config::NUM_CASCADES`           | 4     | not mirrored to GLSL; the CSM count reaches the shader via the shadow UBO (`csmCount` / `cascadeSplits`) |
| `Config::SHADOW_NEAR`            | 0.1   | the near plane of the spot and cube projections `gl_shadow_data.cpp` builds; not in the prelude, but carried to the shader in the shadow UBO (a cube's `params.y`), which rebuilds a face's projected depth from it. A light's range is held at twice it, and a spot's cone at a degree, so neither projection can come out degenerate |

## Editor integration

- The **light gizmo** (in `editor/overlays/gizmo_overlay_draw.cpp`) draws
  a directional ray for directional lights, a cone for spotlights, a
  sphere for points, and the rect / disk outline for area lights.
- The **inspector** exposes the relevant fields per light type. Area
  light fields appear only when type is Rect or Disk.
- Light entities can be created from `Add Component -> Light` and
  parametrised live; shadow atlas slots are reassigned automatically
  when the lit set changes between frames - `GLShadowData::build` hands
  out 2D tiles and cube slots each frame in entity-slot order, the sun's
  cascades first.
