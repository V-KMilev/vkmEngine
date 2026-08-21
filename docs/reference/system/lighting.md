# Lighting

The engine supports five light types: **Directional**, **Point**,
**Spot**, **Rect** (rectangular area light), and **Disk** (disk area
light). All are data-only ECS components; light evaluation lives in
the PBR shader. Shadows go through a shared shadow atlas (2D for
directional and spot) and a cube atlas (point lights). Image-based
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
    float     shadowBias     = 0.005f;    // slope-scaled (2D) / constant (cube)
    float     shadowDistance = 100.0f;    // directional only: cascade coverage distance (world units)

    bool      enabled        = true;
};
```

Position comes from the entity's `Transform` for Point, Spot, and area
lights. Direction is the Z axis of the entity's rotation. Area lights
face along `-direction`, matching how spotlights are oriented (so the
visible side is the surface in front of the local Z forward).

## Lighting model

The PBR fragment shader (`shaders/forward/pbr/`) implements:

- **Lambertian diffuse** with energy conservation.
- **Cook-Torrance specular**: GGX (Trowbridge-Reitz) for normal
  distribution, Smith for geometry, Schlick for Fresnel.
- **Optional lobes**, gated at runtime by each material's feature flags
  (one shared PBR program, no compiled `#ifdef` variants): transmission,
  volume (absorption), clearcoat, anisotropy, subsurface, sheen,
  parallax/height, alpha test.
- **IBL**: prefiltered specular cube + irradiance cube + split-sum
  BRDF LUT; horizon haze for low-roughness mirrors.
- **GTAO**: half-res ground-truth ambient occlusion baked by
  `GLGTAOPass` and modulated into the indirect term.

### Area lights: LTC + representative point

Rect and Disk are evaluated using two industry-standard tricks:

