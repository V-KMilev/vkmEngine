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
| `engineVersion` | Engine version the project was authored against; logged on load |
| `entryScene` | Scene to boot, relative to the project root |
| `tickRate` | Simulation ticks per second; 64 by default, clamped to a sane range |
| `maxPlayers` | Seats the game has. A property of the game, not of a run: a scene with four characters authored into it is a four-player game wherever it is served |
| `netPort` | Port the game is served on unless a run says otherwise, so serving a project and joining it need no argument to agree |
| `splash` | Logos shown after the engine's own, in order. Usually empty |

`tickRate` is the project's rather than the engine's because it is not only a
simulation detail: for a networked game it is the rate the wire is clocked by,
and a competitive shooter and a turn-based game want different answers out of
the same build. Each host applies it where it learns its project - the runtime
once before the loop, the editor on every project open, since two projects
opened in one session are two cadences.

A missing or malformed `project.json` is **not** fatal - the defaults stand and
an unnamed project opens, which is what a fresh directory should do.

`saveProject` writes one back, for the editor's Project Settings window and for
New Project. It is a read-modify-write: the file is parsed, the fields the struct
describes are overwritten, and every other key is left exactly as it was, because
a `project.json` is hand-authored as often as it is written by a tool and a
writer that rebuilt the document would silently drop what it did not know about.
`splash` is the one field read but not written - `loadProject` skips an entry
naming no image, so writing the list back would delete it from the file.

