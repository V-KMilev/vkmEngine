# Changelog

Notable changes to vkmEngine, newest first. Each release is tagged `vX.Y.Z`.

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
