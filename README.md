<div align="center">

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="assets/logo/vkm_engine_logo_mono.png">
  <img src="assets/logo/vkm_engine_logo.png" alt="vkmEngine" width="300">
</picture>

<h3>Build games, not engines.</h3>

<p>A C++ game engine with everything between an idea and a shipped game already in it:<br>
an editor you press Play in, gameplay that reloads like a script and runs like native code,<br>
multiplayer from the first line, and one command to ship.</p>

<p>
  <a href="https://vkmengine.com"><b>Website</b></a> &nbsp;&bull;&nbsp;
  <a href="https://vkmengine.com/docs/"><b>Docs</b></a> &nbsp;&bull;&nbsp;
  <a href="docs/getting-started.md"><b>Getting started</b></a> &nbsp;&bull;&nbsp;
  <a href="CHANGELOG.md"><b>Changelog</b></a>
</p>

<p>
  <img src="https://img.shields.io/github/v/tag/V-KMilev/vkmEngine?style=for-the-badge&label=version&color=ff6b2c" alt="Version">
  <img src="https://img.shields.io/badge/C%2B%2B17-15191f?style=for-the-badge&logo=cplusplus&logoColor=ffb070" alt="C++17">
  <img src="https://img.shields.io/badge/OpenGL%204.5-15191f?style=for-the-badge&logo=opengl&logoColor=ffb070" alt="OpenGL 4.5">
  <img src="https://img.shields.io/badge/Windows%20%7C%20Linux-15191f?style=for-the-badge" alt="Windows and Linux">
  <a href="LICENSE"><img src="https://img.shields.io/badge/no%20royalty-2a9d8f?style=for-the-badge" alt="No royalty: the vkmEngine License"></a>
</p>

<img src="assets/docs/the_lab.jpg" alt="The lab example: the plaza at dusk, a campfire, a bronze statue and string lights, a ship beyond the pier" width="100%">

</div>

## Why vkmEngine

<table>
<tr>
<td width="50%" valign="top">

**Gameplay in C++, hot-reloaded**<br>
A game is its own project, and its code is its own module: change it and the editor reloads
it without closing.

</td>
<td width="50%" valign="top">

**Play inside the editor**<br>
Press Play to run the game in the viewport; Stop puts the scene back exactly as it was. Every
edit before that is undoable.

</td>
</tr>
<tr>
<td valign="top">

**Multiplayer built in**<br>
Server-authoritative simulation with client prediction and rollback, on physics that replays
to the bit, and a dedicated server that runs any project.

</td>
<td valign="top">

**Ship in one command**<br>
`vkm package` builds, cooks and hands you the game a player gets. Windows and Linux; OpenGL
today, Vulkan on the roadmap.

</td>
</tr>
</table>

## What a game looks like

A game is a project; its gameplay is behaviors, C++ classes the editor reloads each time you
build them. A field in the reflect block is saved with the scene and tuned in the inspector:

```cpp
namespace Game {

class Crate : public ReflectedBehavior<Crate> {
    public:
        void onUpdate(float dt) override;
        void onCollisionEnter(const Collision& hit) override;

    public:
        float                    pushStrength = 6.0f;
        AssetRef<AudioClipAsset> thud;
};

} // namespace Game

VKM_REFLECT_BEGIN(::Game::Crate)
    VKM_F(pushStrength)
    VKM_F(thud)
VKM_REFLECT_END()

void Game::Crate::onUpdate(float dt) {
    Rigidbody* body = tryGet<Rigidbody>();
    if (body && input().pressed("Push")) body->linearVelocity += Math::WORLD_FORWARD * pushStrength;
}

void Game::Crate::onCollisionEnter(const Collision& hit) {
    playSoundAt(thud, hit.point);
}
```

[Scripting](docs/reference/scripting.md) goes on from there: input, sounds, other behaviors, and
what runs on the server.

## What's inside

<table>
<tr>
<td width="33%" valign="top">
<img src="assets/docs/rendering.jpg" alt="The lab's plaza at dusk: a bronze statue, a campfire, sparks and string lights" width="100%">

