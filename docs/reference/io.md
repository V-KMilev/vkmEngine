# IO and Serialization

How a project, its scenes and its assets live on disk. A project is a directory
holding a `project.json`; scenes and prefabs are JSON files that name assets by
name; the asset library maps each name to its recipe and to a cooked binary
cache. This page says what each file holds, which host reads what, and what a
load or a save promises.

## Projects and their roots

A directory is a project when it holds a `project.json`. `Project`
(`src/engine/io/project.h`) is everything it says, one commented field per key.
What the fields do not say on their own:

- `name` is `project.json`'s, else the name of the project's folder: one rule,
  which `loadProject` applies for the engine and `project_name`
  (`tools/vkmcli/project.py`) for vkm.
- `tickRate` is the project's, not the engine's, because a networked game's wire
  is clocked by it.
- `maxPlayers` of zero is kept and means it: the session refuses to host.
- `render` is what the game looks like - pass toggles and their parameters,
  through `visitShippedRenderFields`. Absent fields keep the engine's defaults.
  A player's quality tier is not stored here: a settings screen applies one
  over it in code (`withPreset(projectRender(), tier)`,
  [rendering.md](rendering.md#rendersettings-and-rendermode)) and keeps the
  choice in its own file ([below](#a-projects-own-files)).
- `splash` (logos the runtime shows after the engine's) and `fonts` (a
  `UIText` names one by its `name`) are usually empty.

A missing or malformed `project.json` is **not** fatal: the defaults stand and the
project opens under its folder's name. A `render` setting of the wrong type keeps
its value and warns.

`saveProject` overwrites the fields the struct describes and leaves every other
top-level key as it was - the file is hand-authored as often as tool-written.
The `render` block is the exception: it is rebuilt from
`visitShippedRenderFields`, every shipped field and nothing else. `saveProject`
never writes `splash` or `fonts`: `loadProject` drops an entry naming nothing,
so writing a list back would delete it. The editor's Project Settings window
stamps `engineVersion` with the running engine before it saves.

`findProjectRoot(start)` takes a directory *or any file inside it* and walks up
to the `project.json`.

### Which root owns a path

`ProjectPaths` (`src/engine/io/project_paths.h`) splits on-disk locations by who
owns them:

| Root | Owns | Helpers |
|------|------|---------|
| `engineRoot()` | What ships with the engine, read-only to a game. One copy serves every project | `engineShaders()`, `engineAssets()`, `engineFonts()` |
| `projectRoot()` | The game being made: its content, its code, its asset database | `assets()`, `scenes()`, `prefabs()`, `envs()`, `screenshots()`, `library()`, `cooked()`, `projectBin()` |
| `userRoot()` | How one person likes their tools, across every project and every engine install | `userLogs()`, a sibling under the platform's *state* directory rather than a child of this root |
| `playerRoot(game)` | What a game writes for its player - settings, a profile, saves - in a folder of the game's own name beside the engine's own user folder | |

`projectRoot()` falls back to `engineRoot()` when no override is set. Call
`setProjectRoot()` before anything composes a project path: a path already built
from the old root is a plain string and will not follow (see
[editor.md](editor.md#opening-a-project)).

The engine and the project may both be read-only installs, so what the engine
writes that is not project content goes under `userRoot()`: `editor_user.json`
(recent projects and every Preferences field) and `imgui.ini` (the editor's dock
layout). Logs go to the project's `logs/`, and to `userLogs()` only when the
project cannot hold one. The platform picks the directory (XDG on Linux,
`%APPDATA%` on Windows); `project_paths.cpp` has the rules and fallbacks.

`editor_settings.json` stays in the project root: the editor's per-project state
([editor.md](editor.md)), including the two render fields no player should be
handed - `renderMode` (the debug buffer on screen) and the grid. Everything else
about how the game looks is `project.json`'s `render` block.

Before you add a path:

- **The working directory is the engine root**, in every host. Shaders load
  CWD-relative, so pinning CWD anywhere else breaks startup.
- **Engine chrome falls back engine-ward, project content does not.** A project
  shipping no UI font or window icon gets the engine's. A missing scene, piece
  of art or environment map is reported and the scene goes without.

### Opening a project's world

Every host but `vkm_cook` opens a project through `bootProjectWorld`
(`src/tools/project_boot.h`): the authored `entryScene`, else the world the
project's module builds through `vkmBuildScene`, else the engine's default
scene. **Exactly one** of them runs - seeding a scene first would leave a stray
camera, light and cube under whatever the project builds. A scene is always
standing afterwards; the result says whose:

| `SceneBoot` | Meaning |
|-------------|---------|
| `Project` | The project's own world opened - its entry scene, or one its module built |
| `Default` | The project names no world of its own; the default scene stands in |
| `Failed` | The project names an entry scene that did not load; the default scene stands in |

Only the authored entry scene comes back with a path (`SceneBootResult`), so a
stand-in can never be saved over the file it replaced. The editor adopts that
path (`SceneIOController::adoptPath`), so Save writes back to the file it opened.

### What each host does when a project will not open

The runtime plays a finished game; the editor is the tool you repair one with.

| Condition | `vkm_runtime` | `vkm_editor` | `vkm_cook` |
|-----------|---------------|--------------|------------|
| Log file cannot be opened | exit 1 | exit 1 | exit 1 |
| Window / graphics surface cannot be created | exit 1 (throws) | exit 1 (throws) | n/a - headless |
| No gameplay module in the project's `bin/` | exit 1 | opens, logs WARNING | n/a |
| Module present but will not load (version, entry, unreadable) | exit 1 | opens, logs ERROR | n/a |
| Entry scene named but will not load (`SceneBoot::Failed`) | exit 1 | opens on the default scene; error toast and an Errors panel entry | exit 1 |
| No entry scene and no `vkmBuildScene` (`SceneBoot::Default`) | exit 1 | opens on the default scene | cooks every scene in `scenes/`; exit 0 if there is none |
| No entry scene, module builds the world (`SceneBoot::Project`) | plays it | opens it | cooks every scene in `scenes/`; exit 0 if there is none |
| An asset fails to cook | n/a | reported, the session continues | exit 1 |
| Entry scene loads, but something in it does not (unresolved reference, prefab file missing) | plays it with what loaded | reported, the session continues | exit 1 |

`vkm_server` answers the runtime's column, and also exits 1 when the module
exports no `vkmSetupNetwork`: it has not said what a player is.

With no module, `ComponentSerializer` keeps each behavior as an
`UnknownBehavior` - its type name and properties, written back out unread - so
a save while the module is missing loses nothing.

`vkm_cook` does not use `bootProjectWorld`. It re-bakes every stale asset
(`AssetCooker::cookStaleAssets`), then loads every `.json` in `scenes/`, plus
`entryScene` when that file lives elsewhere. `SceneSerializer::load` returns true
even when references went unresolved, so the cooker installs an `EngineErrorLog`
sink around each load and fails on what `reportError` puts in it. `vkm package`
stops on a non-zero cook.

## A project's own files

A game's settings, a player's profile, a save: a struct the project reflects
(`VKM_REFLECT`, as a component is) goes to JSON and back by its field names with
`ReflectJson::toJson(obj)` and `ReflectJson::fromJson(json, obj)`
(`src/engine/io/reflect_json.h`), and to disk under `playerRoot(game)` with
`writeOwnFile` / `readOwnFile` (`src/engine/io/json_file.h`). The write is whole
or not at all; the read gives an empty object when there is no file yet or it
will not parse (which warns). Leaves, scoped enums by their `VKM_ENUM_NAMES`,
nested reflected structs and vectors of them all go through. A field the file
lacks keeps its value, so an older file still loads; a field holding the wrong
type warns and keeps its own - unlike a scene file, where that fails the load.

## Scene files

`SceneSerializer::save` writes a `version` and four top-level blocks:

- `version`: the layout's (`FILE_FORMAT_VERSION`, `scene_serializer.cpp`). A
  file with none, or one newer than the build, does not load.
- `assets`: name-only references to every asset the scene names - through its
  components and every `AssetRef` field on a behavior - plus the textures those
  materials use. The asset *data* lives in the library, not here, and a name
  this block does not list is one the loader never recreates. Assets marked
  `hidden = true` are skipped (editor previews, fallback textures, bundled
  primitives).
- `entities`: one record per entity, its `id` the entity's slot, each component
  keyed by its short name ([ComponentSerializer](#componentserializer)).
- `environment` and `physics`: the scene's `Environment` and `PhysicsSettings`,
  each one reflected object.

What a load and a save promise:

- **A failed load changes nothing.** A malformed or partial file leaves the live
  `Scene` and the live `ResourceManager` untouched ([how](#loading-a-scene)).
  After a successful load the caller clears the `CommandStack`, since entity ids
  are not comparable across it; `SceneIOController` does this in the editor.
- **A drifted field fails the load and names where.** A string where a number
  belongs aborts the load with an error naming the entity, the component and
  the type it expected. A behavior's authored field is the exception: it keeps
  its value and warns.
- **A missing asset or prefab costs only itself.** An asset name that did not
  resolve leaves the component's slot empty and goes through `reportError`; a
  prefab that will not open leaves the instance holding its reference and
  overrides, so restoring the file brings the subtree back.
- **Every number written is finite.** JSON cannot spell an infinity or a NaN, so
  `detail::writeNonFiniteAsZero` writes one as `0` and logs its path. The read
  stays strict: a `null` in a scalar field is a type error.
- **A write never truncates.** Every JSON write - scene, prefab, manifest - goes
  through `detail::writeJsonFile`, which writes a sibling `.tmp` and renames it
  over the target only after a clean write.

The play-mode snapshot (`saveToString` / `loadFromString`) is the same builder
and the same load, so a scene that could be saved can be restored on Stop. It
merges into the asset graph instead of swapping it, so every handle stays valid
and the editor's undo history survives Stop - see
[the editor](editor.md#a-play-session-owns-the-scene).

### A reference that did not resolve is kept

An empty slot looks like one nobody filled, so a save that went by the slot
alone would write `""` over every name a load could not resolve - opening a
scene with its `library/` missing and pressing Ctrl+S would lose them all.
Instead `resolveAssetRef` records each miss, `SceneSerializer` attaches the
record to the entity as a `MissingAssets` component (never written as a
component), and the save puts each name back in the field it came from - when
that field is still empty - and in the assets block. The record is never
trimmed, because an undo can empty a filled field again;
`SceneSerializer::unresolvedRefs` returns the entries whose field is still
empty, which is what the Inspector's banner and the assets block read.

Only a name stored under a component's own key is kept. A name inside a list -
an LOD level's mesh, a collider part's mesh - has no key to go back under: its
miss is reported, and a save writes it as `""`.

## Prefabs

A prefab is one entity and its descendants in a file of its own, with the same
per-entity component shape a scene uses:

```json
{"version": PREFAB_FORMAT_VERSION, "nextUid": 3,
 "entities": [{"uid": 0, "components": {...}}, {"uid": 1, "parent": 0, "components": {...}}],
 "assets": {"textures": [...], "meshes": [...], "materials": [...],
            "skeletons": [...], "clips": [...], "sounds": [...]}}
```

- The `assets` block is a scene's, for this subtree; it is what makes a prefab
  instantiable anywhere. Every path that reads components out of a prefab loads
  it first, `Prefab::reloadComponent` included.
- Parents precede children, and a child names its parent by index into this
  file's array.
- A scene stores an instance as a `PrefabInstance` (the source path) plus the
  root's `Transform` and `Hierarchy`, and skips the subtree. The loader expands
  it after the entity pass, so editing the prefab changes every instance the
  next time a scene loads. The roots keep their saved slots and the prefab's
  entities take whatever is free, unless the caller hands `loadFromString` the
  slots `Prefab::instanceSlotsOf` read off the world it was written from - which
  is how the editor's Stop puts every slot back.
- `Prefab::save` turns the subtree it wrote into an instance of the file; saving
  an instance over its own source is how a prefab is edited, and bakes its
  overrides in. Nesting is refused both ways - a subtree containing an
  instance, and a root inside one.
- `readPrefab` checks the file's shape once and every entry point reports what
  it cannot use rather than throwing. A duplicate uid is refused, since it makes
  an override's address ambiguous.

### Per-instance overrides

What varies per instance is the root's `Transform` and any number of
*overrides*: one field of one component of one entity in the subtree, stored on
the root's `PrefabInstance` as `uid -> component -> field` and the field's
serialized JSON as text. They merge into the document before `loadComponents`.

The entity half of the address is the file's per-entity `uid`, handed out once
from `nextUid` and never reused, carried at runtime by `PrefabEntity` and never
written into a scene.

Overrides are stored, never re-derived by diffing an instance against its file:
`load(save(x))` is not `x` (an unresolvable asset name comes back `""`), so a
diff would turn load failures into overrides. An override whose uid, component,
field or type is gone - or that addresses the root's `Transform` - is kept,
reported once, and not applied. `Script` cannot be overridden: it serializes as
one field holding the whole behavior list (see
[scripting.md](scripting.md#authored-fields-inside-a-prefab-instance)).

The Inspector records overrides as you edit a component inside an instance
(`src/editor/command/prefab_overrides.h`); `Prefab::reloadComponent` gives one
back to the prefab.

## Assets: recipes and the cooked cache

An asset's **recipe** - the `source` descriptor its import or generator wrote,
with a `kind` - is the editable source of truth. Its **cooked** file is a binary
cache derived from it. Each asset is its own file:

- `library/<type>/<uid>.json` - the recipe; for a material, its runtime form.
  Version-controlled.
- `cooked/<type>/<uid>-<key>.vkmc` - the cooked body. Regenerable; git-ignored.
  Deleting `cooked/` is always safe.
- `cooked/_manifest.json` - one row per asset: name, type, recipe hash and the
  source files that hash read. `AssetLibrary` is the in-memory view.

Neither filename is recorded: `AssetLibrary::recipePath()` and `cookedPath()`
derive them from (type, name), the uid being a hash of `"<type>:<name>"`.

An imported asset is **named by its project-relative path**, and so is the
`path` in its recipe (`ProjectPaths::toProjectRelative`, and
`resolveProjectPath` to open it), so the library does not depend on one
machine's home directory; a source outside the project keeps its absolute path.
A model file's parts are named by the file and the part
([resources.md](resources.md#importing-a-model)).

| `kind`                 | Read by          | Resolves to                                         |
|------------------------|------------------|-----------------------------------------------------|
| `inline`               | every host       | A `MaterialAsset` from PBR scalars + texture refs - every material's recipe, whatever it was imported from |
| `generator` / `decimate` | editor, cooker | Procedural / LOD meshes                             |
| `file` / `model` / `model-image` | editor, cooker | stb texture import and sound decode; cgltf / ufbx mesh, rig and clip import (a clip recipe also carries its authored `markers`) |
| `solid`                | editor, cooker   | Single-colour textures, the engine's defaults among them |

A `decimate` recipe is an LOD level kept as the instruction that makes it,
`{"kind": "decimate", "base": "<mesh>", "ratio": 0.5}`; `decimateRecipe` and
`decimateRatioFromRecipe` (`src/tools/cook/lod_generator.h`) are its one writer
and one reader.

**Who reads what.** A load reads the cooked file when a current one exists and
falls back to the recipe otherwise - which only the editor and `vkm_cook` can
import; the runtime and server link no importer and log a missing cooked file.
A manifest row with no recipe on disk is dropped by the editor and the cooker
but trusted by the runtime and server (`AssetLibrary::Truth`), because
`vkm package` ships `cooked/`, the manifest and `library/materials/` - no other
recipe, and no file a row lists under `sources`.

**When cooking happens.** Save and Play in the editor record every non-hidden
asset (recipe and manifest row) before they return, which is what makes a saved
name resolve; the slow bake of a texture or mesh goes to the `ThreadPool`, and
loads use the recipe until it lands. `vkm_cook` re-bakes everything stale,
bakes before returning, and ends by deleting every cooked file no row names
(`AssetLibrary::removeUnrecordedCooked`), so what `vkm package` copies is what
the game reads. An engine update that changes the cooked format costs a re-cook,
never a re-import.

## Shader hot reload

Shaders are not assets - they are source files read CWD-relative from
`shaders/`. `Vkm::GL::reloadChangedShaders`
(`modules/vkmGL/src/shader/gl_shader_reload.h`) takes the newest write time
under a directory and, when it moves, recompiles every live shader; a program
that no longer compiles keeps its previous one and logs the error. The editor
polls it once a second (`EditorSystem::DISK_POLL_INTERVAL`, which also paces
`pollScriptRebuild`); the runtime never polls.

## How it works inside

### Loading a scene

`SceneSerializer::load` stages both halves of the world:

1. Read the file; a parse failure returns with nothing touched.
2. Resolve each asset reference through the `AssetLibrary` into a **staging**
   `ResourceManager`, skipping names already present. A recipe that does not
   read costs that asset alone, through `reportError`.
3. Deserialise every entity into a **staging** `Scene` at its saved slot
   (`Scene::createEntityAt`), expand each prefab instance, wire the parent
   links, then read `environment` and `physics`.
4. On success, swap both in one step: `Scene::swap` and
   `ResourceManager::swap`. The font slot stays where it is - fonts are baked
   by the host and never enter a scene file.

### Loading an asset

`AssetSerializer::loadAssets` reads a document's sections in
`ASSET_DEPENDENCY_ORDER` (`resource/asset_type.h`) - textures before the
materials that name them, rigs before the clips and meshes bound to them - and
skips a name the graph already holds. Every kind but a material goes to the
cooked loader (`io/asset/cooked_loader.h`), which finds the manifest row,
probes the file with `AssetCook::isCookedCurrent` and reads it - a mesh or
texture on the `ThreadPool`, the rest at once.

On a miss the recipe is imported through the `AssetFactory` seam
(`io/asset/asset_factory.h`), one import per kind, which the editor and
`vkm_cook` install (`registerRecipeAssetFactories`) and the runtime and server
leave null. A material has no cooked file: `AssetSerializer::applyInline`
applies its recipe. A name with no manifest row still loads from a recipe on
disk, so a lost manifest costs a re-cook.

`finalizeAsyncLoads` / `awaitAsyncLoads` (`system/async/async_loader_system.h`)
land async decodes without a frame - drain once, and drain until quiet. The
second lands only completions meant for the graph it was given, because a scene
load waits on its staging graph while the live graph's imports are in flight.

Adding an asset kind is a row in `VKM_ASSET_KINDS`, then the hand work the
comment above that list names.

### Cooking and the cache key

`AssetCooker::cookAllAssets` (the editor's Save and Play, and `vkm_cook`) waits
for imports in flight, records every non-hidden asset and bakes it unless its
hash is unchanged and its file is current or baking; it returns false when any
asset failed. An asset with a recipe whose source did not load, leaving nothing
to bake, is a failure. One with no recipe is skipped, since a project may build
assets in code, and warns when the library has no row for it either: a scene
naming it will not load.

A row's recipe hash is FNV-1a (`core/fnv1a.h`) over the recipe bytes folded with
the bytes of every source file the import reads
(`AssetCooker::foldSourceContent`), since a re-export leaves the recipe
unchanged. A derived asset (an LOD level, a clip bound to another file's rig)
folds its dependency's key in (`AssetCooker::foldDependency`), which is why the
cooker runs in dependency order. An asset served from the cache carries a
`cooked` stand-in, not its recipe, so only `vkm_cook`'s
`AssetCooker::cookStaleAssets` notices re-exported art behind it.

The cooked name's key mixes the recipe hash, the kind and `COOKER_VERSION`
(`AssetCook::cacheKey`, `io/asset/asset_cook.h`), so a stale file is simply never
asked for. One probe, `AssetCook::isCookedCurrent`, serves the loader and the
cooker's skip, so they cannot disagree. **Bump `COOKER_VERSION` whenever
anything the cooker writes changes** - a layout, an importer flag, a mip
policy; `testTheCookersOutputIsPinnedToItsVersion` fails when the bytes move
under an unchanged version.

The `.vkmc` header and each body's layout are in `asset_cook.cpp`, beside the
size constants. Its readers are defensive: every count is bounded against what
is left of the payload before it is used, and a skeleton, clip, sound, mesh or
texture that breaks its own rules is refused (`findSkeletonFault`,
`findClipFault`). A mesh is baked reordered for the GPU (`optimizeMeshForGpu`),
so the same mesh from its recipe and from its cooked file holds its triangles
in different orders.

### ComponentSerializer

Every component has a `save(const T&) -> json` and a `load(const json&, T&)`,
mechanical, one pair per type. A component that names **assets** also takes the
`ResourceManager`, which turns a handle into a name and back. A component that
names **entities** takes an `EntityNamer` to save and an `EntityResolver` to
load, because what an entity is called depends on what carries the reference: a
scene names it by slot, a prefab by its place in the file, an undo snapshot by
the slot it restores. The set is `VKM_SCENE_COMPONENTS`, at the top of
`component_serializer.h`.

A field that describes a *session* rather than the thing does not round-trip,
for example `AudioSource::playing` or `Rigidbody`'s sleep state; each
component's header marks which of its fields are such state.

- `ScriptComponent` (key `"Script"`) stores each behavior by its registered type
  name and recreates it through `BehaviorRegistry`; authored fields sit in a
  `properties` object walked by `Behavior::visitFields`, enums by name. See
  [Scripting](scripting.md).
- A `Collider` part writes its `shape` by name beside every shape's fields, so
  switching shape and back keeps what was authored. A mesh part names its mesh;
  its triangles are rebuilt on load (`syncMeshCollider`).
- `Hierarchy` writes only `parent`, the parent's slot - the `id` on its record;
  a prefab moves it onto the entity's record as a file index
  ([above](#prefabs)). The loader's second pass rebuilds the links through
  `HierarchyOperations::setParent` once every entity exists.
- `loadInto` overwrites a component the entity already has rather than adding a
  second, because a prefab instance root is loaded twice - from the scene and
  from the prefab file.

### Adding a component to the round trip

Two localised edits, no registry table, no virtual dispatch:

1. A `save` / `load` overload pair in `component_serializer.h` (+ `.cpp`). For
   plain reflected fields both bodies are one call to `saveReflected` /
   `loadReflected`. A component that references assets adds
   `emitAssetRefs(const T&, AssetRefs&)` beside them.
2. A row in `VKM_SCENE_COMPONENTS`: `P(Type, "Key")` for a component that refers
   to nothing outside itself, `R(Type, "Key")` when it names assets,
   `E(Type, "Key")` when it names entities. Saving, loading, the known-key set
   behind the "unknown component key" warning, and `collectAssetRefs` (the
   `assets` block and the Asset Browser's in-use check) all expand from it.

An `R` row with no `emitAssetRefs` fails to compile, naming the component. The
key is spelled out rather than derived from the type name because it is the
format.

## Key files

| Layer               | File                                         |
|---------------------|----------------------------------------------|
| Project file        | `src/engine/io/project.h`                    |
| Roots and paths     | `src/engine/io/project_paths.h`              |
| Opening a world     | `src/tools/project_boot.h`                   |
| JSON files          | `src/engine/io/json_file.h`, `src/engine/io/reflect_json.h` |
| Scene serializer    | `src/engine/io/scene/scene_serializer.h`     |
| Prefab              | `src/engine/io/scene/prefab.h`               |
| Component serializer| `src/engine/io/scene/component_serializer.h` |
| Asset serializer    | `src/engine/io/asset/asset_serializer.h`     |
| Asset library       | `src/engine/io/asset/asset_library.h`        |
| Cooked format       | `src/engine/io/asset/asset_cook.h`           |
| Asset cooker        | `src/tools/cook/asset_cooker.h`              |
| Shader reload       | `modules/vkmGL/src/shader/gl_shader_reload.h` |
