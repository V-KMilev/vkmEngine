# Resource Management

`ResourceManager` is the single owner of every asset the engine loads: meshes,
textures, materials, fonts, skeletons, animation clips and sounds. Assets are referenced
from components and the render view through type-safe generational handles, and
the GPU-uploadable ones sync through a per-resource version counter.

It stores those types and no others (`IS_ENGINE_ASSET`), each in a slot made
when the manager is. A slot made on first use for a type a gameplay module
declared would carry the module's code, and outlive the module across a script
reload, so storing one is a compile error.

## Key files

- `src/engine/resource/resource_manager.h` for the manager
- `src/engine/resource/resource.h` for the `Resource` base (name, version, uid, hidden flag, source JSON)
- `src/engine/resource/resource_handle.h` for type-safe `Handle<T>`
- `src/engine/resource/asset_type.h` for `AssetType`, the kind tag every asset is filed and referenced under
- `src/engine/resource/asset/` for the asset kinds - `mesh_asset.h`, `texture_asset.h`, `material_asset.h`, `font_asset.h`, `skeleton_asset.h`, `animation_clip_asset.h`, `audio_clip_asset.h`
- `src/engine/core/memory/sparse_set.h` for the `SparseSet<T>` that backs each asset table
- `src/engine/resource/asset_source_kind.h` for the `kind` tag on an asset's source descriptor
- `src/engine/resource/texture_format.h` for the backend-agnostic texture descriptors
- `src/engine/resource/generate/` for the assets the engine makes rather than loads - `mesh_generators.h` and `default_scene.h` (the minimum a new scene needs to be worth looking at)
- `src/tools/cook/lod_generator.h` (simplifies a mesh into an `LOD`) and `src/tools/cook/mesh_processing.h` (the simplifier, and the draw order every cooked mesh is baked in) - authoring and cook-time work over meshoptimizer, which the runtime never links

## Handles

```cpp
using MeshHandle          = Handle<MeshAsset>;
using TextureHandle       = Handle<TextureAsset>;
using MaterialHandle      = Handle<MaterialAsset>;
using FontHandle          = Handle<FontAsset>;
using SkeletonHandle      = Handle<SkeletonAsset>;
using AnimationClipHandle = Handle<AnimationClipAsset>;
using AudioClipHandle     = Handle<AudioClipAsset>;
```

Each handle wraps a `StorageIndex` (index + generation), so a stale handle is
*detectable* - ask `isAlive(handle)` before reaching through one you did not
just make. It is not safe to use: `get`, `edit` and `remove` assert on it, and
`VKM_ASSERT` compiles to nothing in release, so a stale handle aborts a debug
build and reads freed storage in a shipped one. The generation exists so you can
tell, not so the manager can absorb the mistake.