Saving stamps `engineVersion` with the running engine. That field is provenance
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
[editor.md](../editor.md#opening-a-project)).

`userRoot()` is the newest of the three and exists because the first two can
both be read-only. An SDK installed to `/usr/local` or `Program Files` is; so is
a game installed there. Anything the engine *writes* that is not a project's own
content therefore goes here:

| File | Where | Why not the project |
|------|-------|---------------------|
| `editor_recents.json` | `userRoot()` | A list of the projects you have opened is how you get from one to the next; inside a project it could only ever list itself |
| `imgui.ini` | `userRoot()` | Window positions and column widths are one person's layout, not something a project hands the next person |
| `logs/<project>/log.log` (`cook.log` for `vkm_cook`) | `userLogs()`, **only** when the project cannot hold a `logs/` | A developer looks beside the project, so that stays the first choice |

The platform decides the actual directory, and configuration and state are
different places on both: `$XDG_CONFIG_HOME` (or `~/.config`) and
`$XDG_STATE_HOME` (or `~/.local/state`) on Linux, `%APPDATA%` and
`%LOCALAPPDATA%` on Windows, each under a `vkmEngine` folder. With no home
directory at all - a service account, a stripped container - or with one the
engine cannot create its folder inside, both fall back to the engine root, which
is where these files lived before this root existed. That fallback is why the
repository still ignores `imgui.ini` and `editor_recents.json` at its root.
`userRoot()` creates its directory when first asked for; `userLogs()` does not,
because `bootHost` creates the per-project subdirectory it writes into.

`editor_settings.json` is the deliberate exception: it stays in the project root,
because most of what it holds (panel widths for this project's layout, recent
scenes, the render tuning this project is authored against) is per-project. A
project you are authoring is writable by definition.

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

Every host opens a project by one rule, in `bootProjectScene`
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
a file the editor had just loaded was a scene with no file: Save asked for a
name, offered `scene.json` rather than the name it has, and writing it left the
project's `entryScene` untouched and the session saved where the project never
looks - with the title bar calling it *untitled* the whole time. The path is
stated by the function that opened the file rather than re-derived by each
caller, since re-deriving it means restating the rule this one exists to hold.

### What each host does when a project will not open

The exit code is the only answer a shell gets, so each host has to spend it on
what is actually fatal *for that host*. The split is not arbitrary: the runtime
plays a finished game, the editor is the tool you repair one with.

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
| Entry scene named but will not load (`SceneBoot::Failed`) | exit 1 | opens on the default scene; error toast and a Bottom > Errors entry | exit 1 |
| No entry scene and no `vkmBuildScene` (`SceneBoot::Default`) | exit 1 | opens on the default scene | exit 0 - nothing to cook |
| No entry scene, module builds the world (`SceneBoot::Project`) | plays it | opens it | exit 0 - nothing to cook |
| An asset fails to cook | n/a | reported, the session continues | exit 1 |
| Entry scene loads, but something in it does not (unresolved reference, prefab file missing) | plays it with what loaded | reported, the session continues | exit 1 |

The cooker reaches the last three rows by its own path rather than through
`bootProjectScene` - it has no `Scene` to boot into and no module to ask, so it
reads `entryScene` and loads that file directly.

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
  and, because `bootProjectScene` reports it through `reportError` rather than
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
| Asset serializer    | `src/engine/io/asset/asset_serializer.h`           | Save name-only asset references; on load resolve them via the asset library + the `AssetFactory` seam. |
| Asset library       | `src/engine/io/asset/asset_library.h`              | The cooked-asset database manifest: maps an asset name to its type + recipe hash, and derives its file locations. |
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

- `assets`: name-only references to every asset the scene names - `Mesh`,
  `LOD` levels, `Decal`, the rig plus clip an `Animator` names, the sound an
  `AudioSource` names, and every `AssetRef` field on a behavior - plus the
  textures those materials use. A reference the assets block never lists is one
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
where it is built (`detail::writeNonFiniteAsZero`, shared with `Prefab::save`):
a non-finite value is written as `0` and named in the log by its path, e.g.
`/entities/12/components/AudioSource/volume`. The play-mode snapshot goes through
the same builder *and* the same cook, so a scene that could not be saved cannot
fail to restore on Stop either - the second half is what makes that true, because
the snapshot names its assets exactly as the file does and a name is restorable
only once the library holds a record for it. Nothing about a well-formed file
changes - a finite number writes exactly as it did - and the read side stays
strict on purpose: teaching the loaders that `null` means "keep the default"
would make it a permanent token in every scalar field of the format.

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
list goes back with `LoadMode::Reload`, which rebuilds each asset into the slot
it already occupies (`ResourceManager::swapValue`: contents exchanged, identity
and name left with the slot, version bumped so the backend re-uploads). Both
halves are needed. Without the merge, a rebuilt graph restarts at the same
indices and generations, so a surviving step resolves to whatever landed in its
slot - measured, an undo of a mesh assignment silently restored a different
mesh. Without the reload, a material edited during the session would keep that
edit, because nothing would have put the old contents back.

Nothing is *removed* by a restore: an asset a session created stays, the way an
import made before Play does.

**Opening a scene answers this the other way, on purpose.** Stop promises to put
one session back; an open is leaving that world for another, and it drops the
undo stack, the selection and the material previews on the way through. So the strays go with the session that imported them - carrying them
would grow the graph by a scene's worth of assets per open and cook every one of
them into the library at the next save - and the editor names them in the log
and counts them into a toast rather than letting them vanish quietly. See
[the editor](../editor.md#what-an-open-does-to-the-sessions-imports).

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
   instead of unwinding out of it. **The abort names the record it was standing
   on**: `loadInto` catches, prefixes the component key and rethrows, and the
   entity loop keeps the id it is reading, so a single mistyped field reports
   `Aborted while reading entity 17 of 'scene.json': component 'UIButton': type
   must be string, but is number` rather than a file name and a JSON error. The
   thrower knows neither half - nlohmann names the type mismatch and nothing
   about where in the file it is - and without them a one-character drift costs
   a bisection of the file. An asset *name* step 2 did not bring in is
   not a drifted field and does not fail the load: the component's slot is
   left empty and the miss goes through `reportError`, so the editor toasts it
   and keeps it in Bottom > Errors rather than burying it in a log the editor
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
   font slot swaps *back* (`swapSlot<FontAsset>`): fonts are baked at startup
   and never enter a scene file, so the staging RM has none, and without that
   step every `UIText` loses its font on load.

### A reference that did not resolve is kept, not erased

An asset name the load cannot answer leaves the component's slot empty, and an
empty slot is indistinguishable from one nobody ever filled - so the save wrote
`""` over the name and dropped the entry from the `assets` block, and reported
it as an ordinary successful save. Opening a scene whose gitignored `library/`
a teammate never committed and pressing Ctrl+S out of habit was enough to lose
every reference in it, permanently.

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

**The record retires when the field is filled.** The other way an author can
answer it is to pick something else, and nothing was retiring the record when
they did: the field write already left the chosen mesh alone, but the banner went
on saying the reference did not load and promising to put a name back that the
save had stopped writing, and the assets block went on declaring it - so the
saved scene named an asset that is not there and every later load reported it.
`SceneSerializer::pruneResolvedRefs` drops the entries whose field is no longer
empty, and the component with the last of them. It answers by writing the entity
and reading the field back, which is the same question the save asks and so
cannot drift from it, and the Inspector calls it where the banner is drawn -
a field is filled from the picker, from the Asset Browser or by an undo, and
none of those is a place the record could be retired from once and for all.

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
  decoded texture pixels; a rig's bones and bind data; a clip's keys and
  markers; a sound's PCM).
  Regenerable; git-ignored.
- `library/_manifest.json` - maps each asset `name` to its type and recipe hash,
  under a `manifestVersion` the loader checks. `AssetLibrary`
  is the in-memory view, loaded at startup; a manifest in a version this build
  does not know is refused rather than half-read, because the whole library is
  derived data and re-cooking costs less than guessing. The two filenames above
  are not recorded: `AssetLibrary::recipePath()` / `cookedPath()` derive them from
  (type, name), so the cooker that writes a file and the loader that reads it
  cannot disagree about where it is.

An imported asset is **named by its project-relative path**, and so is the
`path` in its recipe - `ProjectPaths::toProjectRelative` at the loader boundary,
`resolveProjectPath` to open it again. That matters more than it looks: the uid
above is a hash of `"<Type>:<name>"`, so an absolute name would make the entire
library's on-disk layout a function of one machine's home directory, and a
second checkout would produce a manifest whose every entry hashes to a filename
nothing on disk answers to. A source outside the project keeps its absolute path
- it has no relative form - and the conversion happens before the by-name dedup,
or one file reached by two spellings becomes two assets.

**Save** - `AssetSerializer::saveAssetsForScene` walks the components that name
assets (`Mesh`, `LOD`, `Decal`, `Animator`, `AudioSource`, plus the asset fields a
behavior declares) and emits **name-only** references to the meshes / materials /
textures / skeletons / animation clips / sounds used. In the editor,
`SceneIOController` first calls `AssetCooker::cookAllAssets`, which bakes every
non-hidden asset in the `ResourceManager` into the library + cooked cache and
rewrites the manifest (skipping assets whose hash is unchanged and whose cooked
file this build can still read). It waits for anything still importing first,
through `awaitAsyncLoads`: an asset that has not landed yet has no vertices to
bake, and `vkm_cook` has no frame loop to land it. It returns false when any
asset failed to cook, which is what `vkm_cook`'s exit code carries.

`finalizeAsyncLoads` / `awaitAsyncLoads` (`system/async/async_loader_system.h`)
are that finalisation without a frame - drain once, and drain until quiet.
`AsyncLoaderSystem` is the per-frame caller of the first. The two callers of the
second are the cooker and the `decimate` mesh recipe, which would otherwise
cluster a base mesh that has not arrived and silently produce no LOD level at
all. Both run where there is no next frame to wait for.

**Load** - `AssetSerializer::loadAssets` resolves each name through the manifest,
**cache first and recipe on a miss**. `resolveCookedSource` probes the cooked
file with `AssetCook::isCookedCurrent`; when it answers yes the asset gets a
synthesized `{"kind":"cooked","name":...}` source, and when it answers no the
asset gets the `source` object out of its library recipe instead - the same
`model` / `generator` / `file` descriptor the import wrote. A material skips the
probe: it has no cooked binary, so its recipe is always what loads. All of them
go through the `AssetFactory` dispatch seam (`io/asset/asset_factory.h`) - six function pointers
(mesh / texture / material / skeleton / animation clip / audio clip) that each
binary wires at startup, with plain switch dispatch on the `kind` field:

| `kind`                 | Handled by       | Resolves to                                         |
|------------------------|------------------|-----------------------------------------------------|
| `cooked`               | runtime + editor | A mesh/texture read from its cooked binary (async), or a skeleton/clip read from its own (synchronously) |
| `inline`               | runtime + editor | A `MaterialAsset` from PBR scalars + texture refs   |
| `generator` / `decimate` | editor only    | Procedural / LOD meshes (run by the cooker)         |
| `file` / `model` / `model-image` | editor only | stb / Assimp texture, mesh, rig and clip import (a clip recipe also carries its authored `markers`) |
| `folder` / `model` / `default` / `builtin` / `solid` | editor only | material + texture recipes |

The runtime wires only the cooked dispatch (`registerCookedAssetFactories` sets
the pointers to `createCookedMesh/Texture/Material/Skeleton/AnimationClip`),
so it links neither Assimp
nor the image decoders. The editor instead wires the recipe dispatch
(`registerRecipeAssetFactories`, built into the editor-only `vkm_cook`),
whose switches fall through to the cooked functions for cooked/inline kinds.
Engine code never reaches into `src/tools/`; the dispatch is wired at startup in
`src/tools/asset_registration.cpp` (cooked) and
`src/tools/cook/recipe_registration.cpp` (recipe).

Adding a new asset kind means adding a `case` to the dispatch switch; the
serializer itself does not change.

### The cooked bodies

Every `.vkmc` is the same header - magic, endian sentinel, asset kind, format
version, recipe hash, payload length - followed by a body whose layout the
format version names. `AssetType`, `TYPE_DIRS` and the kind tag move together,
guarded by the `static_assert` in `asset_library.cpp`.

| Body | Holds |
|------|-------|
| Mesh | Bounds, the four counts, the skin radius, then bulk vertices, indices and skin, then the rig name |
| Texture | The `TextureParams` fields, then the decoded pixels |
| Skeleton | Bone count, a `{parent, nameLen}` record per bone, bulk inverse-bind matrices, bulk bind-pose TRS, then the concatenated names |
| Animation clip | Bone count, duration, the six key-array counts, the skeleton name length, the marker count and marker-name length, then the bulk `ClipBone` table, the six key arrays, the rig name, a `{time, nameLen}` record per marker and the concatenated marker names |
| Audio clip | Sample rate, channel count, sample count, then the interleaved 16-bit PCM |

Fixed-size records come first and variable-length names last in both new
bodies, so the size reconciliation works the same way `readMesh` does: bound
every count by **division** against what is left of the payload before
multiplying it by anything, then require the remainder to land on exactly zero.

Past the size math, each reader checks what a correctly-sized file can still
get wrong, because nothing downstream re-checks:

- A skeleton's bones must be **parent-before-child** (`-1 <= parent < index`).
  Rejecting a later or self-referencing parent here is what lets every consumer
  compose a pose in one forward loop.
- A clip's key times and values must pair up, its duration must be finite, and
  every channel range must land inside the array it addresses - the sampler
  indexes those arrays directly, once per bone per frame.
- Every clip marker's time must be finite and inside `[0, duration]`. Where a
  marker fires is the whole of what it says, and a time outside the timeline
  never arrives at the instant it names - a looping head wraps it to some other
  moment and a clamped one never reaches it at all.
- A bone count past `MAX_SKELETON_BONES` is refused. It is a corruption
  threshold rather than a capability limit: raising it later accepts strictly
  more files, so it starts tight. `MAX_AUDIO_CHANNELS` and
  `MAX_AUDIO_SAMPLE_RATE` are the same kind of threshold for a sound.
- A sound's sample count must divide by its channel count. The mixer reads
  whole frames, so a file that carries the right number of bytes and still
  describes a half frame would run it off the end.
- Every index in a mesh must name a vertex the file declares. A truncated write
  resumed or a bad sector produces a correctly-sized file that does not, and
  nothing downstream re-checks: decimation indexes a per-vertex array with them
  and GL is handed the buffer as-is.
- A mesh's skin stream must be parallel to its vertices or absent, and every
  bone index in it must be under `MAX_SKELETON_BONES`. That second check earns
  its keep for a sharper reason than the index check beside it: a bone index is
  never read by the CPU at all, it addresses the pose palette in the vertex
  stage, so a corrupt one is an out-of-range buffer read on every vertex of
  every frame and nothing else would notice.
- A texture's declared width and height must describe exactly the pixel bytes
  beside them. `TextureParams` reaches `glTexImage2D` verbatim, which then reads
  `width * height` texels out of that buffer, so a size that merely *fits* is
  not enough. The reconciliation divides rather than multiplies, so the math
  cannot wrap.

Skeletons and clips are read **synchronously** (`loadCookedSkeleton` /
`loadCookedAnimationClip`). A rig is a few tens of kilobytes, well under what
earns a completion type, an `AsyncLoadQueue` lane and a drain in
`AsyncLoaderSystem`. The component that names them is `Animator`, so a scene's
assets block carries a `skeletons` and a `clips` section beside the other three;
they load after materials and before meshes, because a clip names the rig its
bone indices address.

Sounds are synchronous too (`loadCookedAudioClip`), for a different reason: a
cooked sound is bigger, but there is nothing to decode - the file already holds
the PCM the mixer wants, so a worker hop would buy a copy's worth of latency at
the price of a completion lane and a window in which a scene's sounds exist and
are silent. Their `sounds` section loads last, because nothing depends on it and
it depends on nothing.

`MESH_FORMAT_VERSION` is **2**: the mesh body carries the skin stream, the skin
radius and the rig name. Every mesh cooked before it is refused on read - a
cooked file is a derived cache and there are no migration read paths in this
project, by policy. `COOKER_VERSION` moves with it, because `POST_PROCESS_FLAGS`
changed and every stored recipe hash has to go stale at once.

### What a format bump costs, and why the recipe is reachable

**A format bump costs a re-cook, not a re-import.** That is the whole point of
keeping the recipe: refusing an old file is only affordable because something
can still produce a new one. Run `vkm_cook` (or open the project and save) and
the library rebuilds the binaries the manifest promises.

The mechanism is one probe, used from both ends so the two cannot disagree:

```
isCookedCurrent(type, path, recipeHash)   // header only: 28 bytes, no body
```

- **The loader** asks it before resolving a name (`resolveCookedSource`). A file
  that is absent, foreign, of another kind, of a format version this build does
  not read, baked from another recipe, or shorter or longer than the payload its
  own header declares is not current, and the recipe loads instead. That last
  case is what an interrupted write leaves behind, since the header goes to disk
  before the body it describes: without it the reader would refuse a file the
  cooker calls current and never rewrites. The fallback is what makes `cooked/`
  genuinely regenerable rather than regenerable on paper.
- **The cooker** asks it before skipping an asset (`isUpToDate`). Presence is not
  enough: a file this build cannot read is not an output that can be skipped.

Both ends have to move together. If only the loader fell back, a bump that left
recipe hashes untouched would have the cooker call the stale binary current and
never rewrite it, so every load would re-import from source art forever. If only
the cooker rewrote, a stale project would still fail to load until someone
thought to run the cooker.

The probe is deliberately silent - a stale cache is normal and recoverable, so it
logs nothing and the caller reports what the miss meant. In a host that links no
importers (the runtime) the fallback still happens, the dispatch refuses the
recipe kind it gets, and the pair of log lines names the asset and the reason: a
shipped build cannot rebuild a cache, it needs one cooked for it.

Which is why **a packaged game carries no recipes but its materials.** `vkm
package` ships `_manifest.json` and `library/materials/` and leaves the other
four kinds behind: their recipes describe an import the runtime has no code to
perform, so a stale cache is equally unrecoverable with or without them - the
error just reads "recipe missing" instead of "no dispatch for kind". Both mean
re-cook and re-package. A material is the exception because its recipe is not an
import record at all: it *is* the runtime form, read straight back by
`loadLibrarySource`.

The one thing this does not recover is a recipe whose **source art is gone**. The
cook then has a recipe and nothing to bake from, which is an error rather than a
skip: it fails the cook and `vkm_cook` exits non-zero, because the manifest is
about to promise a file nothing produced. An asset that never had a recipe at all
is a different case and still only a warning - a project is free to build meshes
in code and name them, and both examples do.

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

That symmetry is load-bearing rather than tidy. The same raw number means three
different things depending on which carrier it came from - a live slot in this
`Scene`, a slot saved in a file, a one-shifted index into a prefab's entity
list - and a field like `Joint::connected` cannot say which. Taking the carrier
as an argument makes the number impossible to read without naming the namespace
it is in, and `EntityId` being its own type makes writing one back impossible.

Today's coverage, the flat list in `scene_serializer.cpp`:

- `Name`, `Transform`, `Camera`, `Light`, `Animation`
- `Mesh`, `LOD`, `Decal` - the ones that name assets, so their save/load also
  takes the `ResourceManager` that turns a handle into a name and back.
- `ParticleEmitter`, `IrradianceVolume`, `ReflectionProbe`
- `AudioSource` (asset-naming, so it takes the `ResourceManager` too) and
  `AudioListener`. `AudioSource::playing` and `started` are runtime state and
  are deliberately absent: they describe a play session, and a scene row holding
  a half-finished sound would resume a noise whose beginning nobody heard (see
  [Audio](audio.md))
- `UICanvas`, `UIElement`, `UIImage`, `UIText`, `UIButton` (see [UI](ui.md))
- `Rigidbody`, `Collider`, `CharacterController` (physics; runtime sleep state,
  the contact-normal outputs and derived mass properties are not persisted, and a
  controller writes only its four tuning fields - see [Physics](physics.md)). A
  collider part
  writes its `shape` by name alongside every shape's fields, so switching a part
  to a capsule and back does not lose the half-extents it was authored with; a
  part with no `shape` key - every part in a scene written before capsules
  existed - reads as a box.
- `ScriptComponent` (JSON key `"Script"`): each behavior stored by its registered
  type name and recreated through `BehaviorRegistry` on load - a type the
  registry does not know is kept verbatim as an `UnknownBehavior` and written
  back out unread, because an unregistered type says the module is missing and
  not that the author wants the behavior gone. Authored fields sit in a
  `properties` object beside it -
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
   and the `assets` block `saveAssetsForEntities` builds all expand from that
   one list.

Those were four hand-kept lists, and the failure was silent in both directions:
a key that was saved and registered but never loaded round-tripped to nothing,
while the drift warning that exists to catch it stayed quiet because the key was
still known; and a component whose assets the hand-written walk forgot saved its
handle as a name that the `assets` block never listed, so the next load resolved
it to nothing - which is how every decal material and every LOD level above the
first were lost for a time. An `R` row with no `emitAssetRefs` overload now
fails to compile at the walk, naming the component that needs one.

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
single-sourced by `VKM_REFLECT_BEGIN` / `VKM_F`. These keys were the odd layer
out.

`loadInto` overwrites a component the entity already carries instead of adding a
second one. That is not a nicety: a prefab instance root is loaded twice, once
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
already loaded it, so without the block a prefab only built correctly where a
scene happened to have loaded the same assets first - dragging one into a scene
that never held its mesh produced entities that draw nothing. Every path that
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
though the prefab now lists them: an instance may override a `Mesh` or a `Decal`
at an asset the prefab file never names, and only the scene's walk sees that.

A scene stores an instance as a `PrefabInstance` (the source path) plus the
root's `Transform` and `Hierarchy` - where it sits and what it hangs off belong
to the scene - and the saver skips the whole subtree beneath it. The loader
expands it after the entity pass, so the roots keep their saved slots and the
prefab's own entities take whatever is free. Editing the prefab therefore
changes every instance the next time a scene loads, which is the point.

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
(`src/editor/framework/prefab_overrides.h`), each card marks the fields the
instance owns, and `Prefab::reloadComponent` gives one back to the prefab.

## Shader hot reload

Shaders are not assets and not part of the library - they are source files read
CWD-relative from `shaders/`, so hot reload is a matter of noticing that one
changed. `Vkm::GL::reloadChangedShaders`
(`modules/vkmGL/src/shader/gl_shader_reload.h`) takes the newest write time
under a directory and, when it moves, recompiles every live shader; a program
that no longer compiles keeps its previous one and logs the error.

The editor drives it, polling once a second (`EditorSystem::SHADER_POLL_INTERVAL`)
and toasting what it reloaded. There is no file-watcher system: a per-platform
watcher is a dependency for something a directory scan of a few dozen files
already answers, and the runtime has no shader sources to watch anyway.
