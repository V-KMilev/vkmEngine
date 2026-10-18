# Resource Management

`ResourceManager` is the single owner of every asset the engine loads: meshes,
textures, materials, fonts, skeletons, animation clips and sounds. Components
and the render view reference assets through type-safe generational handles;
scenes reference them by name; the GPU-uploadable ones sync to the backend
through a per-asset version counter.

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
through one you did not just make. `get`, `edit` and `remove` assert, and
`VKM_ASSERT` compiles to nothing in a shipping engine
([building.md](building.md#build-definitions)), so a stale handle aborts a
development build and reads freed storage in a shipped game (`remove` alone
then does nothing).

A handle names a slot in one session's manager; that is why scenes store names.

## Names

Every asset inherits `Resource`, whose identity - `name()`, `version()`,
`uid()`, `isHidden()` - only the manager writes (`resource/resource.h`).

- **One name is one asset.** `add(asset, name)` on a name that exists
  **replaces** its contents and hands back the handle that already had it, so
  adding by name is repeatable - which a script reload needs, since `onStart`
  runs again. `findOrAdd` is the other way: it keeps what is there.
- An asset added with **no** name, and `rename()` onto a taken name, get a
  `" (N)"` suffix instead: there a collision is a slip, not a request.
- **`addPrivate()`** marks an asset hidden - the editor's preview spheres and
  thumbnail materials. Hidden assets are left out of pickers, the Asset Browser,
  the cook manifest and scene saves.
- `sourceJson()` is the asset's recipe (loader or generator descriptor), which
  the cooker bakes from.

## Versioning

`commit(handle)` bumps the asset's `version()`; `GLView` keeps the version it
last uploaded and rebuilds GPU state only when they differ.

A version only tracks edits *within* one asset graph. Replacing the graph - a
scene load from a file, the editor's New Scene or a switch of project - moves
`epoch()`: the incoming graph restarts at the same indices, generations and
versions, so the backend compares the epoch and drops every mirror when it
moves. `swap()` and `clear()` bump it. The editor's play-stop restore does not:
it merges into the graph it has ([io.md](io.md#scene-files)).

## Storage

The manager stores the engine's asset types and no others (`IS_ENGINE_ASSET`),
each in its own `SparseSet<T>` with a `SlotAllocator` and a name index, all made
when the manager is constructed or cleared. Storing a type a gameplay module
declared is a compile error: its slot would carry the module's code and outlive
the module across a script reload.

## Asset types

`AssetType` (`resource/asset_type.h`) names every kind the asset library holds.
Nothing on disk carries its numeric value: the manifest and the scene write the
name, and a cooked file its own kind tag. `FontAsset` has no value there - a
font is baked at startup, not cooked. The fields are in each kind's header
under `resource/asset/`.

### MeshAsset

Vertices, indices, bounds, and for a skinned mesh a parallel `skin` stream of
four bone indices and unorm8 weights. The cooked file bulk-writes both streams,
so `Vertex` and `SkinVertex` carry size asserts (`AssetCook::writeMesh`,
`resource/asset/mesh_asset.h`): a layout change is a `COOKER_VERSION` bump.

- **A mesh is skinned iff `skin` is non-empty**; why the skin is a stream of its
  own is [animation.md](animation.md)'s.
- **`skinRadius` is owed by whoever fills `skin`**, through
  `computeAndSetSkinRadius(skeleton)`, as the bounds are owed through
  `computeAndSetBounds()` by whoever fills `vertices`. A posed character is
  bounded by its bone origins inflated by this radius ([Visibility](visibility.md#posed-bounds)),
  so left at zero it culls the character.
- **`skeleton` is a name, not a handle**: a compatibility tag. A mesh posed by
  a rig of another name is reported, and still deforms - its bone indices then
  address the wrong joints.

### TextureAsset

`Resource` plus `TextureParams` (`resource/texture_format.h`) and the raw
`pixelData`, every level `mipLevels` names. The runtime frees the pixels once
the backend holds them (`RenderSystem::releaseUploadedPixels`); the editor keeps
them to cook from. An asset with no pixels has not therefore failed.

What the texels mean is its `TextureUsage`: `Color` (sRGB, filtered as light),
`Data` (linear numbers: roughness, masks) or `Normal` (a tangent-space
direction, two channels - `RG8`, or BC5 once cooked; the shader rebuilds z). Nothing in an image file says
which, so the import states it and the recipe records it; once decoded,
`internalFormat` is the only record, read back by `isSrgb()` and `usage()`.

`filterOverride` is `None` (the default: `RenderSettings::textureFiltering`
decides) or `Nearest`, for content that is wrong blended at any quality - pixel
art, lookup tables, UI sprites. A file texture states it in its recipe (code
building a `TextureAsset` sets `params.filterOverride`); the editor has no
per-texture import panel:

```json
{ "kind": "file", "path": "assets/ui/hud.png", "usage": "Color", "filter": "nearest" }
```

#### What the cook makes of a texture

The cooker (`AssetCooker::bakeTexture`, `src/tools/cook/texture_bake.cpp`)
builds what the GPU samples, once, so the runtime only uploads: the mip chain a
recipe asks for, filtered so colour, cut-out coverage, normals and roughness
hold up at range, and block-compressed to a format that needs no shader change,
unless it is smaller than a block, `Nearest` or not 8-bit;
`src/tools/cook/texture_bake.h` states the rules. GL builds a chain at upload
only for a texture that asks for one and carries a single uncompressed level
(`buildsMipsAtUpload`); `isMipmapped` answers whether a texture samples through
a chain.

### MaterialAsset

A full PBR material: scalar properties (albedo, emission, metallic, roughness,
IOR, transmission, alpha cutoff, double-sided, clearcoat, sheen, subsurface,
anisotropy, parallax) and texture handles for each map. `MaterialType` is
`Opaque`, `Transparent`, `Unlit` or `AlphaMask`; which bucket each draws in, and
in what order, is [Rendering](rendering.md#the-passes-fixed-order)'s. A mesh's
maps are read at `uv * uvScale + uvOffset` (`materialUV` in
`shaders/material.glsl`), so a ground texture repeats across a field - its
texture's recipe needs `"wrap": "repeat"`, as a file's clamps by default; a
glTF's `KHR_texture_transform` on its base colour (else its normal map) imports
as these, without its rotation. A decal and an ocean's surface lay their maps
their own way. `boxMapping` reads the uv off the object's own box instead
(`boxUV`): where a point lies in its frame at the size it is drawn, on the face
its normal most nearly turns to, so a cube stretched into a beam keeps its
texture square and the texture moves with it. It suits boxes and faceted
meshes; a smooth one shows seams.

A **layered** material (`MaterialAsset::layers`) draws up to four other
materials in its place, blended by `layerWeights`' R, G, B and A at the mesh's
uv, each layer's albedo, normal and AO/roughness/metallic maps read at its own
tiling of that uv. Where a layer's colour would repeat densely on screen it is
read again at a far larger scale and blended in, so a far slope shows no grid of
one tile. It is derived, never saved: a terrain makes the one it draws
with. The backend copies the layers' maps into an array texture apiece
(`GLLayerMaps`) and draws with a program built for it
(`shaders/forward/pbr_layered`).

### FontAsset

A baked SDF glyph atlas with its metrics and glyph table. It owns its texels
rather than a `TextureHandle` because a font is engine-owned - baked once at
startup, never written to a scene - and `swap` and `clear` leave the font slot
where it is. See [In-game UI](ui.md).

### SkeletonAsset

A rig as a flat array of bones (name, parent index) with parallel
`inverseBind` and `bindPose` arrays.

- **Bones are indices, not entities**, which would be walked, listed and saved
  for data rebuilt every frame. A `RagdollBone` names its bone by index.
- **`parent < index` is a validated invariant.** `findSkeletonFault` states it;
  the cooked reader and writer refuse a skeleton that breaks it, and
  `SkeletalAnimationSystem` leaves such a rig unposed. `findClipFault` is the
  same for a clip. It makes posing one forward loop and a cycle unrepresentable.
- `bindPose` is stored rather than derived from `inverseBind`, because that
  recovery is lossy once a bone carries scale.
- **Code finds a bone by its name**: `indexOf` finds it as the rig spells it,
  and `indexOfUnqualified` without the namespace an exporter prefixes (Mixamo's
  `mixamorig:`), for code that names a joint the same on every rig.

### AnimationClipAsset

A baked clip: every bone's keys in six flat arrays (times and values for
position, rotation, scale) with a per-bone table of ranges into them, rather
than per-bone `AnimationTrack<T>`s - six allocations instead of hundreds, and
bulk-writable to the cooked file. `bones` is parallel to the named skeleton's
bones, bound **at cook time**, so nothing resolves a bone name at runtime.

**Markers** are instants the clip announces - a footstep, the frame a swing
connects. They live on the clip, so every character playing it gets them, and
crossing one publishes an `AnimationEvent` ([Animation](animation.md#animation-events)).
Neither glTF nor FBX carries them, so they are authored in the clip's recipe:

```json
{ "kind": "model", "path": "assets/hero.glb", "clip": 2,
  "markers": [ { "name": "footstep", "time": 0.12 },
               { "name": "footstep", "time": 0.42 } ] }
```

The loader drops a marker with no name or a time outside the clip and sorts the
rest by time.

### AudioClipAsset

A sound decoded in full at load: 16-bit interleaved PCM at the source's rate
and channel count (only a mono clip can be positioned). Its `ClipSamples` is the
one shared payload in the engine, because a voice reads it on the audio thread
while a scene load may free the asset on the main thread. See [Audio](audio.md).

### TerrainAsset

A square grid of heights, each vertex's share of four layer materials, and the
plant layers it grows, kept in a `.terrain` file and named for its path. Its
meshes, weight map and layered material are derived by `TerrainSystem` and never
saved. See [Terrain](terrain.md).

## Importing and cooking

Recipe imports live in `src/tools/`, outside the engine; which hosts install
them, and what a host without them reads, is
[IO and serialization](io.md#loading-an-asset)'s. The folder says the target:

- **`vkm_tools`** (runtime-safe): `loader/` - the HDR environment loader, plain
  image decode, the SDF font baker - and the host prologue in `project_boot`.
- **`vkm_cook`** (editor and `vkm_cook_app`, never the runtime): `import/` - the
  model (cgltf, ufbx, MikkTSpace), image (stb) and sound (miniaudio) importers -
  and the cooker in `cook/`, plus `registerRecipeAssetFactories`.

The procedural generators (`src/engine/resource/generate/`) are engine code,
because the engine builds with them - a default scene, the solid colours a
recipe names. `createGeneratedMesh` and `createGeneratedTexture` rebuild an
asset from the JSON descriptor its generator stamped - `{"kind": "generator",
"type": "cube"}`, or a `solid` colour.

### Importing a model

`parseModel` (`import/model_source.h`) reads the file into a `SourceModel`, the
engine's own terms, and `model_loaders.cpp` builds the assets and the scene from
that, once for every format.

| Format | Library | What the parse does to reach the engine's space |
|--------|---------|--------------------------------------------------|
| glTF, GLB | cgltf | Already right-handed, +Y up, metres. V is flipped, because glTF's runs down the image and the engine's up. |
| FBX, OBJ | ufbx | Converted at load to right-handed, +Y up, metres, folded into geometry, node transforms and keys, so a centimetre rig arrives as a metre one with unit scale on every joint. OBJ is assumed to be in metres already. |

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
strongest influences.

## Save/load

A scene names its assets in its `assets` block, built by
`AssetSerializer::collectAssetRefs` from every R row of `VKM_SCENE_COMPONENTS`
and every `AssetRef` field. The flow is
[IO and serialization](io.md#scene-files).

## Key files

- `src/engine/resource/resource_manager.h` - the manager
- `src/engine/resource/resource.h` - the `Resource` base
- `src/engine/resource/resource_handle.h` - `Handle<T>`
- `src/engine/resource/asset_type.h` - `AssetType`, the kind every asset is filed and referenced under
- `src/engine/resource/asset/` - the asset kinds, one header each
- `src/engine/resource/texture_format.h` - the backend-agnostic texture descriptors
- `src/engine/resource/generate/` - mesh, texture, material and light generators
- `src/tools/import/` - `texture_loaders.cpp`, `material_loaders.cpp` (a folder of `*Color*`, `*Normal*` maps), `model_source.cpp`, `model_loaders.cpp`, `audio_loaders.cpp`
- `src/tools/loader/` - `environment_loaders.cpp` (`loadHDRImage`), `image_loaders.cpp` (the splash logo), `font_baker.cpp` (`bakeFontSDF`)
- `src/tools/cook/` - the cooker: `texture_bake.h`, `lod_generator.h`, `mesh_processing.h`