1. **Diffuse via LTC (Linearly Transformed Cosines).** The integral of
   a Lambertian BRDF over a polygon has a closed form for a transformed
   cosine distribution. The shader evaluates the polygon form factor
   analytically (Hill's stable edge integral), with no matrix-LUT sampling.
   - Rect uses 4 vertices.
   - Disk is approximated by a 12-vertex polygon. The error is
     negligible for the budget.
2. **Specular via Karis representative-point.** For each shaded pixel,
   pick the point on the emitter closest to the perfect reflection
   direction, then evaluate a standard GGX lobe **broadened** by the
   solid angle of the emitter. This avoids the analytical-integral
   complexity for the specular lobe while staying visually correct for
   the common range of roughness/distance combinations.

`twoSided` enables emission from both faces. By default an area light
only emits from the side its `-direction` normal points along.

Area-light shadows are currently **point-style**: cast from the
emitter's center as a single shadow caster, not a soft area shadow.
This is a deliberate budget choice. Full area shadows would require
PCSS or stochastic shadow maps. The visual quality is acceptable
because the area lights themselves are typically not the dominant
shadow source.

## Shadow atlas

The shadow system has two depth stores sized by `engine_config.h`:

- **2D atlas** (`Config::MAX_SHADOW_CASTERS_2D = 6` tiles) holds
  directional and spot shadow maps. It is one depth `Texture2D` cut into
  a `SHADOW_ATLAS_COLS` x `SHADOW_ATLAS_ROWS` grid of square tiles, not a
  texture array: a caster's slot indexes one tile and is sampled through
  a per-tile UV offset/scale. The first directional caster reserves
  `Config::NUM_CASCADES = 4` consecutive tiles for a CSM (cascaded shadow
  maps) split; the remaining 2 tiles serve spot lights.
- **Cube maps** (`Config::MAX_SHADOW_CASTERS_CUBE = 2`) hold the six
  faces per point-light shadow, as individual depth `TextureCube`s rather
  than one cube array.

Shadow rendering goes through `GLShadowPass`, which:

- Runs whenever there is a caster to draw. The 2D half returns early only
  on an empty job list and the cube half loops over whatever cube jobs
  exist; nothing is cached between frames on an unchanged set of lights
  and casters.
- Gathers `castShadows = true` meshes from the **whole scene** rather than
  from the camera's visible set (see [Visibility](visibility.md)), then
  culls that list per atlas tile against the tile's own light frustum in
  `GLShadowData::cullCasters`, so a tile draws only what can reach it.
- Writes per-caster `lightSpace` matrices, biases, and atlas tile rects
  into a UBO. The forward pass samples a single tiled `sampler2DShadow`
  atlas (`u_shadowAtlas`) - mapping each caster's UV into its tile rect
  and taking a 3x3 kernel of hardware depth compares, each of them
  bilinear over a 2x2 texel neighbourhood, so one tap returns a fraction
  rather than 0 or 1 - plus a small array of plain `samplerCube` maps
  (`u_shadowCube[SHADOW_MAX_CUBE]`), one per point-light slot, each read
  with a single unfiltered tap.

  The atlas texture carries `GL_TEXTURE_COMPARE_FUNC = GL_LEQUAL` and
  linear filtering; `GLShadowAtlas::bind2D` turns the comparison mode on
  and `bind2DRaw` turns it off for the `ShadowAtlas` debug view, which
  reads stored depth through a plain `sampler2D`. Sampling a texture in
  comparison mode with a non-shadow sampler is undefined, so the two
  entry points each set the mode rather than assuming a prior state.

`shadowDistance` controls how far the directional cascades cover in world
units. It is ignored for spot, point, and area lights, which use `radius`
as their cutoff.

The four cascades are split **logarithmically**, anchored at a fixed world
distance (`CASCADE_NEAR`, 1 unit) rather than at the camera's near plane.
The anchor is what stops `shadowDistance` from trading away foreground
sharpness: every boundary then grows as the fourth root of the distance, so
the near cascade stays small as the dial rises - its far edge moves 2.5 ->
4.9 units across a `shadowDistance` of 40 -> 600. Anchoring at the camera
near plane instead makes the log term vanish (at 0.2 it contributes a few
per cent), leaving the split effectively uniform and every boundary linear
in `shadowDistance` - which coarsens the shadow directly in front of the
camera in exact proportion.

The trade is real but small: pulling the near cascade in leaves the outer
three covering more range each, so distant shadows lose some density. Total
coverage against texel density is a fixed budget, and no split escapes it -
the logarithmic one spends it evenly instead of concentrating the whole
shortfall in the foreground.

## Image-based lighting (IBL)

A persistent `GLIBLBaker` re-bakes when the environment changes (a helper
invoked from `GLBackend::render`, not a pass): when `Environment.hdrPath`
(an equirectangular HDR image) is swapped, or - with the procedural sky
enabled - when the sun angles or a sky parameter move. It produces:

**Where the sun is** is authored on the `Environment`, as `sunElevation` and
`sunAzimuth` in degrees, and nowhere else. The sky is scene-global and has to
work whether or not a scene has a directional light, so it cannot read one; and
a sky disagreeing with the light casting the shadows looks broken in a way that
is hard to diagnose. `SkySystem` (Simulation stage) resolves that by pointing
the first directional light from those same angles - so with the procedural sky
on, a light's *rotation* is not yours to animate; write `sunElevation` and the
light follows. Colour, intensity and shadow settings stay the light's.

Below the horizon is night: the atmosphere is nearly black there, so a skyglow
floor plus a moon lobe take over across a twilight band (`_common/sky.glsl`,
shared with the skybox so the two cannot disagree about the time of day). The
moon is derived - opposite the sun, tilted by `moonTilt` - so dropping the sun
raises it. Stars are drawn by the **skybox** only: at 512 with prefiltered mips
the env cube would smear them into a uniform glow.

- The **environment cube**: a sharp cubemap for low-roughness mirror
  reflections.
- The **irradiance cube**: diffuse contribution.
- The **prefilter cube**: specular contribution, with one roughness
  level per mip.
- The **BRDF LUT**: split-sum lookup for the analytic specular term.

These are bound to the dedicated IBL texture slots (see the binding note
in [Rendering](rendering.md)). The baker is skipped when nothing changed
(same env-map path; procedural sun/params unmoved): a comparison and an
early-out.

## Limits and the generated-constants contract

Cross-cutting limits live in `engine_config.h`. At configure time
`cmake/generate_shader_config.cmake` mirrors them (under the same names) into
`shaders/_generated/engine_config.glsl`, which the shaders `#include` through
the engine's shader preprocessor - C++ and GLSL share one source of truth.
Do not re-define these values in a shader; include the generated file.
(`_common/shadows.glsl` keeps its short local names as aliases:
`SHADOW_MAX_2D` = `MAX_SHADOW_CASTERS_2D`, `SHADOW_MAX_CUBE` =
`MAX_SHADOW_CASTERS_CUBE`.)

| C++ constant (`engine_config.h`) | Value | Consumed by |
|----------------------------------|-------|-------------|
| `Config::MAX_LIGHTS`             | 256   | light upload cap; `forward/pbr`, `clustering`, `fog/inject` |
| `Config::MAX_LIGHTS_PER_CLUSTER` | 64    | Forward+ per-cluster light list cap |
| `Config::CLUSTER_X/Y/Z`          | 16 x 9 x 24 | Forward+ cluster grid dimensions |
| `Config::MAX_SHADOW_CASTERS_2D`  | 6     | 2D shadow atlas layers (`SHADOW_MAX_2D`) |
| `Config::MAX_SHADOW_CASTERS_CUBE`| 2     | point-light cube maps (`SHADOW_MAX_CUBE`) |
| `Config::NUM_CASCADES`           | 4     | not mirrored to GLSL; the CSM count reaches the shader via the shadow UBO (`csmCount` / `cascadeSplits`) |
| `Config::SHADOW_CUBE_NEAR`       | 0.1   | shadow pass only (CPU-side cube projection), not mirrored to GLSL |

## Editor integration

- The **light gizmo** (in `editor/overlays/gizmo_overlay.cpp`) draws
  a directional ray for directional lights, a cone for spotlights, a
  sphere for points, and the rect / disk outline for area lights.
- The **inspector** exposes the relevant fields per light type. Area
  light fields appear only when type is Rect or Disk.
- Light entities can be created from `Add Component -> Light` and
  parametrised live; shadow atlas slots are reassigned automatically
  when the lit set changes between frames (`assign 2D atlas layers /
  cube slots` in `RenderView::build`).
