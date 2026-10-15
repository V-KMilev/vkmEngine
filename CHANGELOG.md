# Changelog

Notable changes to vkmEngine, newest first. Each release is tagged `vX.Y.Z`.

## 1.0.11

Auto-exposure, as an option.

- **Auto-exposure**, off by default: the scene's HDR colour is counted into a histogram on
  the GPU and read back without the frame waiting, and the exposure moves toward the lit
  middle of the frame. `RenderSettings::exposure` adds on top. A `project.json` without the
  new keys reads with it off.
- **Declined, with their reasons in `engine.md`:** LTC area-light specular, and a
  nearest-depth shortcut for the soft shadow's blocker search.

## 1.0.10

The shaders and the manual, each said once.

- **One copy of each shader helper**: area lights, the frame across a direction, the
  atmosphere, the near-plane ray, f0 and the B-spline combine are shared, and the forward
  shader's main reads as named steps.
- **The capture's sun map is a class of its own**, `GLCaptureShadow`.
- **The manual says what a thing is, how to use it and what it promises.** Measurements,
  hardware, history and restated headers are out, and every page was read against the code;
  `engine.md`'s settled decisions say what would reopen each.

## 1.0.9

Frame time, and quality tiers built over a project's settings.

- **Quality tiers.** A render scale (the scene at fewer pixels under a sharp UI) and a
  switch for soft shadows step Medium and Low down. A tier is built over the project's own
  settings (`projectRender()`), so picking High after Low keeps none of Low's offs. A
  settings screen can offer the monitor's modes.
- **Faster frames**, the same picture:
  - `parallelFor` splits its range eight chunks a thread, so no thread waits on one the OS
    held back.
  - High traces reflections at half resolution, gathered back by the hits on each pixel's
    own surface.
  - A point in a soft shadow's umbra skips the filter.
  - A mesh whose `movable` is off is worked out once; shadow tiles sign their fixed casters
    at once, so a still view redraws no shadow.
  - A material using none of the optional features draws with a plain forward program.
  - Asset sync and the shadow prepare look a handle up once a run.
- **The occlusion cull reads the CPU cull's world boxes**, so posed characters and water are
  culled too.
- A `project.json` or scene saved without the new keys reads them at their defaults.

## 1.0.8

OpenGL 4.5, and an engine that names no graphics API.

- **The engine names no graphics API.** The window takes a `WindowSurface` from the host,
  which the OpenGL backend implements, and the editor draws through `EditorRenderHooks`;
  `vkm_core` links no GL.
- **Reversed 32-bit float depth.** Clip z runs 0 to w, and the eye's depth is reversed into
  a float buffer, so two walls a centimetre apart a kilometre away no longer fight.
- **Direct state access, and per-frame data through a ring.** The backend edits GL objects
  by name, and draw lists, matrices, palettes and particles stream through one persistently
  mapped, fenced ring.
- **OpenGL 4.5 is the floor**, and the engine pins vkmGL 1.0.0.

## 1.0.7

The OpenGL renderer taken as far as it goes without a temporal filter.

- **Screen-space shadows.** The key light's contacts the cascades are too coarse for - a
  foot on the floor, a pebble, a crease - found in the depth buffer by Bend Studio's method
  (Days Gone), with no noise and no history, so a still frame shades the same every time.
  `screenShadows` turns it off and the Screen Shadows view shows it. Its shader keeps Bend's
  Apache-2.0 notice.
- **Smoke takes the light.** An alpha-blended particle's colour is an albedo, lit at its
  centre by every light through its shadow and by the environment, so smoke in shade is dark
  and smoke over a fire glows; an additive particle still gives off its colour. Re-check the
  colour of an alpha emitter authored to glow.
- **Decals are lit as the surface under them**: by every light through its shadow and by
  the reflection probes, where they took the sun and the sky alone.
- **Sheen and anisotropy follow their glTF extensions.** A sheen dims the layer under it by
  what it reflects and catches the sky's light as well as the lamps'; brushed metal reflects
  the sky stretched along its brushing, and the import keeps the brushing's rotation.
- **The irradiance volume follows its lights and its sky.** The bake keeps what each probe
  sees as surfaces, and a moved sun, a lamp switched on or a new sky lights them again, a
  gather a frame. Light bounces six times, not three. A grid holds at most
  `IrradianceVolume::MAX_PROBES` probes; a larger one is scaled down, and the Inspector says
  so.
- **Reflection probes follow their light too**, captured again a step a frame into a spare
  layer and swapped in whole, so no frame shows half a probe.
