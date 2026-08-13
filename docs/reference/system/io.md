# IO and Serialization

The engine persists scenes to JSON and watches the filesystem for
shader hot-reload. The IO layer has three serializers that compose,
plus a polling file watcher.

## Components

| Layer               | File                                         | Purpose                                                                                  |
|---------------------|----------------------------------------------|------------------------------------------------------------------------------------------|
| Scene serializer    | `src/engine/io/scene/scene_serializer.h`           | Top-level save/load for a `Scene` + the assets it references. Transactional.             |
| Asset serializer    | `src/engine/io/asset/asset_serializer.h`           | Save name-only asset references; on load resolve them via the asset library + the `AssetFactory` seam. |
| Asset library       | `src/engine/io/asset/asset_library.h`              | The cooked-asset database manifest: maps an asset name to its recipe + cooked file + hash. |
| Asset cooker        | `src/tools/cook/asset_cooker.h` (editor)     | Bakes assets from their recipe into the library + cooked binary cache (`cooked/`).        |
| Component serializer| `src/engine/io/scene/component_serializer.h`       | Per-component to/from JSON. Mechanical, one save/load pair per component type.           |
| Shader reload       | `modules/vkmGL/src/shader/gl_shader_reload.h`      | Live-shader registry + `reloadChangedShaders`; rebuilds a program whose source's `mtime` moved, keeping the old one on a compile error. |

## SceneSerializer

`SceneSerializer::save` emits a JSON object with two top-level blocks:

- `assets`: name-only references to every mesh / material referenced by a
  `Mesh` component, plus the textures the materials use. The asset *data* lives
  in the cooked library (keyed by name), not in the scene file, so the scene
  stays tiny and diff-friendly. Assets marked `hidden = true` are skipped
  (editor previews, fallback textures, bundled primitives).
- `entities`: one record per entity. Entities are stored at their slot
  index, and each component is keyed by its short name (see Component
  serializer below).

`SceneSerializer::load` is **transactional for both entities and assets**:

1. Read the file (early-out on parse failure; live scene untouched).
2. Resolve each asset reference through the `AssetLibrary` manifest into a
   **staging** `ResourceManager` (not the live one): meshes/textures load from
   their cooked binary, materials from their library `inline` form. Idempotent:
   assets already present by `name` are skipped. Runs inside a guard so a
   malformed assets block logs and aborts the load with the live state intact.
3. Deserialise every entity into a **staging** `Scene` (not the live
   one). Failures here also leave the live scene untouched.
4. On full success, swap both staging containers in one step:
   `Scene::swap` for the scene, and `ResourceManager::swap` (plus
   `swapSlot<ShaderAsset>`) for the assets.

`Scene::createEntityAt(slotIndex)` is what makes step 3 possible:
entities recreate at their saved slot, so `Hierarchy::parent` indices
in the file resolve directly without a remap step.

Because both the scene and the assets are staged and swapped only on full
success, a malformed or partial file leaves the live `Scene` **and** the live
`ResourceManager` untouched - there are no orphaned half-loaded assets. The .cpp
documents this inline.

After load, the caller should:

- Clear the `CommandStack` (entity IDs and component topology are no
  longer comparable across the swap).

`SceneIOController` in the editor handles these.

## Cooked assets: AssetSerializer, AssetLibrary, AssetFactory

The asset pipeline is **cooked-content + an asset database**. The *recipe* (the
original `source` JSON-with-`kind` descriptor a generator/importer produces) is
the editable source of truth; a *cooked* file is a derived binary cache keyed by
a hash of the recipe. Every asset is its own file:

- `library/<type>/<uid>.json` - the recipe (and, for materials, the canonical
  `inline` form). The version-controlled source of truth.
- `cooked/<type>/<uid>.vkmc` - the derived binary blob (mesh vertices/indices;
  decoded texture pixels). Regenerable; git-ignored.
- `library/_manifest.json` - maps each asset `name` to its recipe + cooked file
  and recipe hash. `AssetLibrary` is the in-memory view, loaded at startup.

**Save** - `AssetSerializer::saveAssetsForScene` walks `Mesh` components and emits
**name-only** references to the meshes/materials/textures used. In the editor,
`SceneIOController` first calls `AssetCooker::cookAllAssets`, which bakes every
non-hidden asset in the `ResourceManager` into the library + cooked cache and
rewrites the manifest (skipping assets whose hash is unchanged).

