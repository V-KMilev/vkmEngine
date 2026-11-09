# Water

Water is a `Water` on an entity with a `Mesh`: a lake, a river, a pool or the sea, of any shape,
each at its own level - a disc of a pond, a strip of a river, a dense sheet of sea. The scene's
waves are one spectrum in its environment (`Environment::waves`), and each water takes its share
of them. Gameplay asks where the water is and what its surface does, and a `Buoyancy` body floats
on it. A scene with no `Water` builds and draws none of it.

## Making water

**Create > Water** in the editor is a still pool: a `Water` on a plane, on a material of its own.
**Create > Sea** is a sheet dense enough for the scene's waves, taking all of them, with no bottom,
its crests and waterlines foaming.

Its material holds its optics: albedo, roughness, ior, and the light it scatters back out of it,
`subsurface` of `subsurfaceColor`. Nothing about the material says it is water; the `Water` does
(`ecs/component/landscape/water.h`, each field documented there): its share of the scene's waves
and of their ripples (0 a pond, 1 the sea), its `flow` along the mesh's +U, so a river follows its
strip and carries what floats on it, how deep it reaches (0 the open sea, with no bottom), and its
foam and waterline.

A mesh that takes waves wants its vertices closer than half the waves' `minWavelength`, as the
swell is drawn by moving them. Water is water only over its mesh.

## Waves

A scene's waves (`Environment::waves`, `WaveSettings`, the World row's Waves card in the editor:
[editor.md](editor.md#where-settings-live)) are an ocean wave spectrum - the wind's speed,
direction and fetch, the water's depth, all in their `shape` (`WaveShape`) - every wavenumber of
it a wave (Tessendorf). The spectrum longer than `minWavelength` is the **swell**, laid on up to
`MAX_SWELL_CASCADES` repeating grids: it moves the water's vertices and what floats. The rest, down to
`WaveSettings::SHORTEST_RIPPLE`, is **ripples**: too short to move a vertex or anything afloat,
read by each pixel for the surface's light and foam. A crest draws the water in by `choppiness`;
the foam it leaves fades over the sea's own time, so a paused sea keeps it. Past `calmRadius`
from `calmCenter` the waves die away, gone at `stillRadius`.

**Surges** are ridges gameplay raises across the sea (`WaveSurge`): part of the waves like any,
so each water shows, foams and floats things on its share of them. Set up to
`WaveSettings::MAX_SURGES` in `scene().environment().waves.surges`; an unused one has height 0.
They are session state, never saved.

## Where the water is

A behavior asks `scene().water().at(point)` which water a world point is in, and gets its
surface's height, normal and flow there (`WaterSample`). `WaterData::at` says the rule by which
a lake above the sea floats what is in it, a pool far below the sea hides none of it, and a pool
on a deck leaves what is under the deck dry.

For the surface alone, `scene().water().waves().surface` (`WaveSurface`) answers `heightAt`,
`normalAt` and `sampleAt` over a world point, given as its X and Z in a `glm::vec2`, and
`displacement` for the water resting at one, each at a share of the waves; `withoutSurges()` is
the wind's sea alone, for a game that floats things on it.

The scene's water is computed, never saved (`WaterSystem` says when it is built), so the cull,
the renderer and gameplay agree, and every end of a networked game, a headless server too, sees the
sea the server floats things on. Water a game raises or moves is carried, never laid out again, and
its `Water`'s values are read as they stand. A world that arrives brings none of the old one's.

## Floating things

A `Buoyancy` (`ecs/component/physics/buoyancy.h`) floats a dynamic `Rigidbody` on whatever water
it is in, taking it as its `Collider`'s box: lifted by how much of the box is under, slowed toward
the water's own speed - so a river carries it - its bob damped to settle in a bob or two, and
turned to lie along the surface. It floats only where this end simulates the body
([networking.md](networking.md)).

## Limits

- **Water with waves casts no shadow** (a held shadow would freeze one moment of them), is never
  kept as fixed ([visibility.md](visibility.md#fixed-meshes)), and assumes it lies level; the
  cull grows its box by its share of `WaveSurface::farthestReach`.
- **The captures, the material preview and a skinned mesh draw water as an ordinary surface**, as
  does a `Water` past `ObjectDraw::MAX_WATERS`; no water takes a lightmap.
- **Waterline foam is captured at one level a frame**: the nearest drawn water with a waterline,
  and every water at its level. Whatever breaks the water foams it, except a cut-out
  (`AlphaMask`), whose card is mostly air.

## How it works inside

**One sea, stated twice.** The GPU draws the swell into `SSBO_SWELL` each frame that draws water
with waves (`GLWaveFields`), and the CPU draws the same grids at the same moment only when something
asks of it - a floating body or a query (`SwellMoment`, a grid drawn on first asking). Both draw by
the same transform (`shaders/opengl/scene/water/swell/swell.glsl`, `ecs/water/swell.h`) and read
between texels alike (`swellCorners` in `shaders/opengl/include/waves.glsl`), so what floats rides
what is drawn. `WaterSystem` makes the swell (`Swell`) again only when what shapes it changes and
some water takes a share of it, and a tick that floats something starts the next tick's swell
drawing on a worker, so that tick finds it drawn. Why waves are a spectrum on grids, and why the CPU
draws its own rather than reading the GPU's: [decisions.md](decisions.md#what-has-been-decided).

**Drawing it.** Water draws with programs of its own, which alone read the waves: the depth
prepass's `shaders/opengl/scene/forward/prepass/water` and the forward pass's
`shaders/opengl/scene/forward/water` both move its vertices by `shaders/opengl/include/waves.glsl`,
bit for bit, so depth matches under `GLDepth::EYE_TEST_EQUAL`; a frame drawing no water binds
neither. Each pixel reads its share of the ripple fields and the shore, roughened by what it spans,
and scatters its color (`waterShade`, `shaders/opengl/scene/forward/water/water.glsl`). The Water
pass ([rendering.md](rendering.md#the-passes-fixed-order)) draws the ripples into three repeating
fields while water is drawn (`GLWaveFields`), and the shore - how far each point of a square of
water ahead of the eye is from whatever breaks it, captured from above (`GLWaterShore`) - while
water with a waterline is, each on a clock of its own.

## Key files

- `src/engine/ecs/component/landscape/water.h` - `Water`
- `src/engine/ecs/component/physics/buoyancy.h` - `Buoyancy`
- `src/engine/ecs/settings/wave_settings.h` - `WaveSettings`, its `WaveShape`, `WaveSurge`
- `src/engine/ecs/water/water_data.h` - `WaterData`, `WaterBody`
- `src/engine/ecs/water/wave_frame.h` - `WaveFrame`, the waves at the moment
- `src/engine/ecs/water/wave_surface.h` - `WaveSurface`; `src/engine/ecs/water/ripples.h` - `Ripples`
- `src/engine/ecs/water/wave_spectrum.h`, `src/engine/ecs/water/swell.h` - the waves' spectrum, `Swell` and
  `SwellMoment`
- `src/engine/system/water/water_system.h` - `WaterSystem`
- `src/backend/opengl/storage/water/gl_wave_fields.h` - `GLWaveFields`, the swell and ripples on the GPU
- `src/backend/opengl/storage/water/gl_water_shore.h` - `GLWaterShore`
- `src/backend/opengl/pass/scene/gl_water_pass.h` - `GLWaterPass`
- `shaders/opengl/include/waves.glsl`, `shaders/opengl/scene/water/swell/swell.glsl`, `shaders/opengl/scene/water/ripples/ripples.glsl`, `shaders/opengl/scene/forward/water/water.glsl` - the GPU's statement
