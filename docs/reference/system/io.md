# IO and Serialization

The engine persists scenes and prefabs to JSON, and resolves the assets they
name through a cooked asset database. The IO layer is three serializers that
compose - scene, component, asset - plus the library that maps an asset name to
the files holding it.

## Projects and the two roots

The engine runs *projects*: a directory becomes one by containing a
`project.json`. That file is what lets a game live nowhere near the engine's own
repo, and `Engine::Project` (`src/engine/io/project.h`) is everything it says:

| Field | Purpose |
|-------|---------|
| `name` | Display name; titles the window, names the packaged exe by default |
| `engineVersion` | Engine version the project was authored against; logged on load |
| `entryScene` | Scene to boot, relative to the project root |

A missing or malformed `project.json` is **not** fatal - the defaults stand and
an unnamed project opens, which is what a fresh directory should do.

`findProjectRoot(start)` accepts a directory *or any file inside it* and walks up
looking for `project.json`, so passing a scene finds the project owning it.

### Which root owns a path

`ProjectPaths` (`src/engine/io/project_paths.h`) splits on-disk locations by who
owns them, because two different things live on disk:

| Root | Owns | Helpers |
|------|------|---------|
| `engineRoot()` | What ships with the engine, read-only to a game. One copy serves every project | `engineShaders()`, `engineAssets()`, `engineFonts()` |
| `projectRoot()` | The game being made: its content, its code, its asset database | `assets()`, `scenes()`, `envs()`, `screenshots()`, `library()`, `cooked()`, `projectBin()` |

