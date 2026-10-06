# IO and Serialization

The engine persists scenes and prefabs to JSON, and resolves the assets they
name through a cooked asset database. The IO layer is three serializers that
compose - scene, component, asset - plus the library that maps an asset name to
the files holding it.

## Projects and the three roots

The engine runs *projects*: a directory becomes one by containing a
`project.json`. That file is what lets a game live nowhere near the engine's own
repo, and `Vkm::Engine::Project` (`src/engine/io/project.h`) is everything it says:

| Field | Purpose |
|-------|---------|
| `name` | Display name; titles the window, names the packaged exe by default |
| `version` | The game's own version: `vkm package` names the package by it and stamps it into a Windows executable's properties |
| `engineVersion` | Engine version the project was made for; logged on load, and the one record a module's build checks (`vkm_check_engine_version`) |
| `entryScene` | Scene to boot, relative to the project root |
| `tickRate` | Simulation ticks per second; 64 by default, clamped to a sane range |
| `maxPlayers` | Seats the game has, clamped to 64. A property of the game, not of a run: a scene with four characters authored into it is a four-player game wherever it is served. Zero is kept and means it - the session refuses to host rather than opening a seat the author closed |
| `netPort` | Port the game is served on unless a run says otherwise, so serving a project and joining it need no argument to agree |
| `splash` | Logos the **runtime** shows after the engine's own, in order. Usually empty; the editor shows only the engine's mark, because the editor is not the game |
| `render` | What the game looks like: the pass toggles and their parameters, written and read through `visitShippedRenderFields`. Absent fields keep the engine's defaults, so a file without the block opens as the engine's own look. `renderMode` and `grid` are not in it - see [Which root owns a path](#which-root-owns-a-path) |

`tickRate` is the project's rather than the engine's because it is not only a
simulation detail: for a networked game it is the rate the wire is clocked by,
and a competitive shooter and a turn-based game want different answers out of
the same build. Each host applies it where it learns its project - the runtime
once before the loop, the editor on every project open, since two projects
opened in one session are two cadences.

A missing or malformed `project.json` is **not** fatal - the defaults stand and
an unnamed project opens, which is what a fresh directory should do. A `render`
setting of the wrong type is narrower still: it keeps its value and warns, and
the rest of the file reads.

`saveProject` writes one back, for the editor's Project Settings window; New
Project stamps `name` and `engineVersion` into its template's copy directly.
`saveProject` is a read-modify-write: the file is parsed, the fields the struct
describes are overwritten, and every other key is left exactly as it was, because
a `project.json` is hand-authored as often as it is written by a tool and a
writer that rebuilt the document would silently drop what it did not know about.
`splash` is the one field read but not written - `loadProject` skips an entry
naming no image, so writing the list back would delete it from the file.

The Project Settings window stamps `engineVersion` with the running engine
before it saves. That field is provenance
rather than a format version - nothing refuses a file over it, it only warns -
and what it records after a save is the engine that last wrote the file.

`findProjectRoot(start)` accepts a directory *or any file inside it* and walks up
looking for `project.json`, so passing a scene finds the project owning it.

### Which root owns a path

`ProjectPaths` (`src/engine/io/project_paths.h`) splits on-disk locations by who
owns them, because three different things live on disk:

| Root | Owns | Helpers |
|------|------|---------|
| `engineRoot()` | What ships with the engine, read-only to a game. One copy serves every project | `engineShaders()`, `engineAssets()`, `engineFonts()` |
| `projectRoot()` | The game being made: its content, its code, its asset database | `assets()`, `scenes()`, `prefabs()`, `envs()`, `screenshots()`, `library()`, `cooked()`, `projectBin()` |
| `userRoot()` | How one person likes their tools, across every project and every engine install | `userLogs()`, a sibling under the platform's *state* directory rather than a child of this root |

`engineRoot()` resolves once at first use. `projectRoot()` returns the override
when one is set and falls back to `engineRoot()` otherwise, so `setProjectRoot()`
takes effect immediately - but call it before anything composes a project path,
because a path already built from the old root is a plain string by then and will
not follow. The editor re-roots in that order when it opens a project (see
[editor.md](editor.md#opening-a-project)).

`userRoot()` exists because the first two can both be read-only. An SDK
installed to `/usr/local` or `Program Files` is; so is a game installed there.
Anything the engine *writes* that is not a project's own content therefore goes
here:

| File | Where | Why not the project |
|------|-------|---------------------|
| `editor_user.json` | `userRoot()` | The settings that follow a person rather than a project: the projects you have opened, which is how you get from one to the next and which inside a project could only ever list itself, and every Preferences field - keybinds, snapping, the fly camera's feel, the UI scale, vsync, the frame cap and the window mode - which are facts about you and the monitor in front of you |
| `imgui.ini` | `userRoot()` | The editor's docking layout - where each panel is docked or floats, how big it is - and table column widths are one person's arrangement, not something a project hands the next person ([editor.md](editor.md#the-panels-dock-and-the-layout-is-imguis)) |
| `logs/<project>/log.log` (`cook.log` for `vkm_cook`) | `userLogs()`, **only** when the project cannot hold a `logs/` | A developer looks beside the project, so that stays the first choice |

The platform decides the actual directory, and configuration and state are
different places on both: `$XDG_CONFIG_HOME` (or `~/.config`) and
`$XDG_STATE_HOME` (or `~/.local/state`) on Linux, `%APPDATA%` and
`%LOCALAPPDATA%` on Windows, each under a `vkmEngine` folder. With no home
directory at all - a service account, a stripped container - both fall back to
the engine root (`userLogs()` to its `logs/`), and `userRoot()` does the same
when it cannot create its folder inside the one it names. That fallback is why
the repository ignores `imgui.ini` and `editor_user.json` at its root.
`userRoot()` creates its directory when first asked for; `userLogs()` does not,
because `bootHost` creates the per-project subdirectory it writes into.

`editor_settings.json` is the deliberate exception: it stays in the project root,
because what it holds (which panels are shown, the active tool,
recent scenes, which debug buffer the viewport is showing) is per-project. A project you
are authoring is writable by definition.

What the *game* looks like is not in it. `project.json` carries a `render` block
- the pass toggles and their parameters - because it answers yes to this
section's own question, "would you commit this?": an author who turns bloom off
has decided something about the game, not about their machine. Kept in
`editor_settings.json`, which git ignores and no host but the editor reads, it
would leave a shipped game rendering with the in-class defaults however the
project had been tuned. The two render fields `editor_settings.json` does keep are `renderMode` and `grid`: a debug
buffer and editor chrome, which no player should ever be handed.

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

### Opening a project's world

Every host opens a project by one rule, in `bootProjectWorld`
(`src/tools/project_boot.h`), because they have to agree on it: the authored
`entryScene`, else the world the project's module builds through `vkmBuildScene`,
else the engine's default scene. **Exactly one** of them runs - seeding a scene
first would leave a stray camera, light and cube under whatever the project then
builds.

A scene is always standing afterwards, so the return value says *whose* it is
rather than whether there is one:

| `SceneBoot` | Meaning |
|-------------|---------|
| `Project` | The project's own world opened - its entry scene, or one its module built |
| `Default` | The project names no world of its own; the default scene stands in |
| `Failed` | The project names an entry scene that did not load; the default scene stands in |

`SceneBootResult` carries the path beside it, and only one of the three worlds
has one: the authored entry scene. A module-built world and the default scene
standing in for a load that failed are both worlds with no file behind them,
which is what stops a stand-in being saved over the file it replaced.

The editor adopts that path as the scene it is editing
(`SceneIOController::adoptPath`, from both its startup and File > Open Project),
because the world it opens on is the one scene it never read itself. Unadopted,
a file the editor had just loaded would be a scene with no file: Save would ask
for a name, offer `scene.json` rather than the name it has, and writing it would
leave the project's `entryScene` untouched and the session saved where the
project never looks - with the title bar calling it *untitled* the whole time. The path is
stated by the function that opened the file rather than re-derived by each
caller, since re-deriving it means restating the rule this one exists to hold.

### What each host does when a project will not open

The exit code is the only answer a shell gets, so each host has to spend it on
what is actually fatal *for that host*. The runtime plays a finished game; the
editor is the tool you repair one with.

`SceneSerializer::load` returns true after a load in which every reference went
unresolved - each one is a component slot left empty, not a parse failure - so
the last row is not something the loader can answer for. The cooker installs an
`EngineErrorLog` sink around the load and counts what `reportError` puts in it,
because a scene whose references resolved to nothing is exactly the state that
gets packaged and ships a world with empty slots. That sink catches every
failure the load reports, not only unresolved assets - a prefab file that will
not open is in it too - so the message counts failures and names the two
remedies apart rather than telling the author of a deleted file to save the
project. `vkm package` needs no rule of its own: it already returns on a
non-zero cook.

| Condition | `vkm_runtime` | `vkm_editor` | `vkm_cook` |
|-----------|---------------|--------------|------------|
| Log file cannot be opened | exit 1 | exit 1 | exit 1 |
| Window / GL context cannot be created | exit 1 (throws) | exit 1 (throws) | n/a - headless |
| No gameplay module in the project's `bin/` | exit 1 | opens, logs WARNING | n/a |
| Module present but will not load (version, entry, unreadable) | exit 1 | opens, logs ERROR | n/a |
| Entry scene named but will not load (`SceneBoot::Failed`) | exit 1 | opens on the default scene; error toast and an Errors panel entry | exit 1 |
| No entry scene and no `vkmBuildScene` (`SceneBoot::Default`) | exit 1 | opens on the default scene | cooks every scene in `scenes/`; exit 0 if there is none |
| No entry scene, module builds the world (`SceneBoot::Project`) | plays it | opens it | cooks every scene in `scenes/`; exit 0 if there is none |
| An asset fails to cook | n/a | reported, the session continues | exit 1 |
| Entry scene loads, but something in it does not (unresolved reference, prefab file missing) | plays it with what loaded | reported, the session continues | exit 1 |

The cooker reaches its rows by its own path rather than through
`bootProjectWorld` - it has no `Scene` to boot into and no module to ask. It
first re-bakes every asset the library holds whose source changed since its
last cook (`AssetCooker::cookStaleAssets`), then loads every `.json` in the
project's `scenes/`, in sorted order, plus `entryScene` when that file lives
elsewhere, and cooks each in turn. A scene that will not load, or loads with a
failure reported, stops the cook with exit 1.

`vkm_server` has no column because it answers the runtime's column for every row
in it: it plays the same world, so a project it cannot open is a game it cannot
referee. It adds one condition of its own - a project whose module exports no
`vkmSetupNetwork` has not said what a player is, so there is nothing to serve
and it exits 1 where the runtime would have played it offline.

Two judgments behind that table:

- **A game is its module.** Behaviors are created through the registry the module
  fills, so with no module `ComponentSerializer` can construct none of them. It
  keeps each one as an `UnknownBehavior` - the type name and its property object,
  held as text and written back out unread - so a save while the module is
  missing loses nothing, and the runtime plays a world that draws and does
  nothing. Exit 0 would report that as a game that played, which
  is what the exit code is spent on here. The editor logs it and opens anyway,
  because a module that will not load is fixed by rebuilding it and reloading,
  and you need the editor open to do that.
- **A broken entry scene is not fatal to the editor**, which is the thing you
  open a broken scene in. The default scene stands in carrying no save path, so
  a save cannot overwrite the file that failed to load; the reason is in the log
  and, because `bootProjectWorld` reports it through `reportError` rather than
  logging it itself, in the Errors tab and a toast however the project was
  opened. The same rule holds for an open made from inside a session:
  `SceneIOController::loadPath` moves the current path only when the read
  succeeded, so a failed `File > Open` leaves the title - and the file the next
  Ctrl+S writes - on the scene still in the viewport.

## Components

| Layer               | File                                         | Purpose                                                                                  |
|---------------------|----------------------------------------------|------------------------------------------------------------------------------------------|
| Scene serializer    | `src/engine/io/scene/scene_serializer.h`           | Top-level save/load for a `Scene` + the assets it references. Transactional.             |
| Prefab              | `src/engine/io/scene/prefab.h`                     | One entity subtree in its own file, instanced by reference from a scene.                 |
| Asset serializer    | `src/engine/io/asset/asset_serializer.h`           | Save name-only asset references; on load resolve them through the asset library: the cooked file, else the recipe. |
| Asset library       | `src/engine/io/asset/asset_library.h`              | The cooked-asset database manifest: maps an asset name to its type + recipe hash, and derives its file locations. |
| Asset cooker        | `src/tools/cook/asset_cooker.h` (editor)     | Bakes assets from their recipe into the library + cooked binary cache (`cooked/`).        |
| Cooked format       | `src/engine/io/asset/asset_cook.h`                 | The `.vkmc` binary reader/writer. Readers are defensive: every count and size is validated before it is used. |
| Component serializer| `src/engine/io/scene/component_serializer.h`       | Per-component to/from JSON. Mechanical, one save/load pair per component type.           |
| Shader reload       | `modules/vkmGL/src/shader/gl_shader_reload.h`      | Live-shader registry + `reloadChangedShaders`; recompiles every live shader when the newest write time under `shaders/` moves, and each one that no longer compiles keeps its old program. |

Every JSON write in this layer - scene, prefab, manifest - goes through
`detail::writeJsonFile` (`src/engine/io/json_file.h`): the dump lands in a
sibling `.tmp` and is renamed over the target only once the stream reports a
clean write, so a full disk cannot leave a truncated file where a good one was.
`detail::readJsonFile` is the matching read.

## SceneSerializer

`SceneSerializer::save` emits a JSON object with four top-level blocks:

- `assets`: name-only references to every asset the scene names - `Mesh`, a
  `Collider`'s mesh parts, `LOD` levels, `Decal`, the rig plus clip an
  `Animator` names, the sound an `AudioSource` names, the texture a `UIImage`
  draws, and every `AssetRef` field on a behavior - plus the textures those
  materials use. A reference the assets block never lists is one
  the loader never recreates, so every component that names an asset has to be
  walked there, and a behavior's authored fields are walked with them. The asset
  *data* lives in the cooked library (keyed by name), not in the scene file, so
  the scene stays tiny and diff-friendly. Assets marked `hidden = true` are
  skipped (editor previews, fallback textures, bundled primitives). A behavior's
  name is the one kind that can dangle - a component's comes from an asset that
  exists - so it is emitted as authored and reported by the load if the library
  has no such entry.
- `entities`: one record per entity. Entities are stored at their slot
  index, and each component is keyed by its short name (see Component
  serializer below).
- `environment` and `physics`: the scene-global `Environment` and
  `PhysicsSettings`, each a single reflected object rather than a component.

Every number in the finished document is finite. JSON cannot spell an infinity
or a NaN - nlohmann writes both as `null` - and a `null` where a float belongs is
a type error the component loaders throw on, which fails the whole load: one bad
field would cost every entity in the file. So the document is held to the rule
where it is written (`detail::writeNonFiniteAsZero`, which `detail::writeJsonFile`
runs on every document the engine writes to disk and `saveToString` on the
snapshot): a non-finite value is written as `0` and named in the log by its
path, e.g. `/entities/12/components/AudioSource/volume`. The play-mode snapshot
goes through the same builder, the same rule *and* the same cook, so a scene
that could not be saved cannot fail to restore on Stop either - the cook is what
makes that true of its assets, because the snapshot names them exactly as the
file does and a name is restorable only once the library holds a record for it.
A finite number writes as itself, and the read side stays strict: a `null` in a
scalar field is a type error, never "keep the default".

Where the snapshot stops being the file is the list beside it. A scene document
names the assets the scene uses and nothing else, which is right for something
somebody saves and not enough for something that promises to put a session back:
a sound imported and not yet assigned to a source is in the Asset Browser, in
every picker, and in no component, so the scene never mentions it, and a session
that edited it would leave that edit standing. So `captureSnapshot` records
`AssetSerializer::saveAllAssets` - every live asset that has a name and is not
hidden - alongside the document, and `restoreSnapshot` feeds that list back
through `loadAssets` before it reads the scene. The extra list never reaches
disk and the file format is untouched; it is the session's half of the snapshot,
and the cook above is what makes it restorable.

**Restoring keeps the graph and rebuilds its contents.** The two halves of the
snapshot go back differently from the way a file load puts a scene in place, and
the difference is the undo history the editor keeps across Stop. Its steps hold
the assets they are to put back, as handles, and a handle is a slot index into
one `ResourceManager` - so `loadFromString` reads the scene with
`AssetPolicy::Merge`, resolving names against the graph the snapshot was
captured from rather than swapping a rebuilt one in behind them, and the asset
list goes back with `LoadMode::Reload`, which rebuilds each material into the
slot it already occupies (`ResourceManager::swapValue`: contents exchanged,
identity and name left with the slot, version bumped so the backend re-uploads).
Both halves are needed. Without the merge, a rebuilt graph restarts at the same
indices and generations, so a surviving step resolves to whatever landed in its
slot, and an undo of a mesh assignment would silently restore a different mesh. Without the reload, a material edited during the session would keep that
edit, because nothing would have put the old contents back.

**Only materials are rebuilt.** Play cooks first, so every other kind the graph
holds resolves to its cooked file, and the cooked loaders answer a name already
resident with the asset already there. Rebuilding them anyway would re-read
every mesh and texture the project cooked on every Stop, and a rebuild through
an import still decoding would hand the live slot a loading stub whose decode
lands on the discarded shell. The Material Editor is the one tool that edits an
asset during play; a mesh, texture, rig, clip or sound a behavior edits keeps
the edit after Stop.

What the session created goes: after the reload, `AssetSerializer::dropAssetsNotIn`
removes every named, visible asset the snapshot's list does not carry, so a world
a session generated does not leave its assets behind in the Asset Browser and in
every picker. That cannot strand an undo step - an asset created during the
session is held by no step taken before it, and the steps taken during it are
dropped by the same Stop. An import made before Play is in the list, and stays.

**Opening a scene answers this the other way, on purpose.** Stop promises to put
one session back; an open is leaving that world for another, and it drops the
undo stack, the selection and the material previews on the way through. So the strays go with the session that imported them - carrying them
would grow the graph by a scene's worth of assets per open and cook every one of
them into the library at the next save - and the editor names them in the log
and counts them into a toast rather than letting them vanish quietly. See
[the editor](editor.md#what-an-open-does-to-the-sessions-imports).

`SceneSerializer::load` is **transactional for both entities and assets**:

1. Read the file (early-out on parse failure; live scene untouched).
2. Resolve each asset reference through the `AssetLibrary` manifest into a
   **staging** `ResourceManager` (not the live one): meshes/textures load from
   their cooked binary, materials from their library `inline` form. Idempotent:
   assets already present by `name` are skipped. Each asset is read inside a
   guard of its own: a recipe that does not read - a string where a number
   belongs - goes through `reportError` with the asset's name and its recipe
   file, and costs that asset alone, whose references are left unresolved as a
   missing one's are.
3. Deserialise every entity into a **staging** `Scene` (not the live one),
   expand each prefab instance into it, wire the parent links, then read the
   `environment` and `physics` blocks. All of it sits inside one guard, so a
   drifted field anywhere - a string where a number belongs - fails the load
   instead of unwinding out of it. **The abort names the record it was standing
   on**: `loadInto` catches, prefixes the component key and rethrows, and the
   entity loop keeps the id it is reading, so a single mistyped field reports
   `Aborted while reading entity 17 of 'scene.json': component 'UIButton': type
   must be string, but is number` rather than a file name and a JSON error. The
   thrower knows neither half - nlohmann names the type mismatch and nothing
   about where in the file it is - and without them a one-character drift costs
   a bisection of the file. A behavior's authored fields are the exception: a
   value of the wrong type there keeps the field's current value and warns,
   because a behavior's properties are the part of a scene most often edited by
   hand and read by code the engine does not own. An asset *name* step 2 did not bring in is
   not a drifted field and does not fail the load: the component's slot is
   left empty and the miss goes through `reportError`, so the editor toasts it
   and keeps it in the Errors panel rather than burying it in a log the editor
   has no view of. By design there are no benign cases - the `assets` block is
   built by walking exactly what the scene references. **A prefab that will not
   open goes through the same seam**, and costs more: an unresolved name loses a
   component's field, while a prefab whose file `Prefab::instantiateInto` cannot
   read loses the whole authored subtree, leaving a childless entity in the
   viewport. The instance is not dropped with it - the reference and its
   overrides stay on the entity and survive the next save - so restoring the
   file and loading again brings the subtree back, which is what the report
   says and what the Inspector's Prefab card repeats on the empty instance.
4. On full success, swap both staging containers in one step:
   `Scene::swap` for the scene, and `ResourceManager::swap` for the assets. The
   swap leaves the font slot where it is: fonts are baked at startup and never
   enter a scene file, so the staging RM has none, and trading the slot away
   would lose every `UIText` its font on load.

### A reference that did not resolve is kept, not erased

An asset name the load cannot answer leaves the component's slot empty, and an
empty slot is indistinguishable from one nobody ever filled - so a save that
went by the slot alone would write `""` over the name, drop the entry from the
`assets` block, and report it as an ordinary successful save. Opening a scene
whose gitignored `library/` a teammate never committed and pressing Ctrl+S out
of habit would be enough to lose every reference in it, permanently.

The name is the author's work, so the load keeps it: `resolveAssetRef` records
what it could not resolve, `SceneSerializer` attaches it to the entity as a
`MissingAssets` component (present only on the entities that have one, never
written as a component of its own), and the save puts it back in two places -

- the **field it came from**, but only when the save has just left that field
  as an empty string, so a slot the author has since filled keeps what they
  chose; and
- the **assets block**, as the name-only entry a behavior's authored reference
  already gets, because a field naming an asset the block does not declare stays
  unresolved even once the library holding it is back.

A reference with nowhere to return to is never recorded in the first place, so
neither half has to know about it: `LOD`'s levels are a ramp rather than a name,
and the holes in that ramp are its own decision.

With both, restoring the library and reopening the scene brings the reference
back to life. The editor also names them on the entity: the Inspector heads a
selection that has any with the component, field and name it could not load,
which is what separates "the mesh is gone" from "I never assigned one".

**What reads the record asks whether each field is still empty.** The other way
an author can answer a missing reference is to pick something else. The save
already leaves a filled slot alone, but the banner would go on saying the
reference did not load, and the assets block would go on declaring a name the
scene has stopped using - so every later load would report it. The record itself
is never trimmed, though, because a filled field is one an undo can empty again,
and the name has to still be there when it does. So
`SceneSerializer::unresolvedRefs` returns the entries whose field is still
empty, and the Inspector's banner and the assets block read that rather than the
record. It answers by writing the entity and reading the field back, which is
the same question the save asks and so cannot drift from it.

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

## Cooked assets: AssetLibrary, AssetSerializer, the cooker

An asset's **recipe** - the `source` descriptor its import or generator wrote,
with a `kind` - is the editable source of truth. Its **cooked** file is a binary
cache derived from it. Every asset is its own file:

- `library/<type>/<uid>.json` - the recipe; for a material, its runtime form.
  Version-controlled.
- `cooked/<type>/<uid>-<key>.vkmc` - the cooked body (below). Regenerable;
  git-ignored.
- `cooked/_manifest.json` - one row per asset: its name and type, its recipe
  hash and the source files that hash read, under a `manifestVersion`. It is
  derived like the files it indexes, so it lives with them. `AssetLibrary` is
  the in-memory view.

Neither filename is recorded: `AssetLibrary::recipePath()` and `cookedPath()`
derive them from (type, name), so the cooker that writes a file and the loader
that reads it cannot disagree about where it is. The uid is a hash of
`"<type>:<name>"`, since a name may hold separators and colons.

An imported asset is **named by its project-relative path**, and so is the
`path` in its recipe - `ProjectPaths::toProjectRelative` at the loader boundary,
`resolveProjectPath` to open it again. An absolute name would make the
library's layout a function of one machine's home directory. A source outside
the project keeps its absolute path, and the conversion happens before the
by-name dedup, or one file reached by two spellings becomes two assets. A model
file's parts are named by the reference and the part -
`assets/props/crate.glb:mesh0`, `...:skeleton`, `...:clip0`
([resources.md](resources.md) lists the parts).

### Loading: cache first, recipe on a miss

`AssetSerializer::loadAssets` reads a document's sections in
`ASSET_DEPENDENCY_ORDER` (`resource/asset_type.h`) - textures before the
materials that name them, rigs before the clips and meshes bound to them - and
skips a name the graph already holds. For every kind but a material it asks the
cooked loader (`io/asset/cooked_loader.h`), which finds the manifest row, probes
the file with `AssetCook::isCookedCurrent` and reads it - a mesh or texture on
the `ThreadPool`, a skeleton, clip or sound at once - giving the asset a
`{"kind":"cooked","name":...}` stand-in for a source. A rig or clip is too small
to earn an async lane; a sound needs no decode, and a worker hop would open a
window in which a scene's sounds exist and are silent.

When no current file serves the name, the recipe is imported through the
`AssetFactory` seam (`io/asset/asset_factory.h`): one import per kind that cooks,
which the editor and `vkm_cook` install (`registerRecipeAssetFactories`, in
`src/tools/cook/recipe_registration.cpp`) and the runtime and server leave null,
so they link no importer. A material has no cooked file and no slot: its recipe
is its runtime form, and io applies it itself (`AssetSerializer::applyInline`).
A name the manifest has no row for still loads from its recipe when one is on
disk, since the recipe's path is derived from the name alone: a lost or refused
manifest costs a re-cook, not the project.

| `kind`                 | Read by          | Resolves to                                         |
|------------------------|------------------|-----------------------------------------------------|
| `inline`               | every host       | A `MaterialAsset` from PBR scalars + texture refs - every material's recipe, whatever it was imported from |
| `generator` / `decimate` | editor, cooker | Procedural / LOD meshes                             |
| `file` / `model` / `model-image` | editor, cooker | stb texture import and sound decode; cgltf / ufbx mesh, rig and clip import (a clip recipe also carries its authored `markers`) |
| `solid`                | editor, cooker   | Single-colour textures, the engine's defaults among them |

A `decimate` recipe is an LOD level kept as the instruction that makes it:
`{"kind": "decimate", "base": "<mesh>", "ratio": 0.5}`, the share of the base's
triangles the level aims for. `decimateRecipe` and `decimateRatioFromRecipe`
(`src/tools/cook/lod_generator.h`) are its one writer and one reader; a missing
or non-numeric `ratio` asks for a half.

What an import returns is filed under the name the document asked for. An
importer dedupes by its own identity - a texture by its path, a model's parts by
names derived from the file - so when two library entries import one file it
can answer with an asset held under another name. That entry is left
unresolved, with a warning, rather than taking the other asset's name from
everything resolving it.

Adding a new asset kind is a row in `VKM_ASSET_KINDS`, which brings its enum
value, its directory and its section of the assets block, and then the hand work
the comment above that list names: a case in `cookedKind`, a place in
`ASSET_DEPENDENCY_ORDER` (the build fails until it has one), a cooked loader,
and an `AssetFactory` slot with its import.

### Saving and cooking

**Save** - `AssetSerializer::saveAssetsForScene` walks the components that name
assets (`Collider`, `Mesh`, `LOD`, `Decal`, `Animator`, `AudioSource`,
`UIImage`, plus the asset fields a behavior declares) and emits **name-only**
references. In the editor, `SceneIOController` first calls
`AssetCooker::cookAllAssets`, which records every non-hidden asset in the
library and bakes its cooked file, skipping one whose hash is unchanged and
whose file this build can read or is baking. It waits for imports still in
flight first (`awaitAsyncLoads`): an asset that has not landed has nothing to
bake. It returns false when any asset failed, which is `vkm_cook`'s exit code.
An asset with a recipe and nothing in it - its source did not load - is a
failure, since the manifest would promise a file nothing produced; one with no
recipe at all is only a warning, since a project may build assets in code and
name them, as `examples/potion_runner` does.

Recording an asset - its recipe and manifest row - is a few small writes and is
what makes a saved name resolve, so Save and Play do it before they return.
Baking a texture or mesh takes seconds, and the editor hands it to the
`ThreadPool` with a copy (`AssetCooker::Bake::InBackground`); until it lands,
loads fall back to the recipe. One already under way is not started twice, and
one that fails is logged and baked again by the next cook. `vkm_cook` bakes
before returning (`Bake::Now`).

`finalizeAsyncLoads` / `awaitAsyncLoads` (`system/async/async_loader_system.h`)
land async decodes without a frame - drain once, and drain until quiet. A
completion carries the worker's whole asset, swapped into the slot it was
requested for, which keeps its identity and source. The second lands only the
completions meant for the graph it was given and puts the rest back, because a
scene load waits on its staging graph while the live graph's imports are in
flight. `AsyncLoaderSystem` calls the first every frame; the cooker and the
`decimate` recipe, which would otherwise simplify a base mesh that has not
arrived, call the second.

### The cache key

A row's recipe hash is a 64-bit FNV-1a (`core/fnv1a.h`) over the recipe bytes,
folded with **the bytes of every source file the import reads**
(`AssetCooker::sourceFiles`, `AssetCooker::foldSourceContent`): the file the
recipe names, a `.gltf`'s buffers in files beside it, and an `.obj`'s material
libraries. A re-export leaves the recipe byte-identical, so without the content
the hash never moves. It is hashed by content rather than write time, which
differs between machines, and at cook time rather than written into the recipe,
which describes the import rather than one machine's bytes. The row records
those files as its `sources`, which `vkm package` reads to leave them behind.
A derived asset - a level decimated from another mesh, a clip bound to a rig
from another file - names its dependency only by name, so
`AssetCooker::foldDependency` folds the dependency's recorded key into its own;
that is why the cooker cooks in dependency order, a decimated level after its
base. The cook reads an asset's art once a session and keeps it while the
asset's version and recipe stand, so a save does not read the project's art
again.

An asset served from the cache carries the `cooked` stand-in, not its recipe,
so no open or save sees re-exported art behind it. **`vkm_cook` does**: before
it loads a scene, `AssetCooker::cookStaleAssets` hashes every manifest row's
recipe off disk and re-imports and re-bakes each asset whose hash moved, whose
base did, or whose recorded file is not current.

A cooked file is named for what it was derived from: `<uid>-<key>.vkmc`, where
the key mixes the recorded recipe hash, the kind and `COOKER_VERSION`
(`AssetCook::cacheKey`, `io/asset/asset_cook.h`). So:

- **Staleness is a lookup that misses.** A file baked from a since-changed
  recipe is never asked for. The cook that re-bakes it deletes the file the
  previous record named, and `vkm_cook` ends by deleting every cooked file no
  record names (`AssetLibrary::removeUnrecordedCooked`) - an older version's, a
  killed cook's temporary - so what `vkm package` copies is what the game reads.
  Deleting `cooked/` outright is always safe.
- **A version bump orphans exactly what it should.** The version is in the name
  and in the header, where a reader refuses a mismatch - what a file copied
  under a name not baked for it meets.
- **An interrupted cook leaves nothing behind.** The writer builds beside the
  file and renames onto it, atomically.

`COOKER_VERSION` is the one version: bumped when anything the cooker writes
changes - a kind's layout, an importer flag, a mip policy, a welding rule. A
change that forgets the bump leaves every project serving what the old cooker
made, so `testTheCookersOutputIsPinnedToItsVersion` runs every decision a cook
makes over fixed inputs - the vertex orderer, the BC4/BC5 encoder, a cut-out
through the mip filter and BC7, a model through the import's weld and tangents -
and fails when the bytes move under an unchanged version. Its inputs are
integers and its float code rounds alike in every build type, as the x86-64
baseline has no fused multiply-add.

### The cooked bodies

Every `.vkmc` is the same header - magic, endian sentinel, asset kind, cooker
version, payload length - followed by a body. The kind tag is its own numbering,
not the enum's value: `cookedKind` in `asset_cook.cpp` maps an `AssetType` to
it. A texture's colour space is its internal format's (`TextureAsset::isSrgb`).

| Body | Holds |
|------|-------|
| Mesh | Bounds, the vertex, index and skin counts, the skin radius, the rig name's length, then bulk vertices, indices and skin, then the rig name |
| Texture | The `TextureParams` fields - the level count among them - then every level, level 0 first: texels, or 4x4 blocks for a BC format |
| Skeleton | Bone count, a `{parent, nameLen}` record per bone, bulk inverse-bind matrices, bulk bind-pose TRS, then the concatenated names |
| Animation clip | Bone count, duration, the six key-array counts, the skeleton name length, the marker count and marker-name length, then the bulk `ClipBone` table, the six key arrays, the rig name, a `{time, nameLen}` record per marker and the concatenated marker names |
| Audio clip | Sample rate, channel count, sample count, then the interleaved 16-bit PCM |

Every body puts its counts before what they size, and a reader bounds every
count by **division** against what is left of the payload before multiplying it,
then requires the remainder to land on exactly zero.

A mesh body is baked in the order a GPU draws fastest: the cook reorders a copy
with `optimizeMeshForGpu` (`src/tools/cook/mesh_processing.h`) - triangles for
the post-transform vertex cache, then vertices in first-use order, the skin
stream remapped with them. It is the one place a mesh is reordered, so a mesh
drawn from its recipe and the same mesh read from its cooked file hold the same
triangles in different orders.

Past the size math, each reader checks what a correctly-sized file can still get
wrong. A skeleton and a clip are checked by the asset's own rules,
`findSkeletonFault` and `findClipFault`, which the writer and
`SkeletalAnimationSystem` apply too:

- A skeleton's three arrays are parallel and its bones **parent-before-child**
  (`-1 <= parent < index`), so every consumer composes a pose in one forward
  loop.
- A clip's key times and values pair up, its duration is finite, and every
  channel range lands inside the array it addresses - the sampler indexes them
  directly.
- Every clip marker's time is finite and inside `[0, duration]`; one outside the
  timeline never fires at the instant it names.

The rest belong to the cooked format alone, and nothing downstream re-checks
them:

- A bone count past `MAX_SKELETON_BONES` is refused - a corruption threshold,
  not a capability. `MAX_AUDIO_CHANNELS` and `MAX_AUDIO_SAMPLE_RATE` are the
  same for a sound.
- A sound's sample count divides by its channel count: the mixer reads whole
  frames.
- Every index in a mesh names a vertex the file declares: decimation and GL use
  them as-is.
- A mesh's skin stream is parallel to its vertices or absent, and every bone
  index in it is under `MAX_SKELETON_BONES`: the vertex stage reads the pose
  palette with it, and nothing on the CPU would notice a bad one.
- A texture's size, format and level count describe exactly the bytes beside
  them, since each level reaches `glTexImage2D` or `glCompressedTexImage2D`
  verbatim. The level count is one or the whole chain down to 1x1 - a partial
  chain samples as black under a mipmap filter.

### What a version bump costs

**A re-cook, not a re-import.** Refusing an old file is affordable because the
recipe can still produce a new one: `vkm_cook` rebuilds every file the manifest
promises, and opening the project and saving rebuilds the ones its scenes name.

One probe, `AssetCook::isCookedCurrent`, serves both ends, so they cannot
disagree. It reads the header and measures the file: absent, foreign, of another
kind, or longer or shorter than its header declares is not current - the last is
what an interrupted write leaves. **The loader** asks it before reading, and
loads the recipe instead on a no; **the cooker** asks it before skipping an
asset (`isUpToDate`), because a file this build cannot read is not an output to
skip. The probe logs nothing: a stale cache is normal, and the caller says what
the miss meant. A host with no importers - the runtime - logs that it has no
cooked file and imports no recipes: a shipped build cannot rebuild its cache.

Which is why **a packaged game carries no recipes but its materials,** and no
source art: `vkm package` ships `cooked/`, the manifest and `library/materials/`,
and leaves out every file a row lists under `sources`. A material's recipe is
its runtime form; any other kind's describes an import the runtime cannot
perform. So the hosts read one manifest two ways (`AssetLibrary::Truth`): the
editor and the cooker take the recipes as the truth and drop a row whose recipe
is gone, while the runtime and the server take the manifest's word, because in a
package the recipes are missing on purpose.

A re-cook does not recover a recipe whose **source art is gone**; that fails
the cook, as above.

## ComponentSerializer

For every component, there is a `save(const T&) -> json` and a
`load(const json&, T&)`. They are intentionally mechanical, one pair
per component.

Two kinds take a second argument, for the same reason: a reference cannot
survive a file as the handle it is in memory. A component that names **assets**
takes the `ResourceManager`, which turns a handle into a name and back. A
component that names **entities** takes the carrier - an `EntityNamer` to save,
an `EntityResolver` to load - because what an entity is called depends entirely
on what is carrying the reference. A scene names it by its slot, a prefab by its
place in the file, an undo snapshot by the slot it is restoring, and a
connection will name it its own way.

The carrier is an argument because the same raw number means three
different things depending on which carrier it came from - a live slot in this
`Scene`, a slot saved in a file, a one-shifted index into a prefab's entity
list - and a field like `Joint::connected` cannot say which. Taking the carrier
as an argument makes the number impossible to read without naming the namespace
it is in, and `EntityId` being its own type makes writing one back impossible.

What the round trip carries is `VKM_SCENE_COMPONENTS`, at the top of
`component_serializer.h`. Read the macro for the set; the rest of this section
is what the list cannot say.

A row's letter is which of `ComponentSerializer::save`'s shapes it takes: `R`
names assets, so its save and load take the `ResourceManager` that turns a handle
into a name and back (`Collider`, `Mesh`, `LOD`, `Decal`, `AudioSource`,
`Animator`, `UIImage`); `E` names other entities, so it takes the carrier described above
(`Joint`, `Ragdoll`); `P` writes itself.

`Hierarchy` is written beside them but is not a row: only `parent` is serialized,
and the sibling pointers are rebuilt on load by re-running
`HierarchyOperations::setParent`.

What a component deliberately leaves out is the other half, and it is the same
rule every time - a field that describes a *session* rather than the thing does
not round-trip:

- `AudioSource::playing` and `started`: a scene row holding a half-finished sound
  would resume a noise whose beginning nobody heard (see [Audio](audio.md)).
- `CharacterController`'s `jumpRequested`, `grounded` and `groundNormal`: a row
  holding a half-consumed jump request would replay it on load
  (see [Physics](physics.md)).
- `Rigidbody`'s sleep state, contact-normal outputs and derived mass properties.
- `ScriptComponent` (JSON key `"Script"`) is the one row with a format of its
  own: each behavior is stored by its registered type name and recreated through
  `BehaviorRegistry` on load - a type the registry does not know is kept verbatim
  as an `UnknownBehavior` and written back out unread, because an unregistered
  type says the module is missing, not that the author wants the behavior gone.
  Authored fields sit in a `properties` object beside it; `Behavior::visitFields`
  walks them in both directions, and enums are written by name so reordering one
  does not invalidate saved scenes. See [Scripting](scripting.md).

A `Collider` part writes its `shape` by name alongside every shape's fields, so
switching a part to a capsule and back does not lose the half-extents it was
authored with. It is reflected like the rest, parts included, so a part missing
a key keeps that field's default - a part with no `shape` reads as the default
shape - the way every reflected field reads an absent key. A mesh part names its
mesh, which makes `Collider` an `R` row; its triangles are built from that mesh
on load (`syncMeshCollider`) and never written.

`Environment` (sky / night / fog groups) and `PhysicsSettings` are scene-global
rather than per-entity, so they are written as their own top-level objects.
Both are fully reflected: each field list lives once, in `ecs/environment.h`
and `ecs/physics_settings.h`, and both directions walk it. Render tuning (GTAO / bloom / MSAA / ...) lives in
`RenderSettings` on the Engine, not in a serialized component.

`Mesh` references handles by `name` rather than by `Storage` index;
that is what makes assets a stable identity across save/load.
`Hierarchy::parent` stores an index into the file's own entity table, not an
`EntityId`, and it is the one component the flat list does not carry: the
parent it names may not exist when the child is read, so `saveComponents`
writes it and the loader's second pass wires it up once every entity exists.

### Adding a component to the round trip

Two localised edits, no registry table, no virtual dispatch:

1. A `save` / `load` overload pair in `component_serializer.h` (+ `.cpp`). If the
   component has nothing but plain reflected fields, both bodies are one call to
   `saveReflected` / `loadReflected`. A component that references assets by name
   adds a third overload beside them, `emitAssetRefs(const T&, AssetRefs&)`,
   which records the handles it holds without writing anything.
2. A row in `VKM_SCENE_COMPONENTS`, at the top of `component_serializer.h` -
   `P(Type, "Key")` for a component that refers to nothing outside itself,
   `R(Type, "Key")` when it names assets and its save/load take the
   `ResourceManager`, `E(Type, "Key")` when it names entities and they take the
   `EntityNamer` / `EntityResolver`. The letter says what the component refers
   to, not what it is - every row is an ordinary data component. Saving,
   loading, the known-key set behind the "unknown component key" drift warning,
   and the walk over what an entity references (`collectAssetRefs`, which the
   `assets` block `saveAssetsForEntities` builds from, and which the editor's
   Asset Browser asks whether an asset is in use) all expand from that one list.

As four hand-kept lists they would fail silently in both directions: a key saved
and registered but never loaded would round-trip to nothing, while the drift
warning that exists to catch it stayed quiet because the key was still known;
and a component whose assets a hand-written walk forgot would save its handle as
a name the `assets` block never listed, so the next load resolved it to
nothing. An `R` row with no `emitAssetRefs` overload fails to compile at the
walk, naming the component that needs one.

The key is spelled out in the row rather than derived from the type name, since
it is the format - `ScriptComponent` is stored as `"Script"`. `Hierarchy` is not
a row: it is written explicitly and read by the loader's second pass.

Scene load runs in two passes for the same reason an `E` row exists: every
entity is created at its saved slot before any component is read, because a
slot is only an entity once they all exist. The entry checks - an unusable id,
a repeated one - belong to that first pass, since after it every id is alive
and a second aliveness test would call every entity a duplicate of itself.

The editor solves the same problem the same way one directory over
(`VKM_EDITOR_SNAPSHOT_COMPONENTS`), and a component's *field* list is already
single-sourced by `VKM_REFLECT_BEGIN` / `VKM_F`.

`loadInto` overwrites a component the entity already carries instead of adding a
second one, because a prefab instance root is loaded twice, once
from the scene block that placed it and once from the prefab file.

## Prefabs

A prefab is a scene fragment - one entity and its descendants, with the same
per-entity component shape a scene uses, in its own file:

```json
{"version": 3, "nextUid": 3,
 "entities": [{"uid": 0, "components": {...}}, {"uid": 1, "parent": 0, "components": {...}}],
 "assets": {"textures": [...], "meshes": [...], "materials": [...],
            "skeletons": [...], "clips": [...], "sounds": [...]}}
```

The `assets` block is the same one a scene carries, for the subtree this file
describes, and it is what makes a prefab instantiable anywhere. Component
references are asset *names*, and a name resolves to nothing unless something
already loaded it, so without the block a prefab would build correctly only
where a scene had loaded the same assets first. Every path that
reads components out of a prefab loads the block first, `Prefab::reloadComponent`
included, since reverting an override re-reads an asset name too. Loading is
idempotent by name, so an instance after the first costs a lookup per entry.

Parents precede children and a child names its parent by *index into this
file's own array*, because entity ids mean nothing outside the scene that issued
them. `Prefab::save` drops the `Hierarchy` block for that reason and rewrites
the link as an index.

That file is one a person can write, so `readPrefab` establishes its shape and
its identities once, and every entry point reports what it cannot use rather
than raising: the callers are an editor showing a toast beside its own file
picker and a scene load with other entities to build. Past that check an entry
is an object, its component block is one too, and no two entries answer to the
same uid; the numbers still read out of it - `version`, `uid`, `parent` - treat
a key of the wrong type as an absent one.

A duplicate uid is a hard refusal because it makes an override's address
ambiguous, and the four places that resolve one disagree about what to do with
it: `applyOverrides` patches every match, while `reloadComponent`,
`definesComponent` and `entityWithUid` each stop at the first. `Prefab::save`
keeps a uid only when it is that entity's alone, so the writer cannot produce a
file its own reader refuses - a collision, or a child wearing the root's zero,
costs one renumbered entity instead of an unloadable prefab.

A scene's own `assets` block still walks the entities inside its instances, even
though the prefab lists them: an instance may override a `Mesh` or a `Decal`
at an asset the prefab file never names, and only the scene's walk sees that.

A scene stores an instance as a `PrefabInstance` (the source path) plus the
root's `Transform` and `Hierarchy` - where it sits and what it hangs off belong
to the scene - and the saver skips the whole subtree beneath it. The loader
expands it after the entity pass, so the roots keep their saved slots and the
prefab's own entities take whatever is free - unless the caller says where they
stood: `loadFromString` takes `Prefab::instanceSlotsOf` the world it was written
from, which is how the editor's Stop puts every slot back for an undo history
that addresses entities by slot. Editing the prefab therefore changes every
instance the next time a scene loads, which is the point.

`Prefab::save` turns the subtree it wrote into an instance of the file, so the
master copy is not a loose subtree the next scene save would inline. Saving an
instance back over its own source is how a prefab is edited; its overrides are
baked into the file and then cleared, because keeping them would pin that one
instance to the values every other instance just adopted. Nesting is refused in
both directions - a subtree containing an instance, and a root inside one.

### Per-instance overrides

What varies per instance is the root's `Transform` and any number of *overrides*:
one field of one component of one entity in the subtree, stored on the root's
`PrefabInstance` and written beside the reference as `uid -> component -> field`.

The entity half of that address is why the file carries a `uid` per entity and a
`nextUid` high-water mark. Ids are runtime slots, array position moves when the
prefab is re-saved, and `Name` is user-editable and not unique, so none of them
survives an edit to the prefab; a uid is handed out once and never reused.
`PrefabEntity` carries it at runtime, and a scene never writes it - a scene never
writes a prefab's entities at all. `nextUid` is seeded from the file being
overwritten, because the entity that held the highest number may be the one just
deleted and only the file still remembers it.

The value is the field's own serialized JSON as text, and the merge happens on
the document before `loadComponents`, not as a patch on a built component: the
loaders construct a fresh component and assign, and several cannot be run twice.

Overrides are stored, never re-derived by diffing an instance against its file.
`load(save(x))` is not `x` here - an unresolvable asset name comes back as `""` -
so a diff would manufacture overrides out of load failures. When the prefab
changes underneath an override (the uid, component, field or type is gone, or it
addresses the root's `Transform`, which is the instance's own pose) the entry is
kept, reported once, and not applied, so renaming a field and renaming it back
does not lose the edit.

`Script` is refused the same way, and for a reason that is about the address
rather than about drift: the component serializes as one field holding the whole
behavior list, so the only override this format can spell replaces every
behavior on the instance. The editor neither writes one nor shows one, so an
applied one would be invisible and unrevertable. A hand-edited file that names
it is reported and left alone - see
[scripting.md](scripting.md#authored-fields-inside-a-prefab-instance).

The type check walks the prefab's value and the override's together rather than
comparing their top-level kinds, because an array of the right kind holding the
wrong elements throws inside the component loader - the failure the check exists
to prevent. Only a numeric array is length-checked: that is a fixed-width vector
whose length is part of its type, where `Collider::parts` and `LOD::levels` are
lists their loaders read at whatever length they find.

They are authored in the inspector: editing a component on an entity that
belongs to an instance records the changed fields there and then
(`src/editor/command/prefab_overrides.h`), each card marks the fields the
instance owns, and `Prefab::reloadComponent` gives one back to the prefab.

## Shader hot reload

Shaders are not assets and not part of the library - they are source files read
CWD-relative from `shaders/`, so hot reload is a matter of noticing that one
changed. `Vkm::GL::reloadChangedShaders`
(`modules/vkmGL/src/shader/gl_shader_reload.h`) takes the newest write time
under a directory and, when it moves, recompiles every live shader; a program
that no longer compiles keeps its previous one and logs the error.

The editor drives it, polling once a second (`EditorSystem::DISK_POLL_INTERVAL`)
and toasting what it reloaded. The same interval paces the other thing a build
rewrites under a running editor - `pollScriptRebuild` stats the gameplay module
- which is why the constant names the disk rather than the shaders. There is no
file-watcher system: a per-platform watcher is a dependency for something a
directory scan of a few dozen files already answers, and the runtime has no
shader sources to watch anyway.
