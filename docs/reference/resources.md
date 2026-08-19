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
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

struct MeshAsset : Resource {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    glm::vec3             boundsMin, boundsMax;
};
```

### TextureAsset

`TextureAsset` extends `Resource` plus the engine-level `TextureParams`
(`width`, `height`, `internalFormat` / `format` / `type`, `wrapS` / `wrapT`,
`minFilter` / `magFilter`, `generateMipmaps`). It owns the raw `pixelData`, an
`srgb` flag, and the original `filePath`; the backend converts the params to GL
state at upload time.

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

### Loaders (`src/tools/loader/`)

| File                    | Provides                                                       |
|-------------------------|----------------------------------------------------------------|
| `texture_loaders.cpp`   | Load via stb_image, auto-detect channels, sRGB flag handling   |
| `material_loaders.cpp`  | Folder loader: scans a folder for `*Color*`, `*Normal*`, etc.  |
| `model_loaders.cpp`      | Assimp-backed mesh import; per-load aiScene parse cache        |
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
