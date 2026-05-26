# Resource Management

`ResourceManager` is the single owner of every GPU-uploadable asset:
meshes, textures, materials, and shaders. Assets are referenced from
components and the render view through type-safe generational handles,
and they sync to the GPU through a per-resource version counter.

## Key files

- `src/engine/resource/resource_manager.h` for the manager
- `src/engine/resource/resource.h` for the `Resource` base (version, name, internal flag, source JSON)
- `src/engine/resource/resource_handle.h` for type-safe `Handle<T>`
- `src/engine/resource/mesh_asset.h`, `texture_asset.h`, `material_asset.h`, `shader_asset.h` for the four asset kinds
- `src/engine/core/memory/storage.h` for the generational arena that backs each asset table

## Handles

```cpp
using MeshHandle     = Handle<MeshAsset>;
using TextureHandle  = Handle<TextureAsset>;
using MaterialHandle = Handle<MaterialAsset>;
using ShaderHandle   = Handle<ShaderAsset>;
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

// Commit (bumps per-resource version + per-type version + global)
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
| `internal`     | `bool`                                | When true, the asset is editor-only (preview meshes, fallback textures) and is not saved   |
| `source`       | `std::unique_ptr<nlohmann::json>`     | Captured loader/generator descriptor used by `AssetSerializer` on save                      |

`source` is held by `unique_ptr` against a forward-declared `nlohmann::json`
so headers don't drag the JSON header in. See the code style guide section
13.1 for why this is a deliberate exception.

`internal = true` is set by editor-side asset factories on previews,
fallback textures, and bundled primitive meshes; `SceneSerializer` and
`AssetSerializer` skip those assets entirely so save files contain only
user-relevant content.

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
(width, height, format, filter, wrap). It owns the raw pixel data plus
an sRGB flag and the original file path; the backend converts the params
to GL state at upload time.

### MaterialAsset

Full PBR material. The scalar properties cover:

- Albedo (`vec4`), emission (`vec3`), metallic, roughness, IOR, transmission
- Alpha cutoff, AO, clearcoat, clearcoat roughness, anisotropy
- Subsurface, sheen, parallax/height

It carries texture handles for albedo, normal, metallic, roughness, a
combined metallic-roughness slot, AO-metallic-roughness (glTF), AO,
emission, height, clearcoat, transmission.

`MaterialAsset::featureFlags()` derives a `MaterialFeature` bit set from
the active scalars and present textures (`HAS_TRANSMISSION`,
`HAS_CLEARCOAT`, `HAS_PARALLAX`, ...). The backend uses this bit set as
the key for the per-material shader variant cache; see
[Rendering](system/rendering.md).

`MaterialType` is `Opaque`, `AlphaMask`, `Unlit`, or `Transparent`.
Sorting in the render view groups by this enum first so the per-batch
HDR snapshot for refraction sees a complete opaque pass.

### ShaderAsset

`ShaderAsset` carries the path prefix to a folder under `shaders/` plus
the per-asset `variantAware` flag. Only the PBR shader is variant-aware
today: variant compilation injects `#define HAS_X` per feature bit, so
opaque materials don't pay for transmission/clearcoat branches that
won't fire. Other shaders go through `GLView::resolveShader` and share
one compiled program.

## Versioning

`commit(handle)` bumps three counters:

1. The asset's own `version`.
2. The per-type version (used by `GLView` for early-out: a type that didn't
   change between frames does not even iterate its asset table).
3. A global version counter (used by the editor for cheap "did anything change?" checks).

`GLView` keeps per-asset cached versions and rebuilds GPU state only when
the cached value diverges from the asset's `version`. Hot-reloading a
shader file (via `FileWatcher`) bumps the shader version, which evicts
every variant compiled from it; the next material draw recompiles lazily.

## Storage

Every asset table is a `Storage<T>`, a generational arena (slot map):

- O(1) add, remove, look-up.
- O(n) dense iteration.
- Swap-and-pop removal keeps data packed.
- Generation checks make stale handles a no-op (not a crash).

## Tools: loaders and generators

Procedural generators and file loaders live in `src/tools/`. They are
**not** part of the engine library; they register factory lambdas into
`AssetFactories` at startup (`tools/asset_registration.cpp`) so the
engine-side `AssetSerializer` can recreate any asset by dispatching its
`kind` field.

### Generators (`src/tools/generator/`)

| File                    | Provides                                                       |
|-------------------------|----------------------------------------------------------------|
| `mesh_generators.cpp`   | Triangle, plane, cube, sphere, pyramid, cone, preview shapes   |
| `texture_generators.cpp`| Solid color, white, black, normal, gray (1x1 fallback)         |
| `material_generators.cpp`| Default PBR material with the fallback texture suite          |
| `light_generators.cpp`  | Pre-baked directional / point / spot light components          |

### Loaders (`src/tools/loader/`)

| File                    | Provides                                                       |
|-------------------------|----------------------------------------------------------------|
| `texture_loaders.cpp`   | Load via stb_image, auto-detect channels, sRGB flag handling   |
| `material_loaders.cpp`  | Folder loader: scans a folder for `*Color*`, `*Normal*`, etc.  |
| `model_loader.cpp`      | Assimp-backed mesh import; per-load aiScene parse cache        |
| `shader_loaders.cpp`    | Load a `ShaderAsset` by path prefix; registers with FileWatcher|

## Save/load round-trip

See [IO and serialization](system/io.md) for the full flow.
`AssetSerializer::saveAssetsForScene` emits only the assets actually
referenced by the scene's `Mesh` components (plus their material's
texture references) and skips any with `internal = true`. On load,
assets with the same `name` already in the manager are skipped (loads
are idempotent), and new assets go through `AssetFactories` dispatch
by `kind`.