`engineRoot()` resolves once at first use. `projectRoot()` returns the override
when one is set and falls back to `engineRoot()` otherwise, so `setProjectRoot()`
takes effect immediately - but call it before anything composes a project path,
because a path already built from the old root is a plain string by then and will
not follow. The editor re-roots in that order when it opens a project (see
[editor.md](../editor.md#opening-a-project)).

Two consequences worth knowing before you add a path:

- **The working directory is the engine root**, in every host. Shaders load
  CWD-relative, so pinning CWD anywhere else breaks startup.
- **Engine chrome falls back engine-ward, project content does not.** A project
  shipping no UI font or window icon gets the engine's, because those are the
  engine's own furniture and every project needs them. Scenes, art and
  environment maps are the project's to author: inventing an engine copy for
  those would hand a project content it never asked for, so a missing one is
  reported and the scene goes without. The default Environment is a procedural
  sky precisely so a project needs no file at all.

## Components

| Layer               | File                                         | Purpose                                                                                  |
|---------------------|----------------------------------------------|------------------------------------------------------------------------------------------|
| Scene serializer    | `src/engine/io/scene/scene_serializer.h`           | Top-level save/load for a `Scene` + the assets it references. Transactional.             |
| Prefab              | `src/engine/io/scene/prefab.h`                     | One entity subtree in its own file, instanced by reference from a scene.                 |
| Asset serializer    | `src/engine/io/asset/asset_serializer.h`           | Save name-only asset references; on load resolve them via the asset library + the `AssetFactory` seam. |
| Asset library       | `src/engine/io/asset/asset_library.h`              | The cooked-asset database manifest: maps an asset name to its recipe + cooked file + hash. |
| Asset cooker        | `src/tools/cook/asset_cooker.h` (editor)     | Bakes assets from their recipe into the library + cooked binary cache (`cooked/`).        |
| Cooked format       | `src/engine/io/asset/asset_cook.h`                 | The `.vkmc` binary reader/writer. Readers are defensive: every count and size is validated before it is used. |
| Component serializer| `src/engine/io/scene/component_serializer.h`       | Per-component to/from JSON. Mechanical, one save/load pair per component type.           |
| Shader reload       | `modules/vkmGL/src/shader/gl_shader_reload.h`      | Live-shader registry + `reloadChangedShaders`; rebuilds a program whose source's `mtime` moved, keeping the old one on a compile error. |

Every JSON write in this layer - scene, prefab, manifest - goes through
`detail::writeJsonFile` (`src/engine/io/json_file.h`): the dump lands in a
sibling `.tmp` and is renamed over the target only once the stream reports a
clean write, so a full disk cannot leave a truncated file where a good one was.
`detail::readJsonFile` is the matching read.

## SceneSerializer

`SceneSerializer::save` emits a JSON object with four top-level blocks:

- `assets`: name-only references to every mesh / material a component names -
  `Mesh`, `LOD` levels, `Decal` - plus the textures those materials use. A
  component reference the assets block never lists is one the loader never
  recreates, so every component that names an asset has to be walked there. The
  asset *data* lives in the cooked library (keyed by name), not in the scene
  file, so the scene stays tiny and diff-friendly. Assets marked `hidden = true`
  are skipped (editor previews, fallback textures, bundled primitives).
- `entities`: one record per entity. Entities are stored at their slot
  index, and each component is keyed by its short name (see Component
  serializer below).
- `environment` and `physics`: the scene-global `Environment` and
  `PhysicsSettings`, each a single reflected object rather than a component.

`SceneSerializer::load` is **transactional for both entities and assets**:

1. Read the file (early-out on parse failure; live scene untouched).
2. Resolve each asset reference through the `AssetLibrary` manifest into a
   **staging** `ResourceManager` (not the live one): meshes/textures load from
   their cooked binary, materials from their library `inline` form. Idempotent:
   assets already present by `name` are skipped. Runs inside a guard so a
   malformed assets block logs and aborts the load with the live state intact.
3. Deserialise every entity into a **staging** `Scene` (not the live one),
   expand each prefab instance into it, wire the parent links, then read the
   `environment` and `physics` blocks. All of it sits inside one guard, so a
   drifted field anywhere - a string where a number belongs - fails the load
   instead of unwinding out of it.
4. On full success, swap both staging containers in one step:
   `Scene::swap` for the scene, and `ResourceManager::swap` for the assets. The
   font slot swaps *back* (`swapSlot<FontAsset>`): fonts are baked at startup
   and never enter a scene file, so the staging RM has none, and without that
   step every `UIText` loses its font on load.

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
  and recipe hash, under a `manifestVersion` the loader checks. `AssetLibrary`
  is the in-memory view, loaded at startup; a manifest in a version this build
  does not know is refused rather than half-read, because the whole library is
  derived data and re-cooking costs less than guessing.

**Save** - `AssetSerializer::saveAssetsForScene` walks the components that name
assets (`Mesh`, `LOD`, `Decal`) and emits **name-only** references to the
meshes/materials/textures used. In the editor,
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

Today's coverage, the flat list in `scene_serializer.cpp`:

- `Name`, `Transform`, `Camera`, `Light`, `Animation`
- `Mesh`, `LOD`, `Decal` - the ones that name assets, so their save/load also
  takes the `ResourceManager` that turns a handle into a name and back.
- `ParticleEmitter`, `IrradianceVolume`, `ReflectionProbe`
- `UICanvas`, `UIElement`, `UIImage`, `UIText`, `UIButton` (see [UI](ui.md))
- `Rigidbody`, `Collider` (physics; runtime sleep state and derived mass
  properties are not persisted - see [Physics](physics.md)).
- `ScriptComponent` (JSON key `"Script"`): each behavior stored by its registered
  type name and recreated through `BehaviorRegistry` on load (unknown types are
  dropped), with its authored fields in a `properties` object beside it -
  `Behavior::visitFields` walks them in both directions, and enums are written by
  name so reordering one does not invalidate saved scenes. See
  [Scripting](scripting.md).
- `Hierarchy` (only `parent` is serialized; sibling pointers are
  rebuilt on load by re-running `HierarchyOperations::setParent`).

`Environment` (sky / night / fog groups) and `PhysicsSettings` are scene-global
rather than per-entity, so they are written as their own top-level objects.
Both are fully reflected: the field list lives once in `ecs/environment.h` and
both directions walk it. Render tuning (GTAO / bloom / MSAA / ...) lives in
`RenderSettings` on the RenderSystem, not in a serialized component.

`Mesh` references handles by `name` rather than by `Storage` index;
that is what makes assets a stable identity across save/load.
`Hierarchy::parent` stores the old-file entity slot index, which the
loader uses directly because entities are recreated at the same slot.

### Adding a component to the round trip

Three localised edits, no registry table, no virtual dispatch, no macros:

1. A `save` / `load` overload pair in `component_serializer.h` (+ `.cpp`). If the
   component has nothing but plain reflected fields, both bodies are one call to
   `saveReflected` / `loadReflected`.
2. A line in `saveComponents` and a matching `loadInto<T>` line in
   `loadComponents`, both in `scene_serializer.cpp`, in the same order.
3. The JSON key added to `COMPONENT_KEYS` in the same file - the membership test
   behind the "unknown component key" warning that catches schema drift.

`loadInto` overwrites a component the entity already carries instead of adding a
second one. That is not a nicety: a prefab instance root is loaded twice, once
from the scene block that placed it and once from the prefab file.

## Prefabs

A prefab is a scene fragment - one entity and its descendants, with the same
per-entity component shape a scene uses, in its own file:

```json
{"version": 1, "entities": [{"components": {...}}, {"parent": 0, "components": {...}}]}
```

Parents precede children and a child names its parent by *index into this
file's own array*, because entity ids mean nothing outside the scene that issued
them. `Prefab::save` drops the `Hierarchy` block for that reason and rewrites
the link as an index.

A scene stores an instance as a `PrefabInstance` (the source path) plus the
root's `Transform` and `Hierarchy` - where it sits and what it hangs off belong
to the scene - and the saver skips the whole subtree beneath it. The loader
expands it after the entity pass, so the roots keep their saved slots and the
prefab's own entities take whatever is free. Editing the prefab therefore
changes every instance the next time a scene loads, which is the point.

Only the root `Transform` varies per instance; per-field overrides are
deliberately not designed yet (see the header for why).

## Shader hot reload

Shaders are not assets and not part of the library - they are source files read
CWD-relative from `shaders/`, so hot reload is a matter of noticing that one
changed. `Core::reloadChangedShaders` (`modules/vkmGL/src/shader/gl_shader_reload.h`)
takes the newest write time under a directory and, when it moves, recompiles
every live shader; a program that no longer compiles keeps its previous one and
logs the error.

The editor drives it, polling once a second (`EditorSystem::SHADER_POLL_INTERVAL`)
and toasting what it reloaded. There is no file-watcher system: a per-platform
watcher is a dependency for something a directory scan of a few dozen files
already answers, and the runtime has no shader sources to watch anyway.
