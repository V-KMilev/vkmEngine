# Terrain

Ground you sculpt rather than model. A terrain is an asset - a square grid of
heights, the share of four materials painted at each grid vertex, and the plants
it grows - and an entity with a `Terrain` component draws it where the entity
stands. The editor's brushes raise, smooth and flatten the heights, paint the
layers and grow the plants; a static body with a `Terrain` collider part walks
on it as drawn.

## Making one

**Create > Terrain** makes an entity with a `Terrain`, a static `Rigidbody` and a `Collider`
whose one part is `ColliderShape::Terrain`, on a new flat terrain, and opens the **Terrain**
window on it; the window stays on the terrain last selected, and each stroke is one undo step.

## The asset

`TerrainAsset` (`resource/asset/terrain_asset.h`) is a grid centered on its entity,
`resolution` vertices a side, 2^n + 1 so it cuts into whole chunks. Each grid square is
two triangles split along one diagonal, the same for `heightAt`, the meshes and the
collider, so a body walks on what is drawn. The entity's position and turn place it; its
scale does not, as for a collider.

It is kept in a `.terrain` file of the project, named for that file's project-relative
path (`TerrainFile`), and every host reads that file as it is, as a scene's: it is not
cooked, and `vkm package` ships it ([io.md](io.md#assets-recipes-and-the-cooked-cache)).
The brushes change it in memory only, so the editor writes each terrain whose file differs
before a save or a Play cooks
(`writeTerrainFiles`, `session/asset/terrain_files.h`), and Stop reads each back
(`readTerrainFiles`): what a session dug or brushed goes.

From gameplay, `TerrainBrush` (`system/terrain/terrain_brush.h`) makes the same
edits a stroke makes - a crater, a dug trench - and `hit` finds where a ray meets
the surface; commit the asset afterwards (`ResourceManager::commit`).

## Plants

Each grid square grows as many of a layer's plants as the density painted at its
corners asks (`TerrainPlants::grow`), each placed, turned and sized by a hash
of the square and the layer's seed. The plants are a function of the asset, so
every load and every end of a networked game grows the same ones, and a stroke
moves only those where it painted. A plant is drawn whole while its box's
center is within its layer's `maxDistance` of the eye, and casts a shadow
where its layer and its `Terrain` both do.

A plant is one or more parts, each a mesh and a material: a trunk and its leaves. Each
part takes the coarser levels of its mesh the project has made (`LOD::levelName`, as
**Generate Levels** names them) by distance; a plant has no ranges of its own to author.
A chunk's plants pick their levels together, and the new levels fade in over the old.

## How it works inside

`TerrainSystem`, at Transform, derives each terrain's chunk meshes (four levels), weight
map and layered material ([resources.md](resources.md#materialasset)) as private assets,
remakes only what a stroke touched (its header says how), and publishes the chunks and
plants as `FrameProducts::terrain`. A collider's terrain part is rebuilt there too, so the
editor's queries meet a stroke where nothing ticks.

A chunk's k-th level takes every 2^k-th vertex, the finest near the eye
(`TerrainChunks::levelAt`). Where two neighbors meet at different levels their edges
part; each level hangs a skirt from every rim it shares with another chunk, so the gap
shows ground, not sky.

`VisibilitySystem` lists the chunks after the meshes every frame, and the plants after
them, so their indices hold: fixed objects ([visibility.md](visibility.md#fixed-meshes))
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
- `src/engine/ecs/component/landscape/terrain.h` - `Terrain`
- `src/engine/system/terrain/terrain_system.{h,cpp}` - `TerrainSystem`
- `src/engine/system/terrain/terrain_chunks.{h,cpp}` - chunks, levels, skirts
- `src/engine/system/terrain/terrain_plants.{h,cpp}` - where plants stand
- `src/engine/system/terrain/terrain_brush.{h,cpp}` - the brushes' edits
- `src/engine/ecs/component/physics/mesh_collider.{h,cpp}` - `syncTerrainCollider`
- `src/editor/panels/terrain_editor_panel.{h,cpp}` - the Terrain window
- `src/editor/session/asset/terrain_files.{h,cpp}` - the terrains' files, written at
  a save and Play, read back at Stop
- `src/editor/overlays/terrain_tool.{h,cpp}` - the brush's settings and target
- `src/editor/overlays/terrain_tool_overlay.{h,cpp}` - the brush in the viewport
