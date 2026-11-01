# IO and Serialization

How a project, its scenes and its assets live on disk. A project is a directory
holding a `project.json`; scenes and prefabs are JSON files that name assets by
name; the asset library maps each name to its recipe and to a cooked binary
cache. This page says what each file holds, which host reads what, and what a
load or a save promises. Handles, names and each asset kind's own rules are
[resources.md](resources.md)'s.

## Projects and their roots

A directory is a project when it holds a `project.json`. `Project`
(`src/engine/io/project/project.h`) is everything it says, one commented field per key.
What the fields do not say on their own:

- `name` is `project.json`'s, else the name of the project's folder: one rule,
  which `loadProject` applies for the engine and `project_name`
  (`tools/vkmcli/project.py`) for vkm.
- `render` is what the game looks like, every field `RenderSettings` reflects;
  an absent one keeps the engine's default. A player's choices are not stored
  here: a settings screen applies a tier or a setting over it in code
  ([rendering.md](rendering.md#rendersettings-and-rendermode)), and the runtime
  keeps them in the player's own `graphics.json` (`Graphics::keepChoicesIn`,
  [below](#a-projects-own-files)), read over the project's at the next start.

A missing or malformed `project.json` is **not** fatal: the defaults stand and the
project opens under its folder's name.

`saveProject` reads, modifies and writes, since the file is hand-authored as often
as tool-written; a field at its default is no key, its doc says which blocks it writes only when
they differ, and `saveProjectRender` writes the `render` block alone.

### Which root owns a path

`Roots` (`src/engine/io/project/roots.h`) splits on-disk locations by who
owns them; the test is "would you commit this?":

| Root | Owns |
|------|------|
| `engineRoot()` | What ships with the engine, read-only to a game; one copy serves every project |
| `projectRoot()` | The game being made: its content, its code, its asset library and cooked cache. It is `engineRoot()` until one is set, which is where a packaged game keeps its data |
| `userRoot()` | How one person likes their tools, across every project and every engine install: `editor_user.json` (recent projects and the Preferences) and the editor's dock layout, `imgui.ini` |
| `playerRoot(game)` | What a game writes for its player - settings, a profile, saves - in a folder of the game's own name beside the engine's own user folder |

Call `setProjectRoot()` before anything composes a project path: a path already
built from the old root is a plain string and will not follow (see
[editor.md](editor.md#opening-a-project)). Logs go to the project's `logs/`, and
to `Roots::userLogs()` only when the project cannot hold one, as an
installed game cannot. Two hosts on one project - two editors - keep a log each: the first
takes `log.log` and the next the first of `log-2.log`, `log-3.log`, ... that no live host
holds (`claimLog`, an OS file lock the process's end lets go), so each Console shows its own
run. A host started with `VKM_INSTANCE` set adds it to its log's name outright.

`editor_settings.json` stays in the project root: the editor's state for this
project - its workspace, recent scenes and viewpoints, and the debug view and grid
it has the frame show (`HostChrome::Display`). How the game looks is
`project.json`'s.

Before you add a path:

- **The working directory is the engine root**, in every host. Shaders load
  CWD-relative, so pinning CWD anywhere else breaks startup.
- **Engine chrome falls back engine-ward, project content does not.** A project
  shipping no UI font or window icon gets the engine's. A missing scene, piece
  of art or environment map is reported and the scene goes without.

### Opening a project's world

Every host but `vkm_cook` opens a project through `bootProjectWorld`
(`src/engine/core/project_boot.h`): the authored `entryScene`, else the engine's
default scene, so a scene always stands afterwards and the `SceneBoot` result
says whose. Only the authored entry scene comes back with a path, so a stand-in
can never be saved over the file it replaced; the editor adopts that path
(`SceneSession::adoptPath`), so Save writes back to the file it opened. A
project naming no entry scene starts in the first scene Save As writes
(`setEntrySceneIfNone`), so `vkm run` plays what was saved. The runtime and the
server open it through `bootPlayedWorld`, which refuses a world that is not the
project's own.

### What each host does when a project will not open

The runtime plays a finished game; the editor is the tool you repair one with.

| Condition | `vkm_runtime` | `vkm_editor` | `vkm_cook` |
|-----------|---------------|--------------|------------|
| Log file cannot be opened, in `logs/` or the user's folder | logs to the console only, and says so | logs to the console only, and says so | logs to the console only, and says so |
| Window / graphics surface cannot be created | exit 1 (throws) | exit 1 (throws) | n/a - headless |
| No gameplay module in the project's `bin/` | exit 1 | opens and waits for one (logs INFO); builds it once when the project has a `CMakeLists.txt` | n/a |
| Module present but will not load (version, entry, unreadable) | exit 1 | opens, logs ERROR and toasts it; behaviors are kept but do not run | n/a |
| Entry scene named but will not load (`SceneBoot::Failed`) | exit 1 | opens on the default scene; error toast and an entry in the Console's Errors | exit 1 |
| No entry scene (`SceneBoot::Default`) | exit 1 | opens on the default scene | cooks every scene in `scenes/`; exit 0 if there is none |
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
(`src/engine/io/json/reflect_json.h`), and to disk at `Roots::playerFile(name)`,
in the open game's `playerRoot`, with `writeOwnFile` / `readOwnFile`
(`src/engine/io/json/own_file.h`). The write is whole or not at all; the read gives an
empty object when there is no file yet or it will not parse. A field the file
lacks keeps its value, so an older file still loads; a field holding the wrong
type warns and keeps its own - unlike a scene file, where that fails the load.

## Scene files

`SceneSerializer::save` writes a `version` and four top-level blocks:

- `version`: the layout's. A file with none, or one newer than the build, does
  not load ([the gates](#what-each-formats-version-gate-does)).
- `assets`: name-only references to every asset the scene names - through its
  components and every `AssetRef` field on a behavior - plus the textures those
  materials use. The asset *data* lives in the library, not here, and a name
  this block does not list is one the loader never recreates.
- `entities`: one record per entity, its `id` the entity's slot, each component
  keyed by its short name ([ComponentSerializer](#componentserializer)).
- `environment` and `physics`: the scene's `Environment` and `PhysicsSettings`,
  each one reflected object.

What a load and a save promise:

- **A failed load changes nothing.** The load builds a staging `Scene` and a
  staging `ResourceManager` and swaps both in only when the whole file read, so a
  malformed or partial file leaves the live world untouched. After a successful
  load the caller clears the `CommandStack`, since entity ids are not comparable
  across it; `SceneSession` does this in the editor.
- **A drifted field fails the load and names where.** A string or `true` where
  a number belongs, or a vector short of numbers, aborts the load with an error
  naming the entity, the component and the type it expected. A behavior's
  authored field is the exception: read by the same leaf rule
  (`ReflectJson::leafFromJson`), it keeps its value and warns. An enum name this
  build lacks is a value removed or renamed, not drift: the field keeps its
  default and warns, in a component and a behavior alike.
- **A missing asset or prefab costs only itself.** An asset name that did not
  resolve leaves the component's slot empty and goes through `reportError`; a
  prefab that will not open leaves the instance holding its reference and
  overrides, and its entities standing empty in their slots, so what names one
  still does and restoring the file brings the subtree back. An `assets` entry
  that is no object, or names no string, is warned of and skipped.
- **A key this build does not read is said once.** A component key no row has, or a
  field its component's save does not write - renamed or removed since the file was
  saved - is dropped with one warning per key per file, in a scene and a prefab alike
  (`SceneSerializer::collectUnread`); the next save leaves it out. A behavior's field is
  known where a save of a default one of its type writes it; one held as text while its
  module is absent keeps every key.
- **Every number written is finite, and a float is written short**
  (`detail::tidyNumbers`), so a value saved unchanged writes the same digits; the
  read stays strict, and a `null` in a scalar field is a type error.
- **A write never truncates.** Every JSON write - scene, prefab, manifest, project -
  goes through `detail::writeJsonFile`, which renames a finished temporary of its
  own over the target (`WholeFileWrite`).

The play-mode snapshot (`saveToString` / `loadFromString`) is the same builder
and the same load, so a scene that could be saved can be restored on Stop - see
[the editor](editor.md#a-play-session-owns-the-scene) - but for its assets: they
merge into the live graph, added only where a name is absent, rather than being
swapped in, so every handle stays valid and the epoch does not move. The document
names entities by slot, as a file does; handed the source world's
`Scene::slotGenerations`, `loadFromString` starts each slot at its generation there,
so every entity comes back under the id it had.

### A reference that did not resolve is kept

An empty slot looks like one nobody filled, so a save that went by the slot
alone would write `""` over every name a load could not resolve - opening a
scene with its `library/` missing and pressing Ctrl+S would lose them all.
Instead the load keeps each miss in the list `ComponentSerializer::load` is
handed, `SceneSerializer` attaches it to the entity as a `MissingAssets`
component (never written as a component), and the save puts each name back in the field it came from - when
that field is still empty - and in the assets block. The record is never
trimmed, because an undo can empty a filled field again;
`SceneSerializer::unresolvedRefs` returns the entries whose field is still
empty, which is what the Inspector's banner and the assets block read.

A name inside a list - an LOD level's mesh, a collider part's mesh - is kept
under its element's path (`levels.2.mesh`, `ReflectJson::childPath`) and goes
back the same way while that element is still there.

## Prefabs

A prefab is one entity and its descendants in a file of its own, with the same
per-entity component shape a scene uses:

```json
{"version": PREFAB_FORMAT_VERSION, "nextUid": 3,
 "entities": [{"uid": 0, "components": {...}}, {"uid": 1, "parent": 1, "components": {...}}],
 "assets": {...}}
```

- The `assets` block is a scene's, for this subtree; it is what makes a prefab
  instantiable anywhere, so every path that reads components out of a prefab
  loads it first. Parents precede children. The file names an entity by its
  position in this array plus one, zero being none: a child's `parent` and every
  reference inside a component alike.
- A scene stores an instance as a `PrefabInstance`: its entity's record holds the
  source path under `"prefab"`, its [overrides](#per-instance-overrides) under
  `"overrides"` and the slot each entity of the subtree stood in under `"slots"`,
  by uid, beside the root's `Transform` and `Hierarchy`, and skips the subtree.
  The loader expands it after the entity pass and before any component is read,
  so editing the prefab changes every instance the next time a scene loads, each
  entity goes back to its slot, and a reference into an instance - a `Joint`
  tied to a body in it - finds its entity. An entity the file does not place -
  one the prefab gained since, or one whose slot is unusable or held, which the
  load warns of - goes above every slot the file places, so it takes none
  another is waiting for; where the prefab will not open, it is not made. The
  editor's Stop puts every slot back the same way.
- Saving an instance over its own source is how a prefab is edited, and bakes its
  overrides in. Nesting is refused both ways - a subtree containing an instance,
  and a root inside one - and so is a missing or duplicate uid, which would make
  an override's address ambiguous.

### Per-instance overrides

What varies per instance is the root's `Transform` and any number of
*overrides*: one field of one component of one entity in the subtree, stored on
the root's `PrefabInstance` as `uid -> component -> field` and the field's
serialized JSON as text, merged into the document before `loadComponents`. The
`uid` is the file's, handed out once from `nextUid` and never reused, carried at
runtime by `PrefabEntity` and never written into a scene.

Overrides are stored, never re-derived by diffing an instance against its file:
`load(save(x))` is not `x` (an unresolvable asset name comes back `""`), so a
diff would turn load failures into overrides. An override whose uid, component,
field or type is gone - or that addresses the root's `Transform` - is kept,
reported once, and not applied. `Script` cannot be overridden: it serializes as
one field holding the whole behavior list (see
[scripting.md](scripting.md#authored-fields-inside-a-prefab-instance)).

The Inspector records overrides as you edit a component inside an instance
(`src/editor/undo/prefab_overrides.h`); `Prefab::reloadComponent` gives one
back to the prefab.

## Assets: recipes and the cooked cache

An asset's **recipe** - the `source` descriptor its import or generator wrote -
is the editable source of truth. Its **cooked** file is a binary cache derived
from it. Each asset is its own file under a uid hashed from its type and name, so
no path is recorded: `AssetLibrary::recipePath()` and `cookedPath()` derive them.

- `library/<type>/<uid>.json` - the recipe; for a material, its runtime form.
  Version-controlled.
- `cooked/<type>/<uid>-<key>.vkmc` - the cooked body. Regenerable; git-ignored.
  Deleting `cooked/` is always safe.
- `cooked/_manifest.json` - one row per asset: name, type, recipe hash, the
  source files that hash read, and the recipe document's own hash.
  `AssetLibrary` is the in-memory view.

An imported asset is **named by its project-relative path**, and so is the
`path` in its recipe (`Roots::toProjectRelative`), so the library does not
depend on one machine's home directory; a source outside the project keeps its
absolute path. A model file's parts are named by the file and the part
([resources.md](resources.md#importing-a-model)).

A recipe's `kind` picks the import that rebuilds the asset - a generator's
primitive, an LOD level decimated from another mesh, a solid color, a file, one
part of a model or an image it carries. The kinds and the keys are the format,
listed once in `io/asset/recipe/asset_source.h`. A material's recipe has no `kind`:
it is the material's values and its maps by name, read by every host whatever it
was imported from, and it has no cooked file. Nor has a terrain: every host reads
its `.terrain` file, which its name names, as it is ([terrain.md](terrain.md#the-asset)).

**Who reads what.** A load reads the cooked file when a current one exists and
falls back to the recipe otherwise - which only the editor and `vkm_cook` can
import; the runtime and server link no importer and log a missing cooked file.
A manifest row with no recipe on disk is dropped by the editor and the cooker
but trusted by the runtime and server (`AssetLibrary::Truth`), because
`vkm package` ships `cooked/`, the manifest and `library/materials/` - no other
recipe, and no file a row lists under `sources` but a terrain's.

**When cooking happens.** Save and Play in the editor write each terrain's file
([terrain.md](terrain.md#the-asset)), then record every non-hidden asset (recipe
and manifest row) before they return, which is what makes a saved name resolve;
the slow bake of a texture or mesh goes to the `ThreadPool`, and loads use the
recipe until it lands. A cook fails on an asset whose recipe's source did not
load, leaving nothing to bake. It skips one with no recipe, since a project may
build assets in code, and warns when the library has no row for it either: a
scene naming it will not load.
`vkm_cook` re-bakes everything stale, bakes before returning, and ends by
deleting every cooked file no row names (`AssetLibrary::removeUnrecordedCooked`),
so what `vkm package` copies is what the game reads. An engine update that
changes the cooked format costs a re-cook, never a re-import.

## Shader hot reload

Shaders are not assets: they are source files read CWD-relative from `shaders/`,
never recorded or cooked. The editor polls them (`ReloadWatcher`), and when
anything under its backend's `shaders/opengl/` was written, `Vkm::OpenGL::reloadChangedShaders`
(`modules/vkmOpenGL/src/vkm_opengl/shader/shader_reload.h`) recompiles every live shader; a
program that no longer compiles keeps its previous one and logs the error. The same poll
reloads a grading strip saved over (`EditorRenderHooks::reloadChangedGrading`,
[rendering.md](rendering.md#color-grading)). The runtime never polls.

---

## How it works inside

### Loading an asset

`SceneAssets::loadAssets` reads a document's sections in
`ASSET_DEPENDENCY_ORDER` (`resource/asset_type.h`) - textures before the
materials that name them, rigs before the clips and meshes bound to them - and
skips a name the graph already holds. A terrain is read from its `.terrain` file
(`TerrainFile::load`); every other kind but a material goes to the
cooked loader (`io/asset/cooked/cooked_loader.h`), which reads a current cooked file - a
mesh or texture on the `ThreadPool`, the rest at once. On a miss, or where a
recipe no longer hashes to its row's `document` (edited by hand, or pulled), the
recipe is imported through the `AssetFactory` seam (`io/asset/recipe/asset_factory.h`),
which the editor and `vkm_cook` install (`registerRecipeAssetFactories`) and the
runtime and server leave null. A material is built from its recipe
(`materialFromRecipe`) after the maps it names, so a map the
document does not list still resolves. A name with no manifest row still loads
from a recipe on disk, so a lost manifest costs a re-cook.

Adding an asset kind is a row in `VKM_ASSET_ROWS` (`resource/asset_type.h`) -
its name, directory and cooked tag - then the per-kind code the comment above
that list names; the rest is made from the row.

### Cooking and the cache key

A row's recipe hash covers the recipe bytes, the bytes of every source file the
import reads - a re-export leaves the recipe unchanged - and the key of any asset
it derives from (an LOD level, a clip bound to another file's rig), which is why
the cooker (`AssetCooker::cookAllAssets`) runs in dependency order. An asset
served from the cache carries a `cooked` stand-in, not its recipe, so only
`AssetCooker::cookStaleAssets` notices re-exported art behind it.

The cooked file's name mixes the recipe hash with the kind and `COOKER_VERSION`
(`AssetCook::cacheKey`, `io/asset/cooked/asset_cook.h`), so a stale file is simply
never asked for. One probe, `AssetCook::isCookedCurrent`, serves the loader and
the cooker's skip, so they cannot disagree. It reads the header and size only; a
body the reader then refuses is reported as an error and, where the host can
import its recipe, removed for the next cook to bake again. **Bump
`COOKER_VERSION` whenever anything the cooker writes changes** - a layout, an
importer flag, a mip policy; the test "what the cooker writes is pinned to its version"
fails when the bytes move under an unchanged version. The bytes are the same on every
platform: a lightmap unwrap's trigonometry and its solver's one sort, and an sRGB mip's
power, are the engine's own (`src/pipeline/portable_math.h`,
`src/pipeline/mesh/xatlas_portable.h`), as two C libraries' `sinf`, `acosf` or `powf` differ
in their last bit and their `qsort`s order the solver's rows each its own way.

The `.vkmc` layouts are in `asset_cook.cpp`. Its writer and its reader hold each
kind to one set of rules, the kind's own, stated beside the asset
([resources.md](resources.md#asset-types)), so nothing cooks that will not read. A
mesh is baked reordered for the GPU (`optimizeMeshForGpu`).

### ComponentSerializer

A reflected component saves and loads its reflected fields through one template pair,
`save(const T&) -> json` and `load(const json&, T&)`, with no code of its own; the set is
`VKM_SCENE_COMPONENTS`, in `scene_components.h`. A component that names
assets also takes the `ResourceManager`, which turns a handle into a name and back. One
that names entities takes an `EntityNamer` to save and an `EntityResolver` to load,
because what an entity is called depends on what carries the reference: a scene names it
by slot, a prefab by its place in the file, an undo snapshot by the slot it restores.

A field that describes a *session* rather than the thing does not round-trip,
for example `AudioSource::playing` or `Rigidbody`'s sleep state; each
component's header marks which of its fields are such state.

- `ScriptComponent` (key `"Script"`) stores each behavior by its registered type
  name and recreates it through `BehaviorRegistry`; its authored fields sit in a
  `properties` object walked by `BehaviorBase::visitFields` - the walk the
  inspector and a hot reload use - enums by name, and a field the file lacks
  keeps the constructed default. See [Scripting](scripting.md).
- A `Collider` part writes every shape's fields beside its `shape`, so switching
  shape and back keeps what was authored; a mesh part's triangles are rebuilt on
  load (`syncMeshCollider`).
- `Hierarchy` writes only `parent`, the parent's slot, and the loader's second
  pass links it through `HierarchyOperations::setParent` once every entity exists.
  A prefab writes the parent on its entry instead ([Prefabs](#prefabs)).
- `loadInto` overwrites a component the entity already has rather than adding a
  second, because a prefab instance root is loaded twice - from the scene and
  from the prefab file.

### Adding a component to the round trip

Any plain struct is a component: `scene.add<T>(e, ...)`. **Surviving a save is a
separate step, and skipping it fails silently** - the component works all session and is
absent next load. Three edits, each where its kind lives, and no virtual dispatch:

1. Reflect it: `VKM_REFLECT_BEGIN(::Vkm::Engine::T)` / `VKM_F(field)` /
   `VKM_REFLECT_END()` at global scope
   ([architecture.md](architecture.md#conventions)). Reflecting only the fields that are
   authored is how a component says the rest is session state.
2. A row in `VKM_SCENE_VALUE_COMPONENTS`, which with the Script row is
   `VKM_SCENE_COMPONENTS`: `P(Type, "Key")` for a component that refers
   to nothing outside itself, `R(Type, "Key")` when it names assets,
   `E(Type, "Key")` when it names entities. Saving, loading, the known keys
   behind the "unknown component key" and "unknown field" warnings (a field is known
   where a save of the component writes it), and `collectAssetRefs` (the
   `assets` block and the editor's in-use check) all expand from it.
3. Its inspector card, which the row declares, so the build does not link without one: its
   `cardInfo` and `drawCard` in a `cards_*.cpp` under `src/editor/panels/inspector/`, a row a
   field ([editor.md](editor.md#a-card-is-rows)).

An `E` row writes its own `save` / `load` pair, since a reflected field cannot name an
entity, and so does a component that is not reflected. The key is spelled out rather than
derived from the type name because it is the format.

**Replicating is a further step and fails the same silent way**: `netEncode` / `netDecode`
beside it, registered from the project's `vkmSetupNetwork`
([networking.md](networking.md#what-replicates)). Replicate a cause only the authority
knows, not an effect every end recomputes - an effect on the wire is a second writer.

**Undo needs nothing**: its commands and the snapshot a delete is undone from take every row
of the list ([editor.md](editor.md#undo--redo)).

### What each format's version gate does

One rule, several mechanisms. Know which one you are holding before you bump a constant:

| Format         | Constant                                              | What the reader refuses                       |
|----------------|-------------------------------------------------------|-----------------------------------------------|
| Cooked asset   | `io/asset/cooked/asset_cook.h` `COOKER_VERSION` - one number for every kind | Anything but an exact match in the header. A bump never reaches that check: the version is part of each file's name ([above](#cooking-and-the-cache-key)), so a bump for one kind re-bakes every kind. |
| Scene          | `io/scene/scene_serializer.cpp`                       | Missing, zero, or *newer* than this build. Nothing else (`detail::readsFormatVersion`). |
| Prefab         | `io/scene/prefab.cpp`                                 | The same check.                               |
| `project.json` | `io/project/project.cpp` `engineVersion`                      | Nothing. It warns: it is provenance, not a format version, and refusing to open a project would be worse. |
| Asset manifest | `io/asset/asset_library.cpp` `MANIFEST_VERSION`       | Anything but an exact match, either direction. The library then starts empty: a host that imports recipes (the editor, `vkm_cook`) loads every name from its recipe - whose path needs no row - and the next cook records it again, while one that does not (the runtime, the server) loads no cooked asset until the project is cooked again. Derived data, refused whole rather than half-understood. |
| Library recipe | None - `library/<type>/<uid>.json` carries no version | Nothing past its shape: one with no `source`, or of a `kind` no factory dispatches, does not load. An unknown key is ignored, a material's with a warning; a missing one reads as its default, and a key of the wrong type costs that one asset, reported, never the scene. A cook rewrites a recipe in the current form only when it imports the asset again and finds its record out of date; one served from a current cooked file is left as it is. It is version-controlled source, so a change to a key's meaning alters authored data: say in the commit what an old recipe reads as. |
| `.terrain` file | `io/asset/terrain_file.h` `TerrainFile::VERSION`   | Anything but an exact match. It is authored source, not derived, and what every host reads: a bump leaves every project's terrains unreadable until a one-off pass re-writes them. |
| `bindings.json` | None - a player's rebindings in `Roots::playerRoot` (`InputMap::keepBindingsIn`) | Nothing past its shape: each action's bindings by name (`io/project/actions_json.h`). An entry that names no key or button - an older file's `{"source", "code", "scale"}` objects among them - is skipped with a warning, and an action left with none keeps its defaults, so the player binds it again. |
| `editor_settings.json`, `editor_user.json` | `src/editor/editor_settings.cpp` `FILE_VERSION`, `USER_FILE_VERSION` | Anything but an exact match: the file is ignored with a warning and the defaults stand (`readVersioned`); the editor writes the current form as it closes or leaves the project. |

For scenes and prefabs, **bumping the constant does not stop an old file loading.** The gate
refuses the future, not the past: an older file is read by the current reader and a removed field
comes back as its default. Say in the commit what an old file will do. If the answer must be
"refuse it", that changes what the gate does and belongs with the owner.

Round-trip any format change against what ships:
`examples/the_lab/scenes/obstacle_course.json` is the saved scene of record. It is ported with the
change, and must load and save back unchanged
([decisions.md](decisions.md#changing-a-shape-its-users-rely-on)).

## Key files

| Layer                        | File                                                                                          |
|------------------------------|-----------------------------------------------------------------------------------------------|
| Project file                 | `src/engine/io/project/project.h`                                                             |
| Roots and paths              | `src/engine/io/project/roots.h`                                                               |
| The host prologue            | `src/engine/io/project/host_boot.h`                                                           |
| Opening a world              | `src/engine/core/project_boot.h`                                                              |
| JSON files                   | `src/engine/io/json/json_file.h`, `src/engine/io/json/reflect_json.h`                         |
| A project's own files        | `src/engine/io/json/own_file.h`                                                               |
| Scene serializer             | `src/engine/io/scene/scene_serializer.h`                                                      |
| Prefab                       | `src/engine/io/scene/prefab.h`                                                                |
| Component serializer         | `src/engine/io/scene/component_serializer.h`                                                  |
| Scene components             | `src/engine/io/scene/scene_components.h`                                                      |
| Recipe kinds                 | `src/engine/io/asset/recipe/asset_source.h`                                                   |
| Scene assets                 | `src/engine/io/scene/scene_assets.h`                                                          |
| Asset references             | `src/engine/io/scene/asset_refs.h`                                                            |
| Material and texture recipes | `src/engine/io/asset/recipe/material_recipe.h`, `src/engine/io/asset/recipe/texture_recipe.h` |
| Asset library                | `src/engine/io/asset/asset_library.h`                                                         |
| Cooked format                | `src/engine/io/asset/cooked/asset_cook.h`                                                     |
| Asset cooker                 | `src/pipeline/cook/asset_cooker.h`                                                            |
| Shader reload                | `modules/vkmOpenGL/src/vkm_opengl/shader/shader_reload.h`                                     |