**Rendering**<br>
<sub>Forward+ clustered lighting, cascaded, spot, cube and screen-space shadows, lightmaps for fixed
lights, area lights, a procedural sky, an irradiance volume and reflection probes, planar and
screen-space reflections, GTAO, volumetric fog, a spectral sea, lakes and rivers, terrain,
decals, particles, depth of field and bloom.</sub>
</td>
<td width="33%" valign="top">
<img src="assets/docs/physics.jpg" alt="A crate floating on the cove's waves" width="100%">

**Physics**<br>
<sub>Fixed-step rigid bodies over box, capsule, mesh and terrain colliders, a
sequential-impulse solver that replays to the bit, joints, buoyancy, collision layers,
raycasts and sleeping.</sub>
</td>
<td width="33%" valign="top">
<img src="assets/docs/characters.jpg" alt="Four characters on the lab's start pad" width="100%">

**Characters**<br>
<sub>Skeletal import, GPU skinning, clip blending, bone sockets, ragdolls and a character
controller that takes steps and slides along walls.</sub>
</td>
</tr>
<tr>
<td valign="top">
<img src="assets/docs/viewport.jpg" alt="The editor's viewport over the lab, its lights, sounds and colliders drawn" width="100%">

**The editor**<br>
<sub>Play mode and full undo/redo, prefabs with per-instance overrides, a terrain
sculptor, assets in windows of their own, render settings live, and a cooker.</sub>
</td>
<td valign="top">
<img src="assets/docs/course.jpg" alt="The lab's obstacle course from above, the cove's hills behind" width="100%">

**Multiplayer**<br>
<sub>Client-server over UDP with snapshot interpolation, lag compensation and
delta-compressed replication. The lab is also the multiplayer sample.</sub>
</td>
<td valign="top">
<img src="assets/docs/range.jpg" alt="The demolition range on the beach, a lifeguard tower and a sparking torch" width="100%">

**Core**<br>
<sub>An open ECS over sparse sets, generational handles, a staged system pipeline,
compile-time reflection, a job system, a typed event bus, spatial audio, an in-game UI
with SDF text, and a loading screen a game can make its own.</sub>
</td>
</tr>
</table>

<img src="assets/docs/editor.jpg" alt="The editor with the lab open: its hierarchy, the viewport and the asset browser" width="100%">

## Install

```bash
# Linux
curl -fsSL https://github.com/V-KMilev/vkmEngine/releases/latest/download/install.sh | sh
```

```powershell
# Windows, in PowerShell
irm https://github.com/V-KMilev/vkmEngine/releases/latest/download/install.ps1 | iex
```

