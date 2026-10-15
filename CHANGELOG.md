# Changelog

Notable changes to vkmEngine, newest first. Each release is tagged `vX.Y.Z`.

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
