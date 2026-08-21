# Resource Management

`ResourceManager` is the single owner of every asset the engine loads: meshes,
textures, materials, fonts, skeletons and animation clips. Assets are referenced
from components and the render view through type-safe generational handles, and
the GPU-uploadable ones sync through a per-resource version counter.

## Key files

- `src/engine/resource/resource_manager.h` for the manager
- `src/engine/resource/resource.h` for the `Resource` base (version, name, hidden flag, source JSON)
- `src/engine/resource/resource_handle.h` for type-safe `Handle<T>`
- `src/engine/resource/asset/mesh_asset.h`, `asset/texture_asset.h`, `asset/material_asset.h`, `asset/font_asset.h`, `asset/skeleton_asset.h`, `asset/animation_clip_asset.h` for the asset kinds
- `src/engine/core/memory/sparse_set.h` for the `SparseSet<T>` that backs each asset table

## Handles

```cpp
using MeshHandle          = Handle<MeshAsset>;
using TextureHandle       = Handle<TextureAsset>;
using MaterialHandle      = Handle<MaterialAsset>;
using FontHandle          = Handle<FontAsset>;
using SkeletonHandle      = Handle<SkeletonAsset>;
using AnimationClipHandle = Handle<AnimationClipAsset>;
```

Each handle wraps a `StorageIndex` (index + generation), so stale handles
are detected automatically; using a destroyed handle returns nothing
without crashing.

## API

```cpp
ResourceManager& rm = engine.getResources();

// Add a resource (returns a typed handle)
MeshHandle handle = rm.add(MeshAsset{ ... });

// Add with an explicit name; the name becomes the cross-save-load identity
MeshHandle named  = rm.add(MeshAsset{ ... }, "wall_512");

// Read-only access
const MeshAsset& mesh = rm.get(handle);

// Mutable access for editing
MeshAsset& mut = rm.edit(handle);
mut.vertices.push_back(...);

// Commit (bumps the per-resource version)
rm.commit(handle);

// Remove
rm.remove(handle);

// Iterate all assets of a type
rm.forEachOfType<MaterialAsset>([](MaterialHandle h, const MaterialAsset& a) {
    // ...
});

// Lookup by name (stable identity across save/load)
auto handle = rm.findByName<MeshAsset>("wall_512");
```

## Resource base

Every asset inherits `Resource`:

| Field          | Type                                  | Notes                                                                                       |
|----------------|---------------------------------------|---------------------------------------------------------------------------------------------|
| `name`         | `std::string`                         | Stable identity for serialization and look-up                                               |
| `version`      | `uint64_t`                            | Bumped on `commit()`; backends compare to skip re-upload                                    |
| `uid`          | `uint64_t`                            | Process-unique instance id stamped by `add()`. A handle names a slot; this names the asset in it, which is how an async completion knows the graph did not change under it |
| `hidden`       | `bool`                                | When true, filtered from pickers / Asset Browser / scene save (previews, fallbacks). Set via `addPrivate()` |
| `source`       | `std::unique_ptr<nlohmann::json>`     | The asset's recipe (loader/generator descriptor). The editor cooker bakes it into the library + cooked cache; scenes reference the asset by `name`, not by this descriptor |

`source` is held by `unique_ptr` against a forward-declared `nlohmann::json`
so headers don't drag the JSON header in (see
[../guides/code-style.md](../guides/code-style.md) for why this exception is
deliberate).

`hidden = true` is set (via `ResourceManager::addPrivate`) on the editor's own
preview and thumbnail assets - the Asset Browser's preview sphere and neutral
thumbnail material, the Material Editor's preview primitives. The cooker leaves
them out of the manifest and `AssetSerializer` refuses to write a reference to
one, so save files contain only user-relevant content. Names are guaranteed unique and non-empty per type:
`add()` runs them through `ensureUniqueName`, and a name must be changed via
`rename()` (not `edit().name = ...`) so the name index stays consistent.

## Asset types

### MeshAsset

```cpp
struct Vertex {          // 48 bytes, and it stays 48
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

struct SkinVertex {      // 12 bytes, in a stream parallel to `vertices`
    uint16_t bones[4];   // indices into the rig named by MeshAsset::skeleton
    uint8_t  weights[4]; // unorm8, summing to exactly 255
};

struct MeshAsset : Resource {
    std::vector<Vertex>     vertices;
    std::vector<uint32_t>   indices;
    std::vector<SkinVertex> skin;        // empty, or exactly vertices.size()
    std::string             skeleton;    // rig `skin` addresses; empty when unskinned
    float                   skinRadius;  // furthest a vertex sits from a bone that moves it
    glm::vec3               boundsMin, boundsMax;
};
```