No administrator, and nothing else to install but, on Linux, the C library's headers
([what you need](docs/getting-started.md#what-you-need)): the engine brings its own Python, and its first
build fetches the compiler, CMake and Ninja it was built with - GCC by default, or Clang with
`VKM_COMPILER=clang` before the installer. The installer checks the download against the release's
`SHA256SUMS` before it unpacks anything. Each release also carries the examples built as games,
ready to play. Then open **vkmEngine** from your app menu, or:

```bash
vkm new mygame                   # or: vkm new lab -t the_lab, a copy of an example
cd mygame
vkm run                          # build, cook and play
vkm package                      # the game a player gets
```

[Getting started](docs/getting-started.md) goes on from there. Every release is also a plain
archive on the [releases page](https://github.com/V-KMilev/vkmEngine/releases), to unpack and use
where it is.

## Building from source

```bash
git clone --recursive https://github.com/V-KMilev/vkmEngine
cd vkmEngine
cmake -B build -G Ninja && cmake --build build
./tools/vkm run examples/the_lab                 # build its module, cook, and play
./build/bin/vkm_editor examples/the_lab          # or open it in the editor
```

Four executables share one project directory: `vkm_editor` authors it, `vkm_runtime` plays it,
`vkm_cook` bakes its assets, and `vkm_server` serves it to players. `examples/` holds four
complete projects: the lab, a world to play in that shows the engine and is also the multiplayer
sample; an endless runner; a stress arena; and a city walked at street level for the GPU occlusion
cull.
[Building](docs/reference/building.md) lists the prerequisites, the targets, and how CI and
releases build both SDKs.

## Documentation

The manual lives in [docs/](docs/README.md) and online at
[vkmengine.com/docs](https://vkmengine.com/docs/), with the API reference beside it.

| Using the engine | How it works | Working on the engine |
|---|---|---|
| [ECS](docs/reference/ecs.md) | [Architecture](docs/reference/architecture.md) | [Guides overview](docs/guides/README.md) |
| [Scripting](docs/reference/scripting.md) | [Rendering](docs/reference/rendering.md) | [The engine](docs/guides/engine.md) |
| [Input](docs/reference/input.md) | [Lighting](docs/reference/lighting.md) | [Design](docs/guides/design.md) |
| [UI](docs/reference/ui.md) | [Visibility](docs/reference/visibility.md) | [Implementation](docs/guides/implementation.md) |
| [Physics](docs/reference/physics.md) | [Hierarchy](docs/reference/hierarchy.md) | [Code style](docs/guides/code-style.md) |
| [Audio](docs/reference/audio.md) | [Events](docs/reference/events.md) | [Review](docs/guides/review.md) |
| [Animation](docs/reference/animation.md) | [Resources](docs/reference/resources.md) |  |
| [Networking](docs/reference/networking.md) | [Threading](docs/reference/threading.md) |  |
| [IO & serialization](docs/reference/io.md) | [Building](docs/reference/building.md) |  |
| [Terrain](docs/reference/terrain.md) |  |  |
| [Editor](docs/reference/editor.md) |  |  |

## Source layout

<details>
<summary>Where everything lives</summary>

```
src/
  engine/          Engine code (single include root: src/engine/)
  backend/opengl/  OpenGL backend (gl_-prefixed files, module-qualified includes)
  editor/          ImGui editor (command, session, chrome, panels, overlays, input, ui)
  tools/           Asset import, the cooker, the image/font loaders and the host prologue
app/               The four executables, and the bootstrap (setupEngineApp) the three that build an Engine share
examples/          Projects: Potion Runner, The Lab, Stress Arena, City Block - gameplay lives here
modules/
  vkmGL            Submodule: GL object wrappers + shader loading (vendors GLEW)
  vkmLog           Submodule: logging + VKM_ASSERT
  glm / glfw / stb Submodules: math, windowing, image + font decode, mip filtering
  miniaudio        Submodule: audio device + wav/mp3/flac decode
  imgui            Submodule: Dear ImGui
  meshoptimizer    Submodule: LOD simplification, GPU vertex order and the import's weld
  xatlas           Submodule: lightmap UV unwrapping
  freetype / json / tracy
                   Submodules: font rasterization, JSON, profiling
  cgltf / ufbx / mikktspace
                   Submodules: glTF and FBX/OBJ import, and MikkTSpace tangents
  basis_universal  Submodule: the cooker's BC7 encoder, one file of it, cloned shallow
shaders/           GLSL source (one folder per program)
tools/             vkm (the project tool, in vkmcli/), the toolchain it pins, and the installers
assets/            The editor's fonts and logo, and the manual's images; sample art stays local
```

See [Architecture](docs/reference/architecture.md) for the full per-directory breakdown.

</details>

## Contributing

Fixes and features are welcome: [CONTRIBUTING.md](CONTRIBUTING.md) says how to build, test and
send one, and what the license asks of a contribution.

## License

vkmEngine is source-available under the [vkmEngine License](LICENSE). Make games with it and sell
them, free, with no royalty; change the engine for your own games, which ship it compiled. Changes
to the engine itself are shared only as contributions to this repository or in a fork of it on
GitHub - the engine's source, changed or not, is not published or sold anywhere else, and it is
not offered as an engine or toolkit of your own. `vkm package` puts the license and its notice in
every game it builds. The third-party libraries under `modules/` keep their own licenses, and so does
`shaders/screen_shadow`, a port of Bend Studio's screen-space shadows under Apache-2.0.
