# Terrain

Ground you sculpt rather than model. A terrain is an asset - a square grid of
heights, the share of four materials painted at each grid vertex, and the plants
it grows - and an entity with a `Terrain` component draws it where the entity
stands. The editor's brushes raise, smooth and flatten the heights, paint the
layers and grow the plants; a static body with a `Terrain` collider part walks
on it as drawn.

## Making one

Create > Terrain (or New on the asset browser's Terrains) makes an entity with
a `Terrain`, a static `Rigidbody` and a `Collider` whose one part is
`ColliderShape::Terrain`, on a new flat terrain, all its first layer, and opens
the **Terrain** window on it (Window > Terrain, Ctrl+5, or Edit Terrain on the
entity's card). The window edits the terrain last selected - or, with none, the
scene's first - whatever is selected since, so a click off the terrain keeps it.
Pick a tool there, then drag over the terrain in the viewport:

| Tool | What a held drag does |
|------|-----------------------|
| Raise, Lower | moves the surface up or down under the brush; Shift does the other |
| Smooth | pulls each height toward its neighbours' mean |
| Flatten | pulls each height toward where the stroke began, or a fixed height: a path, a plot |
| Paint | moves each vertex's layer shares toward all of the chosen layer |
| Plants | grows the chosen plant layer; Shift thins it |

Radius is metres, strength how fast a held brush works, hardness the share of
the radius it works in full before fading to its rim, drawn as the brush's
profile. `[` and `]` size the brush, Esc puts it down, and Ctrl+click with
Flatten takes the height clicked. While a tool is up the viewport draws the
brush's rim on the surface - in the warning color while Shift reverses it - and
a strip naming the tool and its keys. Each stroke is one undo step.

Below the brush, the four layers and the plant layers stand as tiles of their
maps; a click on one picks it and the tool that uses it. Under them, the picked
layer's material and the metres one repeat of its maps spans, sand finer than a
field, and the picked plant layer's mesh, material, how many plants a square
metre grows where it is painted full, their sizes, how far from the eye they are
drawn, and whether they cast shadows. A plant is one or more parts, each a mesh
and a material standing where the plant does: a trunk and its leaves.

Last, the grid: the metres a side, the ground stretching with it, and the
vertices a side. A new resolution resamples the terrain
(`TerrainAsset::resample`): heights are read off the surface as drawn, so every
vertex both grids share keeps its height, and the paint and plant densities
blend between neighbours.

## The asset

`TerrainAsset` (`resource/asset/terrain_asset.h`) is centred on its entity:
vertex (i, j) lies at `x = (i / (resolution - 1) - 0.5) * size`, `z` alike, its
height the local Y. `resolution` is 2^n + 1 between `MIN_RESOLUTION` and
`MAX_RESOLUTION`, so the grid cuts into whole chunks. Each grid square is two
triangles split along its diagonal from (i, j) to (i + 1, j + 1); `heightAt`
reads the surface the meshes draw and the collider collides, and `normalAt` a
vertex's normal. The entity's position and turn place it; its scale does not, as
for a collider.

It is kept in a `.terrain` file of the project, named for that file's
project-relative path (`TerrainFile`), and cooked by copying the file into the
library ([io.md](io.md#assets-recipes-and-the-cooked-cache)). The editor writes
each terrain whose file differs from it before a save or a Play cooks
(`writeTerrainFiles`, `session/terrain_files.h`), as the brushes change it in
memory only, and Stop reads each back (`readTerrainFiles`), so what a session
dug or brushed goes.

From gameplay, `TerrainBrush` (`system/terrain/terrain_brush.h`) makes the same
edits a stroke makes - a crater, a dug trench - and `hit` finds where a ray meets
the surface; commit the asset afterwards (`ResourceManager::commit`).

## Plants

Each grid square grows as many of a layer's plants as the density painted at its
corners asks (`TerrainPlants::scatter`), each placed, turned and sized by a hash
of the square and the layer's seed. The plants are a function of the asset, so
every load and every end of a networked game grows the same ones, and a stroke
moves only those where it painted. A plant is drawn whole while its box's
centre is within its layer's `maxDistance` of the eye, and casts a shadow
where its layer and its `Terrain` both do.

## How it works inside

`TerrainSystem` runs at Transform, after `HierarchySystem`. For each `Terrain`
it makes, from the asset, private assets it alone holds (never saved): each
chunk's mesh at four levels, a weight map with a texel a grid vertex, and a
layered material that blends the layers by it
([resources.md](resources.md#materialasset)). It remakes them when the asset's
version moves - a chunk only where the heights it is built from changed, the
weight map only when the shares did - and publishes the chunks and plants as
`FrameContext::terrain`. A collider's terrain part is rebuilt here too, so the
editor's queries meet a stroke where nothing ticks.

A chunk spans `TerrainChunks::CELLS` squares a side; its k-th level takes every
2^k-th vertex, the finest near the eye (`TerrainChunks::levelAt`) and each
holding twice as far as the one before. Where two neighbours meet at
different levels their edges part; each level hangs a skirt from every rim it
shares with another chunk, so the gap shows ground, not sky.

`VisibilitySystem` lists the chunks and plants after the meshes, every frame, so
their indices hold: fixed objects ([visibility.md](visibility.md#fixed-meshes))
whose level, mesh or place changing is a change of what the fixed objects draw.
The plants are listed anew only when a terrain, its place or its plants' meshes
change (`TerrainFrame::plantsRevision`), a group a chunk; while they hold, the
cull tests each group's box before the plants in it.

The collider part takes the grid's triangles with a tree built by halving the
rectangle of squares, which needs no sort, so a stroke rebuilds it as it goes
(`syncTerrainCollider`); the narrowphase and queries walk it as a mesh part's.

### Key files

- `src/engine/resource/asset/terrain_asset.{h,cpp}` - `TerrainAsset`, `TerrainFoliage`
- `src/engine/io/asset/terrain_file.{h,cpp}` - the `.terrain` body
- `src/engine/ecs/component/render/terrain.h` - `Terrain`
- `src/engine/system/terrain/terrain_system.{h,cpp}` - `TerrainSystem`
- `src/engine/system/terrain/terrain_chunks.{h,cpp}` - chunks, levels, skirts
- `src/engine/system/terrain/terrain_plants.{h,cpp}` - where plants stand
- `src/engine/system/terrain/terrain_brush.{h,cpp}` - the brushes' edits
- `src/engine/system/physics/authoring/mesh_collider.{h,cpp}` - `syncTerrainCollider`
- `src/editor/panels/terrain_editor_panel.{h,cpp}` - the Terrain window
- `src/editor/session/terrain_files.{h,cpp}` - the terrains' files, written at
  a save and Play, read back at Stop
- `src/editor/overlays/terrain_tool.{h,cpp}` - the brush in the viewport