**A mesh is skinned iff `skin` is non-empty** - the asset already knows, so no
component has to say so.

The skin rides in its own stream rather than inside `Vertex` because folding
four indices and four weights in would cost every vertex of every mesh in the
engine 25% more bandwidth, paid hardest by the shadow pass, which reads only
`aPos` and replays the geometry per cascade tile and per cube face. A rock does
not pay for skinning. Indices are 16-bit because the cooked format has no
migration path and an 8-bit index would weld a 255-bone ceiling into it
permanently; weights are quantised so the four bytes sum to exactly 255, which
makes `w / 255.0` sum to exactly 1.0 and spares every vertex stage a
renormalise.

`skinRadius` is computed, not authored: `computeAndSetSkinRadius(skeleton)` sits
beside `computeAndSetBounds()` and is owed by whoever fills `skin`, exactly as
the bounds are owed by whoever fills `vertices`. There is one implementation
because leaving the field at zero is not a smaller box but a wrong one - a posed
character is bounded by the box of its posed bone origins *inflated by this
radius* (see [Visibility](system/visibility.md)), and the occlusion cull keeps
conservatively, so an under-sized box does not over-draw, it deletes the
character.

`skeleton` is a **name, not a handle**: a compatibility tag rather than a
dependency. The mesh uploads its skin stream either way and the pose it is drawn
with comes from whatever rig is driving it, so the name is what lets the runtime
report the failure that actually happens - a rig assigned to the wrong character
- instead of exploding the geometry and leaving the cause to be guessed at.

### TextureAsset

`TextureAsset` extends `Resource` plus the engine-level `TextureParams`
(`width`, `height`, `internalFormat` / `format` / `type`, `wrapS` / `wrapT`,
`filterOverride`, `generateMipmaps`). It owns the raw `pixelData`, an `srgb`
flag, and the original `filePath`; the backend converts the params to GL state
at upload time.

`filterOverride` is the texture's own say over how it is sampled, and it is
deliberately narrow: `None` (the default) or `Nearest`. Filtering is otherwise
a machine-quality trade owned by `RenderSettings::textureFiltering`, but some
content is *wrong* when its texels are blended at any quality level - pixel
art, lookup tables, UI sprites - and only the texture knows that. A texture
that states `Nearest` keeps it whatever the setting says; everything else
follows the setting, so a filtering menu still reaches the whole scene. The
cost question (bilinear against trilinear against a degree of anisotropy) has
no per-asset answer and is not expressible here. `resolveTextureFilter` in the
GL backend is the one place the two meet.

A file texture states it in its recipe, beside `sRGB` and `generateMipmaps`:

```json
{ "kind": "file", "path": "assets/ui/hud.png", "sRGB": true, "filter": "nearest" }
```

The key is absent from a texture that has no opinion, which is nearly all of
them. Code that builds a `TextureAsset` directly sets `params.filterOverride`
instead. There is no per-texture import panel in the editor yet, so those two
are the authoring surface.

### MaterialAsset

Full PBR material. The scalar properties cover:

- Albedo (`vec4`), emission (`vec3`), metallic, roughness, IOR, transmission
- Alpha cutoff, AO, clearcoat, clearcoat roughness, anisotropy
- Subsurface, sheen, parallax/height

It carries texture handles for albedo, normal, metallic, roughness, a
combined metallic-roughness slot, AO-metallic-roughness (glTF), AO,
emission, height, clearcoat, transmission.

All optional PBR features are runtime toggles: one shared PBR ubershader
branches on the individual material scalars and texture-present uniforms at
draw time. There is no feature bitset and no per-variant compiled shaders; see
[Rendering](system/rendering.md).

`MaterialType` is `Opaque = 0`, `Transparent = 1`, `Unlit = 2`, or
`AlphaMask = 3`. Sorting in the render view groups the depth-writing types
(Opaque/AlphaMask/Unlit) ahead of Transparent so the per-batch HDR snapshot for
refraction sees a complete opaque pass.

### FontAsset

