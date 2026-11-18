# Resource Management

`ResourceManager` is the single owner of every asset the engine loads: meshes,
textures, materials, fonts, skeletons, animation clips, sounds and terrains. Components
and the render view reference assets through type-safe generational handles;
scenes reference them by name; the GPU-uploadable ones sync to the backend
through a per-asset version counter. How assets live on disk - recipes, the
cooked cache, who loads what - is [io.md](io.md#assets-recipes-and-the-cooked-cache)'s.

## Using it

```cpp
ResourceManager& rm = engine.getResources();

MeshHandle handle = rm.add(MeshAsset{ ... });              // unnamed
MeshHandle named  = rm.add(MeshAsset{ ... }, "wall_512");  // the name is its identity

// Find the name, or make it once; what stands under the name is kept.
MaterialHandle red = rm.findOrAdd<MaterialAsset>("game:red", makeRed);

const MeshAsset& mesh = rm.get(handle);       // read
MeshAsset& mut = rm.edit(handle);             // write...
mut.vertices.push_back(...);
rm.commit(handle);                            // ...then tell the backend

rm.forEachOfType<MaterialAsset>([](MaterialHandle h, const MaterialAsset& a) { /* ... */ });
MeshHandle byName = rm.findByName<MeshAsset>("wall_512");
rm.remove(handle);
```

A behavior field that points at an asset is an `AssetRef<Asset>`
(`resource/asset_ref.h`): a name plus its kind, which `rm.find(ref)` turns into
this session's handle. See [scripting.md](scripting.md#naming-an-asset).

## Handles

Each kind's alias sits in its asset header: `MeshHandle` is `Handle<MeshAsset>`
(`resource/asset/mesh_asset.h`), and so on.

A handle is an index plus a generation, so a stale one is *detectable*, not
safe: ask `isAlive(handle)`, or use `tryGet` / `tryEdit`, before reaching
through one you did not just make. The calls that reach through a handle - `get`,
`edit`, `commit`, `rename`, `swapValue` and `remove` - assert it, and `VKM_ASSERT`
compiles to nothing in a shipping engine
([building.md](building.md#build-definitions)), so a stale handle aborts a
development build; in a shipped game `remove` refuses it and the rest read freed
storage.

A handle names a slot in one session's manager; that is why scenes store names.

## Names

Every asset inherits `Resource`, whose identity - `name()`, `version()`,
`uid()`, `isHidden()` - only the manager writes (`resource/resource.h`).

- **One name is one asset.** `add(asset, name)` on a name that exists
  **replaces** its contents and hands back the handle that already had it, so
  adding by name is repeatable - which a script reload needs, since `onStart`
  runs again. `findOrAdd` is the other way: it keeps what is there.
- **An asset added with no name is named `asset`**, then `asset (2)`, `asset (3)`
  while those are taken. `rename()` onto a taken name gets such a suffix too,
  rather than replacing the holder: there a collision is a slip, not a request.
- **`addPrivate()`** marks an asset hidden - the editor's preview spheres and
  thumbnail materials. Hidden assets are left out of pickers, the Assets window,
  the cook manifest and scene saves.
- `sourceJson()` is the asset's recipe, which the cooker bakes from.

## Asset types

`AssetType` (`resource/asset_type.h`) names every kind the asset library holds.
Nothing on disk carries its numeric value: the manifest and the scene write the
name, and a cooked file its own kind tag. The fields are in each kind's header
under `resource/asset/`. The kinds with a page of their own are described there:
a font in [ui.md](ui.md), a clip and its markers in [animation.md](animation.md),
a sound in [audio.md](audio.md#the-clip-asset), a terrain in
[terrain.md](terrain.md#the-asset).

### MeshAsset

Vertices, indices and bounds, and two optional streams parallel to the vertices:
`skin`, four bone indices and weights a vertex, and `lightmapUVs`, the second
unwrap a baked lightmap is read by, which the cook makes (`unwrapLightmap`).

- **A mesh is skinned iff `skin` is non-empty**; why the skin is a stream of its
  own is [animation.md](animation.md#the-gpu-path)'s.
- **`skinRadius` is owed by whoever fills `skin`**, through
  `computeAndSetSkinRadius(skeleton)`, as the bounds are owed through
  `computeAndSetBounds()` by whoever fills `vertices`. A posed character is
  bounded by its bone origins inflated by this radius
  ([visibility.md](visibility.md#posed-bounds)), so left at zero it culls the
  character.
- **`skeleton` is a name, not a handle**: a compatibility tag. A mesh posed by
  a rig of another name is reported, and still deforms - its bone indices then
  address the wrong joints.
- **`findMeshFault` states what a mesh must hold to be drawn**: whole triangles,
  streams as long as the vertices, indices and bone indices in range. The cook
  and the cooked reader refuse a mesh that breaks it.

### TextureAsset

`Resource` plus `TextureParams` (`resource/asset/texture_params.h`) and the raw
`pixelData`, every level `mipLevels` names, which the runtime frees once the
backend holds them (`RenderSystem::releaseUploadedPixels`) and the editor keeps
to cook from. `mipLevels` is one, or the whole chain to 1x1, and the pixels are
exactly those levels (`findTextureFault`, which the cook and the cooked reader
hold a texture to).

What the texels mean is its `TextureUsage` - color, linear data or a normal - which
the import states and the recipe records, since no image file says it.

`filterOverride` is `None` (the default: trilinear, at
`RenderSettings::textureAnisotropy`) or `Nearest`, for content that is wrong
blended at any quality - pixel art, lookup tables, UI sprites. A file texture
states it, and a wrap that is not the default clamp, in its recipe; the editor
has no per-texture import panel:

```json
{ "kind": "file", "path": "assets/ui/hud.png", "usage": "Color", "filter": "nearest" }
```

#### What the cook makes of a texture

The whole mip chain, filtered so color, cut-out coverage, normals and roughness
hold up at range, and block-compressed where the content allows, so the runtime
only uploads; `AssetCooker::bakeTexture` (`src/pipeline/cook/texture/texture_bake.h`) states
the rules. GL builds a chain at upload only where `buildsMipsAtUpload` says.

### MaterialAsset

A full PBR material: scalar properties and a texture handle for each map.
`MaterialType` is `Opaque`, `Transparent`, `Unlit` or `AlphaMask`; which bucket
each draws in, and in what order, is
[rendering.md](rendering.md#the-passes-fixed-order)'s.

A mesh's maps are read at `uv * uvScale + uvOffset` (`materialUV` in
`shaders/opengl/include/material.glsl`), so a ground texture repeats across a field - its
texture's recipe needs `"wrap": "repeat"`, as a file's clamps by default. A glTF's
`KHR_texture_transform` on its base color (else its normal map) imports as these,
without its rotation. `boxMapping` reads the uv off the object's own box instead
(`boxUV`), so a cube stretched into a beam keeps its texture square; it suits
boxes and faceted meshes, and a smooth one shows seams.

A **layered** material (`MaterialAsset::layers`) draws up to four other
materials in its place, blended by `layerWeights`, each layer's maps read at its
own tiling; where a layer would repeat densely on screen it is read again at a
far larger scale and blended in, so a far slope shows no grid of one tile. It is
derived, never saved: a terrain makes the one it draws with. The backend copies
the layers' maps into an array texture apiece (`GLLayerMaps`) and draws with
programs built for it (`shaders/opengl/scene/forward/pbr` under `LAYERED`).

### SkeletonAsset

A rig as a flat array of bones (name, parent index) with parallel
`inverseBind` and `bindPose` arrays.

- **Bones are indices, not entities** (`SkeletonAsset` says why). A `RagdollBone`
  names its bone by index.
- **`parent < index` is a validated invariant**, which makes posing one forward
  loop and a cycle unrepresentable. `findSkeletonFault` states it; the cooked
  reader and writer refuse a skeleton that breaks it, and
  `SkeletalAnimationSystem` leaves such a rig unposed. `findClipFault` is the
  same for a clip.
- **Code finds a bone by its name**: `indexOf` as the rig spells it, and
  `indexOfUnqualified` without the namespace an exporter puts before every joint
  (`rig:Hips` is `Hips`), for code that names a joint the same on every rig.

## Importing a model

`parseModel` (`import/model/source_model.h`) reads the file into a `SourceModel`, the
engine's own terms, and `model_import.cpp` builds the assets and the scene from
that, once for every format.

| Format | Library | What the parse does to reach the engine's space |
|--------|---------|--------------------------------------------------|
| glTF, GLB | cgltf | Already right-handed, +Y up, meters. V is flipped, because glTF's runs down the image and the engine's up. |
| FBX, OBJ | ufbx | Converted at load to right-handed, +Y up, meters, folded into geometry, node transforms and keys, so a centimeter rig arrives as a meter one with unit scale on every joint. OBJ is assumed to be in meters already. |

A mesh without authored tangents gets MikkTSpace's.

Asset names are deterministic so a re-import relinks: the file's project
reference and the part - `<ref>:mesh<i>`, `<ref>:mat<i>`, `<ref>:skeleton` (one
rig per file), `<ref>:clip<i>` and `<ref>:emb:<image>#<usage>` (`#Color`,
`#Data` or `#Normal`: one image read two ways is two textures) - so
`assets/props/crate.glb:mesh0`. The indices are the file's own order, which
makes that order part of the format:

- **Meshes.** glTF: every primitive of mesh 0, then of mesh 1. FBX and OBJ: by
  node, depth-first, each mesh split by material in the order its faces first
  use them; an instance after the first shares them.
- **Materials.** glTF: the file's, and a primitive with none takes the one past
  the last, built with the spec's defaults. FBX and OBJ: by first use along the
  same walk.
- **Clips.** glTF: the file's animations. FBX: the takes that animate
  something - an exporter's empty "Take 001" is not a clip.
- **Images** a file carries are referenced as `*<index>` - glTF's image index,
  FBX's texture index - and decoded through the `model-image` recipe.

The rig is every joint the file's meshes name plus the nodes joining them,
depth-first so `parent < index` holds; a file with two rigs is refused. Clips
resolve channels to bone indices at import, and a vertex keeps its four
strongest influences. Morph targets are not read.

## How it works inside

### Versioning

`commit(handle)` bumps the asset's `version()`; `GLAssetMirror` keeps the version it
last uploaded and rebuilds GPU state only when they differ.

A version only tracks edits *within* one asset graph. Replacing the graph - a
scene load from a file, the editor's New Scene or a switch of project - moves
`epoch()`: the incoming graph restarts at the same indices, generations and
versions, so the backend compares the epoch and drops every mirror when it
moves. `swap()` and `clear()` bump it, and both keep the fonts, which the host
bakes and no scene names. The editor's play-stop restore does not move it: it
merges into the graph it has ([io.md](io.md#scene-files)).

### Storage

The manager stores the engine's asset types and no others (`IS_ENGINE_ASSET`).
Storing a type a gameplay module declared is a compile error: its slot would carry
the module's code and outlive the module across a script reload.

### Importing and cooking

Recipe imports live in `src/pipeline/`, outside the engine; which hosts install
them, and what a host without them reads, is
[io.md](io.md#loading-an-asset)'s. The folder says the target:

- **`vkm_loader`** (runtime-safe), `src/loader/`: the HDR environment loader, plain
  image decode and the SDF font baker.
- **`vkm_pipeline`** (editor and `vkm_cook_app`, never the runtime), `src/pipeline/`:
  in `import/` the model (cgltf, ufbx, MikkTSpace), image (stb) and sound (vkmAudio's
  decoder) importers and `registerRecipeAssetFactories`, the cooker in `cook/`, and in `mesh/`
  the mesh operations both use.

The procedural generators (`src/engine/io/asset/generate/`) are engine code,
because the engine and a game's behaviors build with them - a default scene, the
solid colors a recipe names, water a game makes as it runs. `createGeneratedMesh`
and `createGeneratedTexture` rebuild an asset from the descriptor its generator
stamped - `{"kind": "generator", "type": "cube"}`, or a `solid` color.

## Key files

- `src/engine/resource/resource_manager.h` - the manager
- `src/engine/resource/resource.h` - the `Resource` base
- `src/engine/resource/handle.h` - `Handle<T>`
- `src/engine/resource/asset_type.h` - `AssetType`, the kind every asset is filed and referenced under
- `src/engine/resource/asset/` - the asset kinds, one header each
- `src/engine/resource/asset/texture_params.h` - the backend-agnostic texture descriptors
- `src/engine/io/asset/generate/` - mesh, texture and material generators
- `src/pipeline/import/` - `texture_import.cpp`, `material_import.cpp` (a folder of `*Color*`, `*Normal*` maps), `audio_import.cpp`; a model's in `model/`: `source_model.cpp` (`parseModel`), the readers `gltf_reader.cpp` and `ufbx_reader.cpp`, the mesh builder they share (`mesh_corners.cpp`), `model_import.cpp`
- `src/loader/` - `hdr_image.cpp` (`loadHDRImage`), `decoded_image.cpp` (the splash logo), `font_baker.cpp` (`bakeFontSDF`)
- `src/pipeline/cook/` - the cooker: `asset_cooker.h`, `texture/texture_bake.h`, `mesh_gpu_order.h`
- `src/pipeline/mesh/` - what import and cook share: `lod_generator.h`, `mesh_decimate.h`, `lightmap_unwrap.h`
- `src/pipeline/cook/mesh_gpu_order.h` - a cooked mesh's draw order (`optimizeMeshForGpu`)