**Load** - `AssetSerializer::loadAssets` resolves each name through the manifest:
meshes/textures get a synthesized `{"kind":"cooked","name":...}` source, materials
load their `inline` descriptor from the library file. Both go through the
`AssetFactory` dispatch seam (`io/asset/asset_factory.h`) - three function pointers
(mesh / texture / material) that each binary wires at startup, with plain
switch dispatch on the `kind` field:

| `kind`                 | Handled by       | Resolves to                                         |
|------------------------|------------------|-----------------------------------------------------|
| `cooked`               | runtime + editor | A mesh/texture read from its cooked binary (async)  |
| `inline`               | runtime + editor | A `MaterialAsset` from PBR scalars + texture refs   |
| `generator` / `decimate` | editor only    | Procedural / LOD meshes (run by the cooker)         |
| `file` / `model` / `model-image` | editor only | stb / Assimp texture + mesh import          |
| `folder` / `model` / `default` / `builtin` / `solid` | editor only | material + texture recipes |

The runtime wires only the cooked dispatch (`registerCookedAssetFactories` sets
the pointers to `createCookedMesh/Texture/Material`), so it links neither Assimp
nor the image decoders. The editor instead wires the recipe dispatch
(`registerRecipeAssetFactories`, built into the editor-only `EngineCooker`),
whose switches fall through to the cooked functions for cooked/inline kinds.
Engine code never reaches into `src/tools/`; the dispatch is wired at startup in
`src/tools/asset_registration.cpp` (cooked) and
`src/tools/cook/recipe_registration.cpp` (recipe).

Adding a new asset kind means adding a `case` to the dispatch switch; the
serializer itself does not change.

## ComponentSerializer

For every component, there is a `save(const T&) -> json` and a
`load(const json&, T&)`. They are intentionally mechanical, one pair
per component.

Today's coverage (the `SerializedComponents` tuple in `scene_serializer.cpp`):

- `Name`, `Transform`, `Camera`, `Light`, `Mesh`, `Animation`
- `Rigidbody`, `Collider`, `PhysicsWorld` (physics; runtime sleep state and
  derived mass properties are not persisted - see [Physics](physics.md)).
- `ScriptComponent` (JSON key `"Script"`): each behavior stored by its registered
  type name and recreated through `BehaviorRegistry` on load (unknown types are
  dropped). Per-behavior fields are not yet written to the scene file - see
  [Scripting](scripting.md).
- `Hierarchy` (only `parent` is serialized; sibling pointers are
  rebuilt on load by re-running `HierarchyOperations::setParent`).

`Environment` (the scene's lighting environment: HDR path, intensity, skybox
toggle) is serialized separately, alongside the entities. Render tuning (GTAO /
bloom / MSAA / ...) lives in `RenderSettings` on the RenderSystem, not in a
serialized component.

`Mesh` references handles by `name` rather than by `Storage` index;
that is what makes assets a stable identity across save/load.
`Hierarchy::parent` stores the old-file entity slot index, which the
loader uses directly because entities are recreated at the same slot.

### Trait-based fold

`scene_serializer.cpp` defines a `SerializerTraits<T>` specialization
for each component type that just calls
`ComponentSerializer::save`/`load`. A `SerializedComponents` tuple
lists every supported component type, and the entity save/load loop
folds across the tuple at compile time. Adding a new component to the
save/load coverage is three lines:

1. A new save/load overload in `component_serializer.h`.
2. A `SerializerTraits` specialization in `scene_serializer.cpp`.
3. The component type appended to the `SerializedComponents` tuple.

No registry tables, no virtual dispatch, no macros.

## FileWatcherSystem

`FileWatcherSystem` is a `System` for `SystemStage::Input`. It polls registered
directories on a configurable interval (default 0.5 s), `stat()`s each
file, and fires the registered `OnChange` callback the next time a
file's `mtime` differs from the last seen value.

It is provided by the engine but **not** registered by the default
`setupEngineApp` wiring today; an app opts in explicitly:

```cpp
auto& watcher = engine.addSystem<FileWatcherSystem>(SystemStage::Input);
watcher.watch("shaders/forward/pbr", [&]{ resources.commitShader(pbrHandle); });
```

The typical use is shader hot-reload: when a `.shader` source file
changes, `commit` on the corresponding `ShaderAsset` bumps its version,
which drops its compiled program, and the next draw recompiles lazily.

It's polling-based, not platform-specific (no `inotify`/`fsnotify`), so
it works the same on every host. The poll cost is tiny because each
entry only `stat()`s its own file list, batched per tick.