A baked SDF glyph atlas: the atlas pixels, its dimension, the vertical
metrics, and a per-glyph table. Self-contained on purpose - it owns its texels
rather than a `TextureHandle` - which is what lets it survive a scene load: the
font is engine-owned (baked once at startup, never written to a scene file), so
`SceneSerializer::load` swaps its slot back out of the displaced manager with
`swapSlot<FontAsset>` instead of letting the scene-level swap drop it. See
[In-game UI](system/ui.md).

### SkeletonAsset

A rig, as a flat array rather than a tree:

```cpp
struct Bone {
    std::string name;
    int32_t     parent = -1;   // -1 for a root; always < this bone's own index
};

struct SkeletonAsset : Resource {
    std::vector<Bone>      bones;
    std::vector<glm::mat4> inverseBind;   // rig model space -> bone space, at bind
    std::vector<Transform> bindPose;      // local TRS a bone falls back to
    int32_t indexOf(std::string_view name) const;
};
```

Two decisions carry the rest of the skeletal path:

**Bones are indices, not entities.** A hundred entities per character would be
walked by the hierarchy, listed in the hierarchy panel and written to the scene
file, for data that is rebuilt every frame and has no authoring meaning. An
index also maps straight onto a rigid body when physics comes to address one.

**`parent < index` is a validated format invariant**, not a convention. The
importer emits bones depth-first and `AssetCook::readSkeleton` re-checks the
ordering on the way back in. That is what makes composing a pose one forward
loop with no recursion and no visited set, and what makes a cycle
*unrepresentable* rather than something every walk has to defend against.

`bindPose` is stored rather than derived from `inverseBind`, because recovering
it means inverting and re-localising, which is lossy the moment a bone carries
scale. The three vectors are parallel and always the same length; the writer
refuses a skeleton where they are not.

### AnimationClipAsset

A baked clip: every bone's keys in six flat arrays, with a per-bone table of
ranges into them.

```cpp
struct ClipChannel { uint32_t first, count; };  // count 0 = channel absent
struct ClipBone    { ClipChannel position, rotation, scale; };

struct AnimationClipAsset : Resource {
    std::string skeleton;          // rig whose bone order `bones` addresses
    float       duration = 0.0f;   // seconds, stored rather than derived
    std::vector<ClipBone>  bones;  // parallel to that rig's bones
    std::vector<float> positionTimes;  std::vector<glm::vec3> positions;
    std::vector<float> rotationTimes;  std::vector<glm::quat> rotations;
    std::vector<float> scaleTimes;     std::vector<glm::vec3> scales;
};
```

`AnimationTrack<T>` is deliberately **not** reused here. Three tracks over a
hundred bones is three hundred heap vector pairs and three hundred easing
function pointers for one clip; six flat arrays are six allocations,
bulk-writable to the cooked file and cache-linear over a bone sweep. Easing goes
with it - keys arrive from a DCC tool already baked at its own sample rate, and
there is no author to pick a curve per bone. The keyframe `Animation` component
keeps `AnimationTrack<T>` and is untouched (see
[Animation](system/animation.md)).

A clip is bound to its rig **at cook time**: `bones` is parallel to the named
skeleton's bone array, so nothing resolves a bone name at runtime.

## Versioning

`commit(handle)` bumps a single per-asset `version` counter. (There is no
global or per-type version counter - those mechanisms were removed.) `GLView`
keys on this per-asset version: it keeps a per-asset cached version and rebuilds
GPU state only when the cached value diverges from the asset's `version`.

`version` only tracks edits *within* one asset graph. A wholesale replacement
(scene load, editor play-stop restore) is what `epoch()` is for: the incoming
graph restarts at the same indices, generations and versions, so the backend
compares the epoch and drops every mirror when it moves. `swap()`, `swapSlot()`
and `clear()` all bump it.

## Storage

Every asset type gets its own `SparseSet<T>` (created on first use) plus a
`SlotAllocator` for generational keys and a name index for O(1) lookup:

- O(1) add, remove, look-up.
- O(n) dense iteration.
- Swap-and-pop removal keeps data packed.
- Generation checks make stale handles a no-op (not a crash).

## Tools: loaders, generators, and the cooker

Procedural generators and file loaders live in `src/tools/`. They are **not**
part of the engine core; they wire the `AssetFactory` dispatch seam at startup so
the engine-side `AssetSerializer` can resolve any asset by its `kind`.
The tools split by dependency weight:

- **`vkm_tools`** (runtime-safe): the GLM-only generators, the cooked-asset
  loaders, and `registerCookedAssetFactories` (`cooked` / `inline`).
