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
| `projectRoot()` | The game being made: its content, its code, its asset database | `assets()`, `scenes()`, `prefabs()`, `envs()`, `screenshots()`, `library()`, `cooked()`, `projectBin()` |
| `userRoot()` | How one person likes their tools, across every project and every engine install | `userLogs()` |

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
| `logs/<project>/<host>.log` | `userLogs()`, **only** when the project cannot hold a `logs/` | A developer looks beside the project, so that stays the first choice |

The platform decides the actual directory, and configuration and state are
different places on both: `$XDG_CONFIG_HOME` (or `~/.config`) and
`$XDG_STATE_HOME` (or `~/.local/state`) on Linux, `%APPDATA%` and
`%LOCALAPPDATA%` on Windows, each under a `vkmEngine` folder. With no home
directory at all - a service account, a stripped container - both fall back to
the engine root, which is where these files lived before this root existed.
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

All three hosts open a project by one rule, in `bootProjectScene`
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

### What each host does when a project will not open

The exit code is the only answer a shell gets, so each host has to spend it on
what is actually fatal *for that host*. The split is not arbitrary: the runtime
plays a finished game, the editor is the tool you repair one with.

| Condition | `vkm_runtime` | `vkm_editor` | `vkm_cook` |
|-----------|---------------|--------------|------------|
| Log file cannot be opened | exit 1 | exit 1 | exit 1 |
| Window / GL context cannot be created | exit 1 (throws) | exit 1 (throws) | n/a - headless |
| No gameplay module in the project's `bin/` | exit 1 | opens, logs INFO | n/a |
| Module present but will not load (version, entry, unreadable) | exit 1 | opens, logs WARNING | n/a |
| Entry scene named but will not load (`SceneBoot::Failed`) | exit 1 | opens on the default scene, error toast | exit 1 |
| No entry scene and no `vkmBuildScene` (`SceneBoot::Default`) | exit 1 | opens on the default scene | exit 0 - nothing to cook |
| No entry scene, module builds the world (`SceneBoot::Project`) | plays it | opens it | exit 0 - nothing to cook |
| An asset fails to cook | n/a | reported, the session continues | exit 1 |

The cooker reaches the last three rows by its own path rather than through
`bootProjectScene` - it has no `Scene` to boot into and no module to ask, so it
reads `entryScene` and loads that file directly.

Two judgments behind that table:

- **A game is its module.** Behaviors are created through the registry the module
  fills, and `ComponentSerializer` drops a behavior whose type nothing registered
  without a word - so a runtime that boots without the module plays a world that
  draws and does nothing, and reports success for it. The editor warns instead,
  because a module that will not load is fixed by rebuilding it and reloading,
  and you need the editor open to do that.
- **A broken entry scene is not fatal to the editor**, which is the thing you
  open a broken scene in. The default scene stands in carrying no save path, so
  a save cannot overwrite the file that failed to load; the reason is in the log
  and, when the failure came from File > Open Project, in an error toast.

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

- `assets`: name-only references to every asset a component names - `Mesh`,
  `LOD` levels, `Decal`, and the rig plus clip an `Animator` names - plus the
  textures those materials use. A
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
  decoded texture pixels; a rig's bones and bind data; a clip's keys).
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
assets (`Mesh`, `LOD`, `Decal`) and emits **name-only** references to the
meshes/materials/textures used. In the editor,
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
go through the `AssetFactory` dispatch seam (`io/asset/asset_factory.h`) - five function pointers
(mesh / texture / material / skeleton / animation clip) that each binary wires at
startup, with plain switch dispatch on the `kind` field:

| `kind`                 | Handled by       | Resolves to                                         |
|------------------------|------------------|-----------------------------------------------------|
| `cooked`               | runtime + editor | A mesh/texture read from its cooked binary (async), or a skeleton/clip read from its own (synchronously) |
| `inline`               | runtime + editor | A `MaterialAsset` from PBR scalars + texture refs   |
| `generator` / `decimate` | editor only    | Procedural / LOD meshes (run by the cooker)         |
| `file` / `model` / `model-image` | editor only | stb / Assimp texture, mesh, rig and clip import |
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
| Animation clip | Bone count, duration, the six key-array counts, the skeleton name length, then the bulk `ClipBone` table, the six key arrays and the name |

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
- A bone count past `MAX_SKELETON_BONES` is refused. It is a corruption
  threshold rather than a capability limit: raising it later accepts strictly
  more files, so it starts tight.
- A mesh's skin stream must be parallel to its vertices or absent, and every
  bone index in it must be under `MAX_SKELETON_BONES`. That second check earns
  its keep for a sharper reason than the index check beside it: a bone index is
  never read by the CPU at all, it addresses the pose palette in the vertex
  stage, so a corrupt one is an out-of-range buffer read on every vertex of
  every frame and nothing else would notice.

Skeletons and clips are read **synchronously** (`loadCookedSkeleton` /
`loadCookedAnimationClip`). A rig is a few tens of kilobytes, well under what
earns a completion type, an `AsyncLoadQueue` lane and a drain in
`AsyncLoaderSystem`. The component that names them is `Animator`, so a scene's
assets block carries a `skeletons` and a `clips` section beside the other three;
they load after materials and before meshes, because a clip names the rig its
bone indices address.

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
  not read, or baked from another recipe is not current, and the recipe loads
  instead. That fallback is what makes `cooked/` genuinely regenerable rather
  than regenerable on paper.
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

Today's coverage, the flat list in `scene_serializer.cpp`:

- `Name`, `Transform`, `Camera`, `Light`, `Animation`
- `Mesh`, `LOD`, `Decal` - the ones that name assets, so their save/load also
  takes the `ResourceManager` that turns a handle into a name and back.
- `ParticleEmitter`, `IrradianceVolume`, `ReflectionProbe`
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

Two localised edits, no registry table, no virtual dispatch:

1. A `save` / `load` overload pair in `component_serializer.h` (+ `.cpp`). If the
   component has nothing but plain reflected fields, both bodies are one call to
   `saveReflected` / `loadReflected`.
2. A row in `VKM_SCENE_COMPONENTS` in `scene_serializer.cpp` - `P(Type, "Key")`,
   or `R(Type, "Key")` when the component references assets and its save/load
   take the `ResourceManager`. Saving, loading and the known-key set behind the
   "unknown component key" drift warning all expand from that one list.

Those were three hand-kept lists, and the failure was silent: a key that was
saved and registered but never loaded round-tripped to nothing, while the drift
warning that exists to catch it stayed quiet, because the key was still known.
The key is spelled out in the row rather than derived from the type name, since
it is the format - `ScriptComponent` is stored as `"Script"`. `Hierarchy` is not
a row: it is written explicitly and read by the loader's second pass.

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
            "skeletons": [...], "clips": [...]}}
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