A handle is a *runtime* identity: it names a slot in one session's
`ResourceManager`, which is why serialization stores names instead. The authored
counterpart is `AssetRef<Asset>` (`resource/asset_ref.h`) - a name plus the
asset kind it names - which is how a behavior field points at an asset and how
that asset ends up in the scene's assets block. See
[scripting.md](scripting.md#naming-an-asset).

## API

```cpp
ResourceManager& rm = engine.getResources();

// Add a resource (returns a typed handle)
MeshHandle handle = rm.add(MeshAsset{ ... });

// Add with an explicit name; the name becomes the cross-save-load identity.
// A name is a declaration - naming one that exists replaces its contents and
// hands back the handle that already had it, so this is safe to run twice.
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

Every asset inherits `Resource`. Its four identity fields are private, read
through const accessors, and written only by `ResourceManager` - the one friend
the class has. The manager keeps a per-type name index and guarantees names are
unique and non-empty, so a name written behind its back would leave
`findByName` looking for a string the asset no longer carries - which is why
nothing else can write one.

One name is one asset, and `add()` holds that by **replacing** what stands under
a name it is given rather than suffixing the newcomer into `wall_512 (2)`. That
is what makes adding by name repeatable, which is what a script reload needs -
`onStart` runs again, so a behavior that builds an asset builds it a second
time. The suffix is still there for an asset that arrives with **no** name, which
is claiming no identity, and for `rename()`, where a collision is a slip rather
than a request. A caller that wants a second asset asks for a free name and says
so by passing one - the editor's "New Material" picks the name before it calls.

| Accessor      | Type                                  | Notes                                                                                       |
|---------------|---------------------------------------|---------------------------------------------------------------------------------------------|
| `name()`      | `const std::string&`                  | Stable identity for serialization and look-up. Assigned by `add(asset, name)`, changed by `rename()` |
| `version()`   | `uint64_t`                            | Started at 1 by `add()`, moved on by `commit()`, `swapValue()` and an `add()` that replaced this asset; backends compare to skip re-upload |
| `uid()`       | `uint64_t`                            | Process-unique instance id stamped by `add()`. A handle names a slot; this names the asset in it, which is how an async completion knows the graph did not change under it |
| `isHidden()`  | `bool`                                | When true, filtered from pickers / Asset Browser / scene save (previews, fallbacks). Set via `addPrivate()` |
| `sourceJson()`| `nlohmann::json&`                     | The asset's recipe (loader/generator descriptor). The editor cooker bakes it into the library + cooked cache; scenes reference the asset by name, not by this descriptor |

The source descriptor is held by `unique_ptr` against a forward-declared
`nlohmann::json` so headers don't drag the JSON header in; that is why
`Resource`'s Rule of 5 is defined out of line in `resource.cpp`, where the full
type is visible. `hasSource()` tests the slot, and the const `sourceJson()`
asserts rather than allocating.

`addPrivate()` is what marks an asset hidden, and the editor is its only caller:
the Asset Browser's preview sphere and neutral thumbnail material, the Material
Editor's preview primitives. The cooker leaves them out of the manifest and
`AssetSerializer` refuses to write a reference to one, so save files contain
only user-relevant content.

## Asset types

Every kind the asset library holds is named by one enum, `AssetType`
(`resource/asset_type.h`). Nothing on disk carries its numeric value: the
manifest and the scene write the name, and a cooked file carries its own kind
tag. It sits beside the assets rather than inside `AssetLibrary` because naming
a kind is not the same job as owning the database, and code that needs only the
tag should not have to include the manifest, its map and `<filesystem>` with it.
`FontAsset` is the one kind with no value there - a font is baked at startup,
not cooked into the library.

### MeshAsset

```cpp
struct Vertex {          // 48 bytes, and it stays 48
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec4 tangent;   // xyz along +U; cross(normal, xyz) * w along +V
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
    bool                    loading;     // an async decode is in flight
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
radius* (see [Visibility](visibility.md)), and the culls keep exactly
what the box says, so an under-sized box does not over-draw, it deletes the
character.

`skeleton` is a **name, not a handle**: a compatibility tag rather than a
dependency. The mesh uploads its skin stream either way and the pose it is drawn
with comes from whatever rig is driving it, so the name is what lets the runtime
report the failure that actually happens - a rig assigned to the wrong character
- instead of exploding the geometry and leaving the cause to be guessed at.

### TextureAsset

`TextureAsset` extends `Resource` plus the engine-level `TextureParams`
(`width`, `height`, `internalFormat` / `format` / `type`, `wrapS` / `wrapT`,
`filterOverride`, `generateMipmaps`, `mipLevels`). It owns the raw `pixelData` -
every level `mipLevels` names, level 0 first; an imported texture is named by
its file, so `name()` says where it came from. The backend converts the params
to GL state at upload time. The runtime frees the pixels once the backend holds
them (`RenderSystem::releaseUploadedPixels`, asked through
`RenderBackend::holdsPixels` for the upload of the asset's current version),
since a game never reads them again; the asset keeps its params. The
editor keeps them, because its cook bakes from them. That an asset has no
pixels is therefore not that it failed: the backend's missing-texture warning
asks its own mirror whether the pixels ever arrived.
What the texels mean is its `TextureUsage` - `Color` (albedo, emission: sRGB,
filtered as light), `Data` (roughness, masks, packed maps: linear numbers) or
`Normal` (a tangent-space direction). Nothing in an image file says which, so
whoever imports a texture states it - a material slot knows what it samples -
and the recipe records it. Once decoded, the usage and the colour space are
`internalFormat`'s and nothing else's: each usage is stored in formats no other
takes (colour the sRGB ones, a normal `RG8`/`BC5RG`, data the rest), and
`isSrgb()` and `usage()` read them back off it. An import still decoding cannot
choose its format until the channel count is known, so its stub carries the
four-channel format of the right usage, and the decode settles which of that
usage's formats it is.

A normal map keeps its x and y alone, as `RG8`, and the PBR shader rebuilds z
from them; storing z buys nothing and costs a block format that holds two
channels better than any that holds three. The decode keeps a file's own
channel count otherwise, except where no format would sample it the way the
shader reads it: grey and alpha, and grey colour, are widened to four channels
(`decodeChannels`, `resource/texture_format.h`). A grey data
file stays one channel, and the backend swizzles a one-channel
texture (`R8`, `BC4R`) to read its red in all three colour channels with an
opaque alpha - so a grey roughness map bound where a shader reads roughness from
green and metalness from blue gives it the grey, as the file decoded to colour
would, rather than zero. A model's maps that sit beside it as files are loaded
by `loadTexture` like any other file texture, so the import and the recipe that
reloads it decode them by the one rule. Pixel rows are tightly packed, which
is why the backend sets `GL_UNPACK_ALIGNMENT` to 1 when it starts.

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

A file texture states it in its recipe, beside `usage` and `generateMipmaps`:

```json
{ "kind": "file", "path": "assets/ui/hud.png", "usage": "Color", "filter": "nearest" }
```

The key is absent from a texture that has no opinion, which is nearly all of
them. Code that builds a `TextureAsset` directly sets `params.filterOverride`
instead. The editor has no per-texture import panel, so those two are the
authoring surface.

#### What the cook makes of a texture

An import holds what the file decoded to: one level of texels. What is cooked
is what the GPU samples, built once by the cooker (`AssetCooker::bakeTexture`,
`src/tools/cook/texture_bake.cpp`) rather than at every load:

- **The whole mip chain.** A texture whose recipe asks for mipmaps carries
  every level down to 1x1 (`mipLevels`); one that does not carries one. Each
  level is filtered from the one above it with `stb_image_resize2`'s default
  kernel, the wrap mode as its edge rule, in linear floats, and rounded to
  bytes once, as it is stored - a level filtered from the rounded bytes above
  it inherits their rounding, half a byte a level in whichever direction the
  content favours. An sRGB texture's colour is decoded to light first and
  weighted by alpha, so a mip of black beside white is the sRGB byte of half
  the light (about 188), not half the byte (128). A linear four-channel texture
  is data (a packed mask) and its channels are filtered apart.
- **Coverage.** A colour texture's alpha is what an alpha test cuts on, and
  filtering smears a cut-out's thin parts into a haze under the cutoff, so
  foliage thins to bare branches as it recedes. Each level's alpha is scaled -
  the scale found by bisection - so the share of its texels above the cutoff is
  level 0's (Castano's coverage-preserving mips). The cutoff is 0.5, the alpha
  test's default: a texture is cooked once for every material that samples it
  and knows none of them. A level whose share already matches, which is every
  level of an opaque texture, is left alone. A blended texture gets the same
  treatment, which reads as its edges staying a little firmer at range.
- **Normal detail as roughness.** A metallic-roughness map whose recipe names
  its material's normal map (`roughnessNormal`, set by the model import when a
  glTF material has both, and carried through a re-import) takes, at each level,
  the roughness the normal detail its texels average away would have spread a
  highlight to: a box pyramid of the normal map, not renormalised, gives each
  footprint's mean normal, whose shortness is the spread (Toksvig, in the von
  Mises-Fisher form of Neubelt and Pettineo's *The Order: 1886*: `kappa =
  (3l - l^3) / (1 - l^2)`, `alpha^2 += 2 / kappa`). Without it a glossy bumpy
  floor at range is a mirror that sparkles where its renormalised mips line up -
  the aliasing no temporal filter is here to average. Only G (roughness) moves;
  the level filtered from keeps the plain value, so the spread is not counted
  again at every level below; the top level, whose texels each see one normal,
  is left alone. The normal map is decoded from its source file through
  `AssetFactory::decodeTexture`, never taken from the graph, which may hold it
  cooked, and its file is in the roughness map's key, so a re-exported normal
  map re-bakes both. A map shared under two normal maps keeps the first pairing.
- **Blocks.** Each level is then compressed into the block format
  that matches what was stored, so no shader changes: `R8` becomes `BC4R`
  (RGTC1), `RG8` `BC5RG` (RGTC2), `RGB8` and `RGBA8` `BC7RGBA` and the two sRGB
  formats `BC7SRGBA` (BPTC). Colour is encoded with perceptual weights, data
  with linear ones. An RGBA8 2048x2048 map goes from 16 MB to 4 MB, and its
  chain adds a third.

Three kinds of texture stay texels: one smaller than a block (under 4x4 - the
1x1 fallbacks) and one that states `Nearest` (pixel art and lookup tables are
wrong when a block's endpoints round them, for the reason they asked not to be
blended) still get their chain; one that is not 8-bit is passed through as it
loaded, with the one level it came with.

The runtime then only uploads. A texture that carries levels or blocks is
created empty and has each level specified as it came (`glCompressedTexImage2D`
for blocks), with `GL_TEXTURE_MAX_LEVEL` pinned to the last; GL builds a chain
only for a texture that asks for one and carries a single uncompressed level
(`buildsMipsAtUpload`) - an import the editor drew before it was cooked, or a
float texture. `isMipmapped` is the one answer to "does this texture sample
through a chain", and the filter resolve reads it.

Two encoders do the blocks. BC4 and BC5 are the engine's own
(`src/tools/cook/bc4_encoder.h`): each block tries both of the format's
palettes, starts each from the block's extremes and refines the endpoints by
least squares against the indices the texels chose, a few rounds - which takes
about a quarter off the error of stopping at the extremes on noisy blocks, and
stays in integers, so its bytes are the same on every build. BC7 is
basis_universal's scalar bc7e, maintained upstream, the one file of that submodule compiled,
at its `basic` level, a row of blocks per `ThreadPool` task. Measured on
project_alpha's 2048x2048 art with 8 threads (RGB PSNR):

| Level      | Colour map        | Normal map        |
|------------|-------------------|-------------------|
| ultrafast  | 0.2 s, 46.3 dB    | 0.1 s, 30.8 dB    |
| veryfast   | 1.1 s, 47.0 dB    | 1.5 s, 35.1 dB    |
| **basic**  | **1.5 s, 47.1 dB**| **5.1 s, 36.0 dB**|
| slow       | 3.9 s, 47.1 dB    | 4.7 s, 36.1 dB    |

`basic` is where a normal map stops improving, and a normal map is where BC7
loses most; colour is past what an eye can tell at every level from veryfast.
A full cook of a project pays this once - a texture re-bakes only when its
recipe or its source moved. A normal map does not go through BC7 at all: it is
stored as x and y and cooked to BC5, each level renormalised before it is
stored, so the table's normal-map column is what BC7 would make of one. project_alpha's cooked
textures shrink from 1450 MB (level 0 alone) to 670 MB with every chain.

### MaterialAsset

Full PBR material. The scalar properties cover:

- Albedo (`vec4`), emission (`vec3`), metallic, roughness, IOR, transmission
- Alpha cutoff, double-sided (both faces drawn, a back face lit as its own; glTF's `doubleSided`), AO, clearcoat, clearcoat roughness, anisotropy
- Subsurface, sheen, parallax/height

It carries texture handles for albedo, normal, metallic, roughness, a
combined metallic-roughness slot, AO-metallic-roughness (glTF), AO,
emission, height, clearcoat, transmission.

All optional PBR features are runtime toggles: one shared PBR ubershader
branches on the individual material scalars and texture-present uniforms at
draw time. There is no feature bitset and no per-variant compiled shaders; see
[Rendering](rendering.md).

`MaterialType` is `Opaque = 0`, `Transparent = 1`, `Unlit = 2`, or
`AlphaMask = 3`. The backend partitions the visible draws by type
(`GLBackend::partitionDrawables`): Opaque and Unlit share the opaque bucket and
AlphaMask has its own, both writing depth; Transparent is drawn last, back to
front, after one copy of the opaque + sky scene that a transmissive surface
refracts.

### FontAsset

A baked SDF glyph atlas: the atlas pixels, its dimension, the vertical
metrics, and a per-glyph table. Self-contained on purpose - it owns its texels
rather than a `TextureHandle` - which is what lets it survive a scene load: the
font is engine-owned (baked once at startup, never written to a scene file), so
`ResourceManager::swap` and `clear` leave the font slot where it is rather than
trading it away with the rest of the graph. See [In-game UI](ui.md).

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
index also maps straight onto a rigid body: a `RagdollBone` names its bone by
index.

**`parent < index` is a validated invariant**, not a convention. The importer
emits bones depth-first, and `findSkeletonFault` (`skeleton_asset.h`) states the
rule once: the cooked reader and writer refuse a skeleton that breaks it, and
`SkeletalAnimationSystem` leaves a rig built in code that breaks it unposed.
`findClipFault` is the same for a clip. That is what makes composing a pose one forward
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
struct ClipMarker  { std::string name; float time; };  // an instant the clip announces

struct AnimationClipAsset : Resource {
    std::string skeleton;          // rig whose bone order `bones` addresses
    float       duration = 0.0f;   // seconds, stored rather than derived
    std::vector<ClipBone>   bones;    // parallel to that rig's bones
    std::vector<ClipMarker> markers;  // in time order; empty for an unmarked clip
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
keeps `AnimationTrack<T>` (see
[Animation](animation.md)).

A clip is bound to its rig **at cook time**: `bones` is parallel to the named
skeleton's bone array, so nothing resolves a bone name at runtime.

**Markers** are what the clip announces as it plays - a footstep, the frame a
swing connects. They live on the clip and not on the `Animator` that plays it,
because a footstep belongs to the walk: every character playing that walk gets
the same footsteps without authoring them again, and retiming the walk moves
them with it. Crossing one publishes an `AnimationEvent` on the `EventBus`; see
[Animation](animation.md#animation-events).

Nothing in glTF or FBX carries an animation event, so a marker is **authored in
the clip's recipe** rather than imported, beside the path and the clip index:

```json
{ "kind": "model", "path": "assets/hero.glb", "clip": 2,
  "markers": [ { "name": "footstep", "time": 0.12 },
               { "name": "footstep", "time": 0.42 } ] }
```

The loader drops a marker with no name or a time outside the clip (it could
never fire at the instant it names), sorts what is left by time, and writes it
back into the clip's own `source` - which is what stops the next cook from
regenerating the recipe without the markers it just read.

### AudioClipAsset

A sound, decoded in full at load: 16-bit interleaved PCM at the rate and
channel layout the source file carried.

```cpp
struct AudioClipAsset : Resource {
    uint32_t sampleRate = 0;   // as authored; the mixer resamples if it differs
    uint32_t channels   = 0;   // only a mono clip can be meaningfully positioned
    ClipSamples samples;       // the PCM, shared with the voices playing it
};
```

The one asset payload in the engine whose ownership is shared, and the reason
is the mixer: a playing voice reads those samples from the audio thread while a
scene load frees the asset from the main thread without asking. Sharing
ownership with the voice turns that from a use-after-free into a sound that
keeps playing for the one frame it takes `AudioSystem` to notice.

A clip is decoded at load, not streamed: a streamed clip would be the only
asset that keeps a file open past its load, and a `Resource` is a value a scene
load builds in a staging manager and swaps in whole. See [Audio](audio.md) for
the full argument and what it costs.

## Versioning

`commit(handle)` bumps the asset's own version counter. `GLView` keys on
this per-asset version: it keeps a per-asset cached version and rebuilds
GPU state only when the cached value diverges from the asset's `version()`.

A version only tracks edits *within* one asset graph. A wholesale replacement
(scene load, editor play-stop restore) is what `epoch()` is for: the incoming
graph restarts at the same indices, generations and versions, so the backend
compares the epoch and drops every mirror when it moves. `swap()` and `clear()`
both bump it.

## Storage

Every asset type gets its own `SparseSet<T>` (made with the manager) plus a
`SlotAllocator` for generational keys and a name index for O(1) lookup:

- O(1) add, remove, look-up.
- O(n) dense iteration.
- Swap-and-pop removal keeps data packed.
- The generation makes a stale handle *answerable*, which is not the same as
  absorbed - see [Handles](#handles). `isAlive()` and `tryGet()` report one;
  `get()`, `edit()` and `remove()` assert, and the assert is gone in release,
  where `remove()` alone stops at a guard and does nothing.

## Tools: loaders, generators, and the cooker

Recipe imports live in `src/tools/`, outside the engine core; they wire the
`AssetFactory` seam at startup so the engine-side `AssetSerializer` can import
any recipe a cooked file does not serve. The procedural generators are engine
code, in `src/engine/resource/generate/`, because the engine itself builds with
them - a default scene, a fallback texture. The tools split by dependency
weight:

- **`vkm_tools`** (runtime-safe): the HDR environment loader, the plain image
  decode, the SDF font baker (all three in `loader/`) and the host prologue in
  `project_boot`. The folder says the target: `import/` and `cook/` are
  `vkm_cook`, the rest of `src/tools/` is `vkm_tools`.
- **`vkm_cook`** (linked by the editor and `vkm_cook_app`, never the runtime): the heavy importers (`import/`: cgltf, ufbx and
  MikkTSpace for models, stb for images, the miniaudio decoder for sounds) and the asset cooker (`cook/`), plus
  `registerRecipeAssetFactories` and what the cooker bakes with: LOD generation
  (`cook/lod_generator.h`), the meshoptimizer passes in `cook/mesh_processing.h`,
  and the texture bake's mip filter and block encoder (`cook/texture_bake.h`).

The runtime registers no imports and reads cooked files only, so it links none
of the importers - model, texture or sound; the editor registers the recipe
imports and (re)cooks recipes into the cache.

### Generators (`src/engine/resource/generate/`)

| File                    | Provides                                                       |
|-------------------------|----------------------------------------------------------------|
| `mesh_generators.cpp`   | `generateTriangle/Plane/Cube/Sphere/Pyramid/Cone/Cylinder` free functions |
| `texture_generators.cpp`| Solid color, white, black, normal, gray (1x1 fallback)         |
| `material_generators.cpp`| Default PBR material with the fallback texture suite          |
| `light_generators.cpp`  | `generateLight(LightType)` - a light component with that type's defaults |

The generators are plain free functions. Each stamps the recipe it was made
from, and the string dispatch back (`"cube"` -> generator) is
`createGeneratedMesh` and `createGeneratedTexture` in the same two files, so
one file writes a generator's keys and reads them; the recipe factories in
`src/tools/cook/recipe_registration.cpp` hand those kinds to them.

### Importing a model

An import is two steps. `parseModel` (`import/model_source.h`) reads the file
into a `SourceModel` - the engine's own terms, whatever the format - and
`model_loaders.cpp` builds every asset and the scene from that, once for every
format. A process-wide cache holds the last eight parsed files, because one file
yields several assets and the recipe factories ask for them one at a time.

| Format | Library | What the parse does to reach the engine's space |
|--------|---------|--------------------------------------------------|
| glTF, GLB | cgltf | Already right-handed, +Y up, metres. V is flipped, because glTF's runs down the image and the engine's up; an authored tangent's `w` already means the engine's. |
| FBX, OBJ | ufbx | Converted at load to right-handed, +Y up, metres, with the conversion folded into geometry, node transforms and keys (`UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY`), so a centimetre rig arrives as a metre one with unit scale on every joint. Pivots stay in the node transforms (`UFBX_PIVOT_HANDLING_RETAIN`), so every export of one rig names and places its joints alike. OBJ is assumed to be in metres already. |

Every mesh is read as three corners per triangle, given tangents and then
welded: corners equal in every byte, skin included, become one vertex, in the
order each first appears. A mesh that authored tangents keeps them; every other
one gets MikkTSpace's, computed per corner before the weld as its reference
integration does - it is the frame bakers bake normal maps against, so it is
the only one such a map shades correctly on. Its sign is the engine's `w`.

The asset names are deterministic so a re-import relinks: the file's project
reference and the part - `<ref>:mesh<i>`, `<ref>:mat<i>`, `<ref>:skeleton` (one
rig per file), `<ref>:clip<i>` and `<ref>:emb:<image>` - so
`assets/props/crate.glb:mesh0`. The reference, not the stem, because two files
with one stem in two folders are two models. The indices are the file's own
order, which makes that order part of the format:

- **Meshes.** glTF: every primitive of mesh 0, then of mesh 1. FBX and OBJ: by
  node, depth-first, each mesh split by material in the order its faces first
  use them (the FBX SDK's order); an instance after the first shares them.
- **Materials.** glTF: the file's, and a primitive with none takes the one past
  the last, built with the spec's defaults. FBX and OBJ: by first use along the
  same walk. A value an FBX does not state keeps the engine's default.
- **Clips.** glTF: the file's animations. FBX: the takes that animate
  something - an exporter's empty "Take 001" is not a clip, and counting it
  would renumber every clip after it.
- **Images** a file carries are referenced as `*<index>` - glTF's image index,
  FBX's texture index - and decoded through the `model-image` recipe.

The rig is the union of every joint any of the file's meshes names, plus the
nodes joining them down from their lowest common ancestor, emitted depth-first
so `parent < index` holds by construction. A file holding two rigs - joints
down more than one branch of a node that is not itself a joint - is refused
rather than merged into one with an invented shared root and a single bone
numbering that no clip in the file is bound to.

Clips resolve their channels to bone indices **at import**, against that same
rig. A channel naming a node outside it - a camera, a prop, the mesh node an
exporter animated - is dropped and counted. An FBX take is resampled by ufbx
into keys a linear sampler reproduces, with pivots, pre-rotations and the unit
conversion already in them; a channel that never moves keeps one key. A glTF
`STEP` channel is held by a key just before the next; a `CUBICSPLINE` one is
sampled at its keys, and its tangents are not read.

A vertex keeps its four strongest influences, renormalised among themselves
before they are quantised, so a fifth is never dropped after the others were
normalised against it. One that arrives with no influence at all is bound
rigidly to the rig root and counted, rather than left at zero weight -
`sum(w * M)` with every `w` zero collapses it onto the origin, which reads as a
broken importer instead of as one bad vertex.

The `import` test suite holds the space each format arrives in: a glTF, an OBJ
and a centimetre FBX written by the test, read back through `parseModel`.

### Importers (`src/tools/import/`, `vkm_cook`) and loaders (`src/tools/loader/`, `vkm_tools`)

| File                           | Provides                                                       |
|--------------------------------|----------------------------------------------------------------|
| `import/texture_loaders.cpp`   | Load via stb_image, channels decoded by the texture's usage    |
| `import/material_loaders.cpp`  | Folder loader: scans a folder for `*Color*`, `*Normal*`, etc.  |
| `import/model_source.cpp`      | `parseModel`: glTF/GLB through cgltf, FBX/OBJ through ufbx, into one `SourceModel`; MikkTSpace tangents and the weld; `loadSourceModel`, a process-wide cache of the last eight parsed files, each kept while the files it read are unchanged |
| `import/model_loaders.cpp`     | Mesh, material, rig, clip and scene import from a `SourceModel` |
| `import/audio_loaders.cpp`     | miniaudio-backed sound import: wav / mp3 / flac decoded to s16 |
| `loader/environment_loaders.cpp`| HDR equirectangular image loader (`loadHDRImage`) for IBL / skybox |
| `loader/image_loaders.cpp`     | Plain RGBA decode for what draws outside the asset graph - the splash logo |
| `loader/font_baker.cpp`        | `bakeFontSDF`: a TTF baked to an SDF atlas and kerning table    |

## Save/load round-trip

See [IO and serialization](io.md) for the full flow.
`AssetSerializer::saveAssetsForScene` emits only the assets actually
referenced by the scene - `Collider` (a mesh part's mesh), `Mesh` (mesh +
material), `LOD` (every level's mesh), `Decal` (its material), `Animator` (its
rig and clip), `AudioSource` (its sound), `UIImage` (its texture) and every
`AssetRef` field on a behavior, plus
the textures those materials reference. `emitDescriptor` gates every
reference a component holds as a handle, so a hidden or unnamed asset can
never be written from one. A behavior's `AssetRef` fields are walked with
them and go out through `emitNamedRef`: an authored name has no handle to
inspect, so it is written exactly as authored, and a name the library does
not hold is reported by `loadAssetSection` on load rather than dropped at
save. A component that writes an asset name into the scene file has to be
walked there, or the name has nothing to resolve against on load. On load,
assets with the same `name` already in the manager are skipped (loads
are idempotent), and new assets go through the `AssetFactory` dispatch
by `kind`.