- **GPU occlusion culling, as an option.** `occlusionCulling` (the Culling card, or a
  project's `render` block) skips the vertex work of what other geometry hides. It pays where
  much of the scene is hidden, so it is off by default: `examples/city_block`, 63,000 meshes
  walked at street level, is the scene it is for.
- **Fixed:** GTAO darkened an open wall beside the lens, where a slice ran the wrong way.
- **Removed:** `dilateProbeGrid`, the CPU repair of a probe grid. The bake repairs refused
  probes on the GPU; `probeFillWeights` gives the blend.

## 1.0.6

Fixes and frame time, before the renderer's new features in 1.0.7.

- **Steadier frames on SMT CPUs.** The thread pool sizes itself by physical cores, not
  hardware threads, so the main thread is stalled less often waking workers: a game at
  Ultra keeps 1% lows of 88-96 fps where it had 80-85, and 0.1% lows of 77-84 where it had
  44-69.
- **A first launch compiles the engine's largest shader once, not twice**: the forward pass
  and the probe captures share it, about 6 s off a cold start.
- **GTAO no longer darkens open walls toward the frame's edges**, which it did to 92% at the
  edges of a 70 degree view, and reaches as far with the camera rolled as level.
- **Orthographic views** keep their ambient occlusion, depth of field and screen-space
  reflections near the camera, where they read 50 m of the scene as a held weapon's.
- **A reflection probe being dragged bakes once it settles**, not on every frame it moves.
- **Measured and left out:** cone specular occlusion from GTAO's bent normal, and
  visibility-bitmask GTAO, each looked worse than what it would replace; why is in the
  settled decisions (`docs/guides/engine.md`).

## 1.0.5

What a port of another engine's game ran into first.

- **A first-person view.** A mesh says who draws it (`Mesh::seenBy`): a body the
  player's own camera does not see but that still casts its shadow and shows in
  reflections (`AllButEye`), arms only that camera sees (`EyeOnly`), and a weapon on
  the viewmodel layer (`Viewmodel`), drawn at the camera's own `viewmodelFovY` and in
  front of the world, so it never goes into a wall it is held against.
- **Characters crouch and ride.** `crouching` shortens the capsule to `crouchHeight`,
  feet kept, and it stands only with room overhead; a character on a moving or turning
  body is carried by it (`groundVelocity`, `groundSpin`). Protocol version 11.
- **Particles show textures.** An emitter's `texture` replaces the soft disc, read as a
  sprite sheet played over a life or at a rate; `stretch` streaks a particle along its
  velocity, and `spin` turns one.
- **Sound.** `AudioRolloff::Inverse` fades as 1/distance, still silent at `maxDistance`;
  a looping clip goes back to its `loopStart`, read from the wav's own loop, so an intro
  plays once. Cooked sounds bake again (`COOKER_VERSION` 13).
- **The nearest shadows.** Past the shadow budget, spots and points keep their shadows
  by `Light::shadowPriority`, then by nearness to the camera.
- **A two-GPU laptop draws the game on its discrete GPU.** On Windows the game and the
  editor ask NVIDIA's or AMD's driver for it themselves; on Linux, Getting Started names
  the variables that choose it.
- **No missing-camera warning on the first frame** of a game whose behavior makes its
  camera in `onStart`.
- **Docs:** which headers a behavior includes, and the names other engines lead one to
  guess; that a stereo clip needs a mono copy to play at a position; and, for work on the
  engine, where `RenderView`'s members are named and that every texture unit is spoken for.

## 1.0.4

What games built on the engine kept writing for themselves, made the engine's.

- **An ocean.** An `Ocean` component raises wind-driven waves from a real sea spectrum
  (JONSWAP, shallower where `depth` is set) on every surface whose material is `waves`. The
  GPU moves the water and `OceanSurface` answers gameplay by the same sum, so what floats
  rides what is drawn, on every end of a networked game. Ripples down to centimetres for
  the light, foam that lingers behind crests, foam where the water meets whatever stands in
  it, and surges - ridges of water gameplay rolls across the sea. `OceanSystem` builds the sea
  once a tick and once a frame for the renderer and gameplay alike; a behavior reads it as
  `ocean()`, and the Ocean card says when water rests off the ocean's height.
- **Networking.** `NetSession::sendMessage` sends the server what must arrive - a move, a
  name - once and in order, as a `PlayerMessageEvent`; `InputMap::setPayload` carries a few
  bytes beside every command; `NetLocal` keeps what each end runs itself off the wire; a
  client may make entities of its own while it plays, and runs them, and what sits under a
  `NetLocal`, as it would offline (`simulates(scene, entity)`); `NetSession::worldTime` is
  the server's clock at what a client draws, and inside a tick that tick's own. A player
  holding still costs a third of what they did on the wire. A game can join from its own
  menu.
- **Characters.** `knockbackDrag` lets a character carry a shove, `climbing` holds it on a
  ladder or a rope while gameplay sets it each tick, and a collision says how fast its
  bodies met (`CollisionEvent::speed`).
- **UI.** `UISlider` and `UIToggle`, opacity that fades a whole subtree, shadowed text, and
  labels pinned in the scene's own view.
- **A player's files.** `playerRoot(game)` is where a game keeps its player's files, and
  `readOwnFile` / `writeOwnFile` read and write them; keys rebind and are kept
  (`InputMap::rebind`, `saveBindings`, `loadBindings`); a project's own fonts bake as it
  starts; graphics presets (`withPreset`) cap the project's look at a quality tier rather
  than replacing it.
- **For behaviors.** `findBehavior<T>`, `findEntityWithBehavior<T>`, `findNamed`,
  `findOrAdd`, a ray onto the level, `damp`, `generateRing`; particle bursts, spawn boxes
  and emitters that go once spent. Every behavior due to start is bound before any of them
  starts.
- **Rendering.** A transparent surface casts a shadow as deep as it is opaque, and a
  decal's clear surround casts none; a lamp's glow in fog no longer shows squares; the
  sky's lighting bakes in a third of the time (37 -> 13.5 ms) and keeps pace with a
  time-lapse sun.
- **The engine's splash** carries its notice at the foot of the screen.
- **A hosted server ends with its game** on Linux, as it already did on Windows.
- **A scene field holding the wrong kind of list or object fails the load**, naming its
  entity and component, as a wrong number or string already did, rather than loading empty.
- **The installers verify their downloads** against the release's `SHA256SUMS`, and so need
  it: they install releases from 1.0.4 on.
- **CI runs when asked**, from GitHub's Run workflow button; a push starts nothing, and a
  version tag builds before it releases.
- **`vkm` tries a download again** when the server fails it, up to four times, rather than
  failing the build on one bad answer.

## 1.0.3

Rendering, measured against current practice and brought up to it.

- **A sky after Hillaire, as Unreal draws it.** Multiple scattering and ozone, a sky drawn
  live every frame, and aerial perspective: distant geometry fades into the same air
  (Sky > Aerial Perspective scales it). One sun lights the scene and the sky, so Sun
  Intensity is gone and Sun Light Intensity sets both; the sun disc is the light's own size
  and brightness, and stars come out at twilight. A moving sun no longer hitches a frame.
- **Daylight defaults.** A light haze, a ground of albedo 0.3 below the horizon and one
  stop more exposure give the sky and sun daylight's balance. A project that sets its own
  exposure keeps it.
- **ACES is the default tonemap** (Hill's fit), with AgX beside it. Bloom glows only past a
  sunlit white, and bloom and the MSAA resolve follow the exposure.
- **Shadows.** Every light's bias is in shadow texels, so one pair holds at any range;
  point lights get soft shadows from their Source Radius; the atlas sizes each tile to
  what it covers (256 MB at 4096, down from ~400) and gives shadows to the lamps in view.
  A scene's stored biases now read as texels - re-check any you set.
- **Indirect light that stays out of closed rooms.** The irradiance volume bakes in
  bounces from an empty grid, so a sealed room holds no sky light and light through a
  door bounces around it; reflection probes are lit by it. A dragged volume bakes once
  it holds still.
- **Lights and materials as their references have them.** Reflections no longer count
  Fresnel twice (dielectrics were up to twice too bright at grazing angles), area
  highlights are no longer dimmed twice, and point and spot lights are spheres of their
  Source Radius. Glass keeps its reflections, leaves glow from behind, clear coats sit
  smooth over bumpy bases, and mirrored instances draw right side out.
- **Double-sided materials**, imported from glTF and set with the material editor's
  Faces dropdown.
- **Fog, particles, decals, AO.** Every shadowed light scatters through fog by its shadow;
  particles fade softly where they meet the scene; decals take the floor's indirect light;
  ambient occlusion follows XeGTAO.
- **Glossy bumpy surfaces stop sparkling at range**: the cook folds a normal map's lost
  detail into its roughness map. Re-import a model to pair its maps.
- **Material previews light like the scene** - its sun, sky, tonemap and exposure - and
  refresh when that light changes.

## 1.0.2

- **A new world grid.** Thin lines that stay readable from any height. Its three switches
  in Render Settings are the axes: each draws its line, and every two draw their plane -
  X and Z the ground, all three every plane.
- **One axis palette** - X red, Y green, Z blue - for the gizmos, the navigation axes,
  the inspector's fields and the grid alike.
- **An orthographic view.** The view bar, View > Orthographic or Numpad 5 switch it; a
  click on a navigation axis snaps to an orthographic view down that axis, and turning
  the view returns to perspective. The grid lies in the plane facing such a view.
- **New views:** Wireframe, Lighting Only and Albedo; Roughness and Metalness now show
  the textures, not only the authored values.
- **View > Show** turns each kind of gizmo on or off, remembered per project. Probes,
  volumes, decals and emitters can now be clicked to select, and a selected UI element
  is outlined.
- No more triangles in the viewport's corner or blinking in the bottom panel's.

## 1.0.1

- **Every release comes built with GCC and with Clang**, on Linux and Windows alike, each
  bringing the compiler it was built with: GCC 15.2 or Clang 21.1. The installers take GCC;
  `VKM_COMPILER=clang` before them takes Clang.
- **The editor builds your game.** New Project, File > Build Scripts and Package Game run
  vkm, their output in a new Build tab, and a project whose code was never built is built
  as it opens - no terminal needed.
- **Building the engine uses a pinned compiler** when none is named, so what you build is
  what ships.
- New projects get their `.gitignore` from the one list of what the tools generate.
- CI builds, tests, packages and uses all four SDKs on every push, and caches compiles
  between runs.
- `CONTRIBUTING.md` says how to build, test and send a change.