- **`vkm_cook`** (editor-only): the heavy importers (`loader/`, Assimp + stb)
  and the asset cooker (`cook/`), plus `registerRecipeAssetFactories`.

The runtime registers only the cooked set, so it links neither Assimp nor the
image decoders; the editor registers the recipe set instead, which falls through
to the cooked functions and (re)cooks recipes into the cache.

### Generators (`src/tools/generator/`)

| File                    | Provides                                                       |
|-------------------------|----------------------------------------------------------------|
| `mesh_generators.cpp`   | `generateTriangle/Plane/Cube/Sphere/Pyramid/Cone` free functions, plus `decimateMesh` (LOD) |
| `texture_generators.cpp`| Solid color, white, black, normal, gray (1x1 fallback)         |
| `material_generators.cpp`| Default PBR material with the fallback texture suite          |
| `light_generators.cpp`  | `generateLight(LightType)` - a light component with that type's defaults |

The generators are plain free functions; the string dispatch (`"name"` ->
generator) lives in the `generator`/`decimate` factory lambdas registered in
`asset_registration.cpp`, not a `byName` API.

### Importing a rigged model

`model_loaders.cpp` builds four kinds of asset from one file, all named
deterministically so a re-import relinks: `<stem>:mesh<i>`, `<stem>:mat<i>`,
`<stem>:skeleton` (one rig per file) and `<stem>:clip<i>`.

The rig is the union of every bone any of the file's meshes names, plus the
nodes joining them down from their lowest common ancestor, emitted depth-first
so `parent < index` holds by construction. `aiProcess_PopulateArmatureData` is
what makes a file holding two rigs answerable: it is refused rather than merged
into one with an invented shared root and a single bone numbering that no clip
in the file is bound to.

Clips resolve their channels to bone indices **at import**, against that same
rig. A channel naming a node outside it - a camera, a prop, the mesh node an
exporter animated - is dropped and counted. Assimp's tick rate is zero far more
often than not, so the `mTime / (mTicksPerSecond ? mTicksPerSecond : 25.0)`
fallback is load-bearing rather than defensive.

Two importer hazards are handled explicitly:

- `aiProcess_LimitBoneWeights` caps a vertex at four influences and
  renormalises what survives, which is exactly what `SkinVertex` holds.
  Without it a fifth influence would be dropped *after* the weights were
  normalised against it.
- `aiProcess_JoinIdenticalVertices` merges vertices on a key that omits skin
  weights and filters the merged-away ones out. Past that, Assimp only rewrites
  a bone's weight list when the rewrite is non-empty - so a bone whose weights
  **all** landed on joined vertices keeps its pre-join vertex ids against the
  shrunken array. The importer bounds-checks every `mVertexId` and counts what
  it drops, because following one is an out-of-bounds read of Assimp's own
  data. The flag stays: dropping it needs a two-phase parse this codebase has
  never exercised, and `POST_PROCESS_FLAGS` is deliberately one shared constant
  so mesh and material indices stay stable across every entry point.

A vertex that arrives with no influence at all is bound rigidly to the rig root
and counted, rather than left at zero weight - `sum(w * M)` with every `w` zero
collapses it onto the origin, which reads as a broken importer instead of as
one bad vertex.

### Loaders (`src/tools/loader/`)

| File                    | Provides                                                       |
|-------------------------|----------------------------------------------------------------|
| `texture_loaders.cpp`   | Load via stb_image, auto-detect channels, sRGB flag handling   |
| `material_loaders.cpp`  | Folder loader: scans a folder for `*Color*`, `*Normal*`, etc.  |
| `model_loaders.cpp`      | Assimp-backed mesh, rig and clip import; per-load aiScene parse cache |
| `environment_loaders.cpp`| HDR equirectangular image loader (`loadHDRImage`) for IBL / skybox |

## Save/load round-trip

See [IO and serialization](system/io.md) for the full flow.
`AssetSerializer::saveAssetsForScene` emits only the assets actually
referenced by the scene - `Mesh` (mesh + material), `LOD` (every level's
mesh) and `Decal` (its material), plus the textures those materials
reference. `emitDescriptor` is the single gate every one of those goes
through, so a hidden or unnamed asset cannot be written as a reference by
any emitter, present or future. A component that writes an
asset name into the scene file has to be walked there, or the name has
nothing to resolve against on load. On load,
assets with the same `name` already in the manager are skipped (loads
are idempotent), and new assets go through the `AssetFactory` dispatch
by `kind`.
