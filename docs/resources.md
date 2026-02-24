# Resource Management

The `ResourceManager` handles all GPU-uploadable assets with typed handles and version-based change tracking.

## Key Files

- `src/engine/resource/resource_manager.h` -- ResourceManager
- `src/engine/resource/resource.h` -- Resource base (version counter)
- `src/engine/resource/resource_handle.h` -- Type-safe handles
- `src/engine/resource/mesh_asset.h` -- MeshAsset
- `src/engine/resource/texture_asset.h` -- TextureAsset
- `src/engine/resource/material_asset.h` -- MaterialAsset
- `src/engine/core/memory/storage.h` -- Generational arena backing store

## Handles

Resources are accessed through type-safe generational handles:

```cpp
using MeshHandle     = Handle<MeshAsset>;
using TextureHandle  = Handle<TextureAsset>;
using MaterialHandle = Handle<MaterialAsset>;
```

Handles wrap a `StorageIndex` (index + generation), so stale handles are detected automatically.

## API

```cpp
ResourceManager& rm = engine.getResources();

// Add a resource (returns typed handle)
MeshHandle handle = rm.add(MeshAsset{...});

// Read-only access
const MeshAsset& mesh = rm.get(handle);

// Mutable access for editing
MeshAsset& mesh = rm.edit(handle);
mesh.vertices.push_back(...);

// Commit changes (bumps version -- triggers GPU re-upload)
rm.commit(handle);

// Remove
rm.remove(handle);
```

## Asset Types

### MeshAsset

```cpp
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;
};

struct MeshAsset : Resource {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    glm::vec3 boundsMin, boundsMax;
};
```

### TextureAsset

Extends `Resource` + `Core::Texture2DParams`. Holds raw pixel data, sRGB flag, and file path.

### MaterialAsset

Full PBR material with properties:
- Albedo (vec4), emission (vec3), metallic, roughness, IOR, transmission
- Alpha, AO, clearcoat, clearcoat roughness, anisotropy, subsurface
- 11 texture handles (albedo, normal, metallic, roughness, metallicRoughness, aoMetallicRoughness, ao, emission, height, clearcoat, transmission)
- MaterialType: Opaque, Transparent, Unlit

## Versioning

Every asset inherits a `uint64_t version` from `Resource`. Calling `rm.commit(handle)` bumps both the per-resource version and a global version counter. The GPU backend (`GLView`) checks versions to skip redundant uploads.

## Storage

Resources are stored in `Storage<T>`, a generational arena (slot map) with:
- O(1) add, remove, lookup
- O(n) dense iteration
- Swap-and-pop removal keeps data packed
- Generation checks prevent stale handle access

## Tools (Generators & Loaders)

Procedural generators and file loaders in `src/tools/`:

### Generators (`src/tools/generator/`)

- `MeshGenerators` -- Triangle, plane, cube, sphere, pyramid, cone
- `TextureGenerators` -- Solid color, white, black, normal, gray (1x1 pixel textures)
- `MaterialGenerators` -- Default PBR material with fallback textures
- `LightGenerators` -- Directional, point, spot light components

### Loaders (`src/tools/loader/`)

- `TextureLoaders` -- Load from file via stb_image, auto-detect channels
- `MaterialLoaders` -- Load PBR material from folder (scans for texture naming patterns: Color/Albedo, Normal, Roughness, etc.)
