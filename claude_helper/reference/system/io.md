# IO and Serialization

The engine persists scenes to JSON and watches the filesystem for
shader hot-reload. The IO layer has three serializers that compose,
plus a polling file watcher.

## Components

| Layer               | File                                         | Purpose                                                                                  |
|---------------------|----------------------------------------------|------------------------------------------------------------------------------------------|
| Scene serializer    | `src/engine/io/scene_serializer.h`           | Top-level save/load for a `Scene` + the assets it references. Transactional.             |
| Asset serializer    | `src/engine/io/asset_serializer.h`           | Save/load the asset graph; dispatches `kind` through `AssetFactories`.                   |
| Component serializer| `src/engine/io/component_serializer.h`       | Per-component to/from JSON. Mechanical, one save/load pair per component type.           |
| File watcher        | `src/engine/system/io/file_watcher.h`        | Polling `mtime` watcher; fires a callback per changed file. Used for shader hot-reload.  |

## SceneSerializer

`SceneSerializer::save` emits a JSON object with two top-level blocks:

- `assets`: every mesh / material referenced by a `Mesh` component, plus
  the textures the materials use. Each asset is stored with its
  `source` descriptor (the original loader/generator JSON), so the
  loader can recreate it without re-reading the engine's runtime state.
  Assets marked `hidden = true` are skipped (editor previews, fallback
  textures, bundled primitives).
- `entities`: one record per entity. Entities are stored at their slot
  index, and each component is keyed by its short name (see Component
  serializer below).

`SceneSerializer::load` is **transactional at the scene level**:

1. Read the file (early-out on parse failure; live scene untouched).
2. Load assets into the live `ResourceManager`. This step is idempotent
   assets that already exist by `name` are skipped, so re-loading a
   scene does not duplicate textures.
3. Deserialise every entity into a **staging** `Scene` (not the live
   one). Failures here also leave the live scene untouched.
4. On full success, `Scene::swap` exchanges the live scene's storage
   with the staging scene's storage in a single step.

`Scene::createEntityAt(slotIndex)` is what makes step 3 possible:
entities recreate at their saved slot, so `Hierarchy::parent` indices
in the file resolve directly without a remap step.

Note on asset rollback: asset state is **not** transactional. If step 3
fails after step 2 has loaded several assets, those assets stay in the
live `ResourceManager`. They are harmless (just orphaned) and a manual
cleanup pass can remove them later. The .cpp documents this
trade-off inline.

After load, the caller should:

- Emit `SceneSerializer::SceneLoadedEvent` so subscribers
  (`CameraController`, panels, gameplay code) can refresh anything they
  cache across scene swaps.
- Clear the `CommandStack` (entity IDs and component topology are no
  longer comparable across the swap).

`SceneIOController` in the editor handles these.

## AssetSerializer and AssetFactories

`AssetSerializer::saveAssetsForScene` walks `Mesh` components, collects
the referenced meshes and materials, walks the materials to collect
referenced textures, and emits each asset's `source` JSON descriptor.

`AssetSerializer::loadAssets` dispatches each descriptor through
`AssetFactories`. `AssetFactories` is a registry of factory lambdas
keyed by the `kind` field of the descriptor:

| `kind`           | Resolves to                                       |
|------------------|---------------------------------------------------|
| `generator`      | A `tools/generator/` factory (triangle, cube, sphere, ...) |
| `file`           | A loader (texture from path, model via Assimp)    |
| `folder`         | The folder material loader (auto-discovers textures by naming) |
| `inline`         | A literal `MaterialAsset` written in place        |

Engine code never reaches into `tools/`. Factories are registered at
startup in `tools/asset_registration.cpp`:

```cpp
AssetFactories::get().registerMesh("generator", [](const json& d) {
    return MeshGenerators::byName(d.at("name"));
});
AssetFactories::get().registerTexture("file", [](const json& d, ResourceManager& rm) {
    return TextureLoaders::loadFromFile(rm, d.at("path"));
});
// ... etc
```

Adding a new asset kind means adding a factory entry; the serializer
itself does not change.

## ComponentSerializer

For every component, there is a `save(const T&) -> json` and a
`load(const json&, T&)`. They are intentionally mechanical, one pair
per component.

Today's coverage:

- `Name`, `Transform`, `Camera`, `Light`, `Mesh`, `Animation`
- `Hierarchy` (only `parent` is serialized; sibling pointers are
  rebuilt on load by re-running `HierarchyOperations::setParent`).
- `Environment` (the scene's lighting environment: HDR path, intensity,
  skybox toggle). Render tuning (GTAO / SSR / bloom / ...) lives in
  `RenderSettings` on the RenderSystem, not in a serialized component.

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

## FileWatcher

`FileWatcher` is a `System` on `SystemStage::Input`. It polls registered
directories on a configurable interval (default 0.5 s), `stat()`s each
file, and fires the registered `OnChange` callback the next time a
file's `mtime` differs from the last seen value.

```cpp
auto& watcher = engine.addSystem<FileWatcher>(SystemStage::Input);
watcher.watch("shaders/pbr", [&]{ resources.commitShader(pbrHandle); });
```

The typical use is shader hot-reload: when a `.shader` source file
changes, `commit` on the corresponding `ShaderAsset` bumps its version,
which drops its compiled program, and the next draw recompiles lazily.

It's polling-based, not platform-specific (no `inotify`/`fsnotify`), so
it works the same on every host. The poll cost is tiny because each
entry only `stat()`s its own file list, batched per tick.
