# Changelog

Notable changes to vkmEngine, newest first. Each release is tagged `vX.Y.Z`.

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
