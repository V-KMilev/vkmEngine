# Editor

`vkm_editor` edits one project: its scenes, prefabs and assets, with a play
session to try them. It is an ImGui shell registered as a `System` on
`SystemStage::Editor`, owning the panels, the layout, the undo stack and scene
I/O. Panels mutate through an `EditorContext` aggregate, never through each
other.

## Layout

```
+---------------------------------------------------------------+
|                          Menu bar                             |
+-----------+---------------------------------------+-----------+
| Hierarchy |                Viewport               | Inspector |
|           |                                       | Material  |
|           | [T]      [Playback bar]   [View bar]  |           |
|           | [o]                                   |           |
|           | [o]                                   |           |
|           | [l]                       [Nav axes]  |           |
|           +---------------------------------------+           |
|           |  Assets | Animation | Errors | Build  |           |
+-----------+---------------------------------------+-----------+
|                          Status bar                           |
+---------------------------------------------------------------+
```

The viewport's overlays - the **tool strip** on the left, the **view bar**
top-right, the **playbar** between them and the navigation axes bottom-right -
draw in a child that is exactly the scene's rect; the three strips share one
design (`beginOverlayStrip`, `ui/editor_widgets.h`).

### The panels dock, and the layout is ImGui's

The editor runs on Dear ImGui's docking branch: the root window holds the menu
bar, one dockspace and the status bar, and every panel is a window that docks,
tabs or floats. Their names are in `chrome/dock_layout.h`, because a name is
what the ini files a window's place under. Where they are and how big is
ImGui's; nothing of the editor's stores a panel size, and the default layout's
are constants in `chrome/dock_layout.h`. Multi-viewport is off.

- **Default layout** - the one above, cut by `buildDefaultLayout`
  (`chrome/dock_layout.cpp`) when the ini holds no dockspace and on **Window >
  Reset Layout**, which also shows every panel.
- **What is shown is the editor's.** Window > Hierarchy, Inspector and Assets
  (Ctrl+1 to Ctrl+3) show and hide the groups the default layout docks together:
  the Hierarchy; the Inspector and Material editor; Assets, Animation, Errors and
  Build. Persisted per project in `editor_settings.json`. Windows have no close
  box, so a toggle is the one way to hide one and to get it back.
- **Where it is, is per person**: `io.IniFilename` is `imgui.ini` in
  `ProjectPaths::userRoot()`, so the layout follows the person across projects
  ([io.md](io.md#which-root-owns-a-path)). Popups, dialogs and the root window
  are `NoSavedSettings`.

## Panels

| Panel               | File                                  | Description                                                                 |
|---------------------|---------------------------------------|-----------------------------------------------------------------------------|
| Hierarchy           | `panels/hierarchy_panel.cpp`          | Every entity; drag a node onto another to reparent (cycle-safe) |
| Inspector           | `panels/inspector_panel.cpp`          | Component cards; prefab-instance overrides |
| Animation           | `panels/animation_panel.cpp`          | Transport, three-lane keyframe timeline, a table per track |
| Render Settings     | `panels/render_settings_panel.cpp`    | Floating window over `RenderSettings` (Window > Render Settings) |
| Material Editor     | `panels/material_editor_panel.cpp`    | The **Material** window: one material under a live preview |
| Asset Browser       | `panels/asset_browser_panel.cpp`      | The **Assets** window: one library for every asset kind |
| Project Settings    | `panels/project_settings_panel.cpp`   | Floating window over `project.json` (File > Settings...) |
| Preferences         | `panels/preferences_panel.cpp`        | Floating editor settings window (Edit > Preferences, Ctrl+,) |
| Errors              | `panels/errors_panel.cpp`             | Recoverable engine failures (`EngineErrorLog`), newest first |
| Build               | `session/build_controller.cpp`        | The vkm command running or last run, and its output |
| Viewport Overlay    | `overlays/viewport_overlay.cpp`       | The axis navigation gizmo |
| Gizmo Overlay       | `overlays/gizmo_overlay.cpp`          | The transform gizmo's drawing and drag, and click-to-pick |
| Gizmo Drawing       | `overlays/gizmo_overlay_draw.cpp`     | Every `draw*Gizmos` body, plus the selection outline |
| Viewport Toolbar    | `overlays/viewport_toolbar.cpp`       | Tool strip (tool, space, snap) and view bar (shading, camera, projection, Frame All, Focus) |
| Playback Bar        | `overlays/playback_bar.cpp`           | Play / Pause / Step / Stop; frames and captions the viewport during a session |
| Start Screen        | `panels/start_screen.cpp`             | Shown when no project is open |

The **chrome** belongs to `EditorSystem` and sits around the layout:

| Chrome              | File                                  | Description                                                                 |
|---------------------|---------------------------------------|-----------------------------------------------------------------------------|
| Menu Bar            | `chrome/editor_menu_bar.cpp`          | File / Edit / View / Window / Entity / Help; reads and writes `EditorState`, forwards scene-file intents to `SceneIOController` |
| Status Bar          | `chrome/editor_status_bar.cpp`        | Dirty dot, the selection's breadcrumb and position, the build banner |
| Dock Layout         | `chrome/dock_layout.cpp`              | Window names and the default layout |

Preferences, Render Settings and Project Settings open through
`beginToolWindow` (`ui/editor_dialogs.h`), which never docks.

### Where world settings live

Scene-global settings are cards in the **World inspector** (the **World** row at
the top of the hierarchy): `Environment`, `Procedural Sky`, `Volumetric Fog` and
`Physics` (`Scene::physics()`). They are scene data. Render Settings, quality
tuning rather than content, has its own window.

### Preferences

`Camera`, `Gizmo` (snap defaults), `Display` and `Keybinds` tabs over one
`Preferences` struct on `EditorState`, saved in `editor_user.json` under
`userRoot()` (per person, not per project) and applied every frame by
`EditorSystem::applyPreferences`. Vsync, the
frame cap and the window mode stand aside during a play session, where the game
may set the window up its own way.

**UI Scale** (Display) multiplies the display's own content scale; everything
the editor draws itself follows it through `EditorStyle::px()`.

### The Material editor

One way in: `EditorState::openMaterial(handle)`, used by the Mesh card and the
Asset Browser's tile and New verb. Shown and hidden with the Inspector (Ctrl+2).

- **Base and Maps are always there**; each of the five secondary lobes is a card
  only while the material uses it. `FEATURES` in the panel says per lobe how to
  tell it is on, switch it on, and reset its fields; `+ Add Feature` offers the
  ones that are off.
- **A row that cannot do anything is not drawn** - Cutoff only for AlphaMask,
  emission Strength only once the emission is not black.
- **Which material it edits.** The tab follows the selection. A material chosen
  by hand - the chooser, an Asset Browser tile, New or Duplicate - is pinned in
  `EditorState::materialEditorTarget` and outranks the selection until another
  entity with a material of its own is picked.

### Animation editor

Works on the selected entity. Play/Pause is a *preview* writing the runtime
`Animation::playing`; what a shipped scene does is the card's **Play On Start**.
**Length** (`Animation::length`) `0` means the last keyframe. Re-keying at an
existing time replaces that keyframe.

The world does not step in Edit mode, so moving the playhead poses the entity
through `AnimationSystem::applyAnimation`, the function the system samples with.
Outside play that pose is the authored `Transform`, so it is recorded as one
**Scrub Animation** step per gesture; in play it is not recorded.

### Inspector cards

A component card is an `editComponentCard` section, so it undoes, records prefab
overrides and appears in Add Component. Controls over session state push no
edit:

- **Animator** - loop, speed and **Play On Start** push an edit; play, stop and
  the scrubber do not, which is safe only because `Animator::playing` and
  `Animator::time` are session state, never serialized.
- **Bone Socket** - the Offset is authored; the entity's `Transform` is the
  socket's output, rewritten from the bone every frame.
- **Audio Source** - `auditionTransport` plays the clip **through the device, not
  `AudioSource::playing`**, whose write would be a scene edit
  ([audio.md](audio.md#the-cursor-is-the-devices-not-the-components)).

**The identity header** shows the entity's wire slot and which components
replicate, or warns that it is off the wire and why (`netSilence`)
([networking.md](networking.md#identity-is-the-servers-slot)). Drawn only when
the project replicates something and the entity carries some of it.

**A card greys what the scene, not the author, writes.** With **Procedural Sky**
on, `SkySystem` drives the key light's rotation, colour and intensity, so that
Light card says so and disables colour and intensity. Both find the light with
`findKeyLight`, so they cannot disagree.

### A card names what its component is waiting for

Where the engine can tell a component will do nothing, its card says so in
place - `EditorStyle::DANGER` for "does nothing", `EditorStyle::WARNING` for "is
not what you think". For example: a `Collider` with no `Rigidbody` (physics
walks the `Rigidbody` storage, so nothing collides with it), a dynamic
`Rigidbody` with no `Collider`, a UI element with no `UICanvas` above it or no
`UIElement` beside it (`warnNoUIElement`), an active camera that is not the
game's eye. The checks live with their cards in `panels/inspector_panel.cpp`;
add one wherever a component can sit silently inert.

### Widgets

Every slider is `sliderFloat` / `sliderInt` and every combo `beginCombo` or
`comboList` (`ui/editor_widgets.h`); nothing calls ImGui's directly, so a look
changed there changes everywhere.

`propDrag` / `propSlider` / `propDragInt` / `propDrag3` pass
`ImGuiSliderFlags_ClampOnInput` (`PROP_CLAMP`), because Ctrl+click turns a drag
into an unbounded text field. Not `AlwaysClamp`: that also clamps a
`lo == hi == 0` range, which is how a row spells "unbounded".

`pickAsset` offers a **(none)** row, because an empty slot round-trips through
the scene file.

## The viewport

### Transform gizmo

Translate, rotate and scale handles, axis-constrained; **Select** mode draws none
so a click always selects. `transform_gizmo.cpp` holds the whole gizmo. A drag
pushes one step when it ends: `editStep<Transform>`, or one `CompositeCommand`
when the selection has several roots. Picking and drags cast `viewportRay` from
the near plane, so orthographic views pick too.

**Snapping** (the strip's toggle, or Ctrl held) moves in whole steps *from where
the drag began*, along the handle held (`overlays/gizmo_snap.h`); an off-step
value keeps its offset.

Keys, while the cursor is not captured: `Q` Select, `W` Move, `E` Rotate, `R`
Scale, `X` Local / World - rebindable in Preferences > Keybinds.

Everything with no mesh draws an authoring marker (`gizmo_overlay_draw.cpp`):
lights, cameras, probes and irradiance volumes, decals, particle emitters, audio
sources and listeners. **View > Show** toggles each kind (`GizmoVisibility`, per
project in `editor_settings.json`); a hidden kind cannot be picked.

### Entity selection and shortcuts

- A viewport click tests the visible set's world boxes, then each hit mesh's
  triangles (`overlays/mesh_pick.h`); a skinned mesh, whose stored triangles are
  in bind pose, is picked by its box. A meshless entity is picked by its marker:
  `GizmoOverlay::markEntity` draws every marker, and the picker tests exactly
  those drawn that frame.
- Probes, irradiance volumes, decals and particle emitters pick by their marker
  only, never by their box, which would swallow every click inside it.
- The selection is an ordered list (`EditorState::selection`); the active
  entity is `EditorState::selectedEntity`, always in it, and
  `EditorState::selectedAt` indexes it by slot. Only `EditorState`'s selection
  helpers write them.
- The inspector shows the active entity. Inside a prefab instance an edit is a
  per-instance override with "Revert to prefab" (`command/prefab_overrides.h`).
- Every shortcut is a keybind in `input/editor_keybinds.h`, dispatched by
  `EditorShortcuts::process`.

### What the viewport shows

The editor looks through a viewpoint of its own (`EditorViewpoint`: position,
yaw, pitch, projection), not an entity: flying marks nothing unsaved, a scene
needs no camera to be edited, and nothing of the editor's writes the game's
camera. The view bar's camera box picks what the frame renders through:

| Mode | The frame renders through | Flies |
|---|---|---|
| Edit, **Editor** (the default) | the editor's viewpoint | yes |
| Edit, a scene camera from the box | that camera, active or not: a read-only preview, `(main)` on the one the game starts on | no |
| Play | the game's camera; the box reads **Game** | no |
| Play, ejected (F8) | the editor's viewpoint, or a previewed camera | unless a camera is previewed |

A previewed or game camera draws no frustum or marker and takes no gizmo
(`Visibility::cameraEntity` names it), since dragging the view you look through
would move it. **Set as Main Camera** in the inspector picks the game's eye,
which is a separate thing.

The viewpoint is kept per scene in `editor_settings.json` under `sceneViews`
(`EditorState::sceneViews`), by path relative to the project. A scene with no
entry is framed from its active camera, or from `(0, 2, 6)` facing the origin.

**Fly controls:**

| Input                          | Action                            |
|--------------------------------|-----------------------------------|
| Right mouse button (hold)      | Look mode (cursor hidden)         |
| Mouse movement (in look mode)  | Yaw / pitch                       |
| W / A / S / D                  | Forward / left / back / right     |
| Q / E                          | Up / down                         |
| Shift (hold)                   | Speed boost                       |
| Scroll wheel (in look mode)    | Dolly; orthographic, zoom         |
| Numpad 5                       | Perspective / orthographic        |

Speeds and sensitivities (`CameraControllerSystem::Settings`) are the
Preferences Camera tab (`Preferences::camera`).

**Orthographic** (view-bar button, View > Orthographic, Numpad 5) keeps the
focus - what Frame Selected, Frame All or a navigation axis last aimed at - the
same size (`EditorViewpoint::orthoHeight`). A navigation axis snaps to an
orthographic view down that axis (`viewAlongAxis`); turning the view returns to
perspective unless the projection was switched by hand.

### When the scene has no camera

It opens, renders and edits like any other; a play session has nothing to render
from, and `ViewportOverlay::drawNoCameraNotice` says so. `Entity > Create >
Camera` makes the new camera active only when the scene has no active one, so a
new camera never takes the game's view.

### Who has the pointer and the keyboard

Decided once a frame, before the ImGui frame opens, from the hovers the last
frame drew (`InputOwnership`, `input/input_ownership.h`, as
`EditorContext::input`); the shortcuts, gizmo, picker, navigation axes and the
capture declared on `HostChrome` all take that one answer. The first row that
holds wins:

| When | Owner | What answers a press |
|---|---|---|
| The cursor is hidden and grabbed, by the fly camera or a running game | `Captured` | Nothing of the editor's: ImGui is told it has no mouse |
| A gizmo drag is held | `GizmoHandle` | The drag, wherever it has gone |
| The pointer is off the viewport | `Panel` | A panel, a popup, a floating window |
| Over the tool strip, the view bar, the playbar or the navigation axes | `Overlay` | The overlay |
| Over a gizmo handle | `GizmoHandle` | The handle |
| Over a blocking element of the game's UI | `GameUI` | Selected, unless the game has the viewport - then the game's |
| Anywhere else on the viewport | `Scene` | The picker |

While a field is typed into, a menu, popup or dialog is open, or Preferences
waits for a key to rebind, no keybind answers and the fly camera hears nothing
(`InputOwnership::keysHeldByUI`).

**A play session's viewport is the game's.** With the pointer on the viewport,
or the cursor grabbed by the game, keys and mouse buttons go to the game: no
editor shortcut acts, so Ctrl+1 picks a weapon rather than hiding the Hierarchy.
Still live: the **Toggle Editor** key (F5), **Play / Stop** (Ctrl+P),
**Pause / Resume** (Ctrl+Shift+P), **Eject / Return** (F8;
`PlaybackBar::processKeys`) and the playbar.
The editor's view stands down (`CameraControllerSystem::setActive`), and
framing, the navigation axes, the gizmo and click-picking with it
(`InputOwnership::clickPicks`).

**Eject / Return** (F8, or the playbar's camera button) hands the viewport back:
the running game renders through the editor's viewpoint, which flies, picks and
edits as in Edit mode, while the game hears nothing
(`InputOwnership::hostHoldsPointer`). A cursor the game grabbed is freed while
ejected and restored on return. Stop ends an ejection with the session.

## Asset browser

**Operations act on the tile under the pointer**, not a selection: **F2**
renames it; its context menu assigns it to the selected entity through an
`EditScope<T>` (an undo step, and an override on an instance). **Rename**
suffixes a taken name (`ResourceManager::rename`). **Delete asks first** instead
of offering undo: a re-added asset takes a new slot, so every handle to the old
one stays dead.

**Import.** The toolbar's first control is the kind's primary verb - `New` for
materials, `Import...` for the rest. Meshes, skeletons and clips raise
`EditorState::requestModelImport`, since all three come out of one model. **A
texture imports as colour (`TextureUsage::Color`)**; data and normal maps come
with their model or through the Material Editor slot, which knows the usage. A
file the project already holds is refused, because `ResourceManager::add` under
a taken name replaces that asset in place.

**"In use"** - the strip on every tile, and Delete's guard - is
`AssetSerializer::collectAssetRefs` (`io/asset/asset_serializer.h`), the walk a
scene save builds its `assets` block from, so an asset a save would name is in
use. Three kinds also count references that walk does not see:

| Kind      | Also referenced by |
|-----------|--------------------|
| Textures  | every `TextureHandle` slot on **every** `MaterialAsset`, drawn or not (`VKM_MATERIAL_MAPS`) |
| Skeletons | a skinned mesh's `MeshAsset::skeleton` and `AnimationClipAsset::skeleton`, resolved from their **name** strings |
| Clips     | every `Animator`'s `fadeFrom` - session state, but a fading clip is still sampled |

A clip tile marked **`no rig`** names a skeleton that does not resolve, so it
animates nothing.

**Previews.** Materials and meshes render through
`EditorRenderHooks::renderPreview`, cached per asset and version, a few baked per
frame (`MaterialPreviewSession`). A texture tile draws the renderer's own GPU
copy, uploaded through `EditorRenderHooks::ensureTexture`. **Known:** an sRGB
texture's thumbnail draws darker than the file - ImGui writes the linear sample
to a framebuffer that is not sRGB-encoded.

**Inside**, the browser is a `KINDS[]` table of `AssetKind` descriptors, one per
`AssetType`, each a set of function pointers; the only place a C++ asset type
appears is `KindOps<Asset>`. `rename` is null for skeletons, whose name meshes
and clips carry as a string. `FontAsset` is not a kind: the library does not
hold it.

### What Create > Primitive puts in the asset graph

A generated mesh has a deterministic name - `mesh:generator:cube`,
`mesh:generator:sphere:16` - and `addGeneratedMesh` and
`generateDefaultMaterial` reuse what the graph holds under it, so three cubes
share one mesh and one `material:default`. A material of its own comes from
**Duplicate** or **New**, which copy the default rather than rename it -
renaming would pull `material:default` from under everything that names it.

## Prefabs

### What an instance will not let you do

A scene stores a prefab instance as a reference, a pose and its overrides, not
the subtree - so anything done inside an instance that is not an override is not
saved. The editor makes each gesture mean what it looks like, or refuses it:

- **Duplicate** instances the prefab again, with its overrides.
- **Undo of a delete** brings the instance back whole, overrides intact.
- **Re-parenting into or out of an instance** is refused with a toast
  (`EditorActions::reparentKeepingWorld`); the root itself moves anywhere.
- **Add Component** inside an instance works and warns: saving the instance over
  its file is how a component is added to a prefab.
- **An entity picker** - a Joint's Connected, a slider's Knob - is disabled
  inside an instance: an override is read back in the prefab's namespace, a pick
  names an entity in the scene's.
- **Revert to prefab** on a component the prefab no longer defines is refused
  and the entry kept.
- **Save as Prefab** inside an instance is refused; save the instance root.

### Save as Prefab

`EditorActions::saveAsPrefab` writes the selected subtree to the project's
`prefabs/` and turns it into an instance of the file.

- **Nothing is overwritten by a name collision.** An existing instance saves back
  over its own source - that is how a prefab is edited; anything else takes the
  first free name (`Enemy`, `Enemy 2`, ...), which the toast names.
- **A save drops exactly the history steps that name the subtree**
  (`CommandStack::forget`, via `Command::addresses`): the scene now stores a
  reference, so a step assigning a component in there would undo to a value no
  longer stored.

## Undo / redo

Every mutation goes through a `Command` holding the before and after state. The
caller has already applied the change, so pushing records it; undo and redo
replay one side. The stack holds 200 steps (`CommandStack::HISTORY_LIMIT`) and
is cleared on scene load, since entity IDs do not survive the swap. The commands
are documented in `command/editor_commands.h`; which one an edit takes:

- **A component's value** goes through `editStep<T>`, `EditScope<T>` or
  `pushEdit` (`command/component_edit.h`), which push a `ComponentEditCommand<T>`
  - or a `PrefabOverrideCommand` inside an instance. Every component the editor
  can put back is a row of `VKM_EDITOR_COMPONENTS`.
- **Everything else** - scene values, scripts, entities and subtrees, reparents,
  prefabs, assets - has its own command in `command/editor_commands.h`;
  `CompositeCommand` makes several steps one.
- **Never create an entity outside the history**: freed slots are reused last-in
  first-out, so it lands in the slot a deleted entity's undo is waiting for.

`CommandStack::push` tries `Command::tryMerge` against the top step while the
gesture is open - that is drag coalescing. `EditorSystem` calls
`CommandStack::endGesture` at the end of every frame with no mouse button held
and no ImGui item active, sealing the top; without it, a second drag of the same
field would merge into the first.

## Opening a project

The editor edits *a project*. If `ProjectController::open` fails at
`EditorSystem::init`, the editor draws the **start screen** and no workspace,
since every workspace path would resolve against the engine's own directory.
With no argument, a `vkm_editor` shipped beside a project opens it. A path
naming a file opens its project (`findProjectRoot` walks up). See
[io.md](io.md#projects-and-their-roots).

`ProjectController::open` (`session/project_controller.h`) roots the editor in a
project, or re-roots it in place with no restart. Its steps are in order because
each composes paths or runs code the one before put in place: the outgoing
project's settings are saved while its root is current, the scene is torn down
(`SceneIOController::beginSceneReplace`) while the old gameplay module still
holds the behaviors' code, and only then is the module swapped. It builds the
wire schema from the module's `vkmSetupNetwork` so the editor can show what goes
on the wire (the editor never hosts or joins), and boots the world through
`bootProjectWorld`, as every host but `vkm_cook` does. An entry scene that will
not load leaves the default scene and an error toast - the editor is where you
fix it ([io.md](io.md#what-each-host-does-when-a-project-will-not-open)).

Choosing a project - `OpenProjectDialog` (`chrome/open_project_dialog.h`), Open
Recent, the start screen - goes through `EditorState::requestSceneAction`,
because opening one throws the scene away.

### Building from the editor

New Project, File > Build Scripts and File > Package Game run `vkm new`, `vkm
build` and `vkm package` through the launcher beside the engine
(`BuildController::launcher`); the editor never builds itself. Output fills the
**Build** window, which can stop the run and its compilers (`ChildProcess`).
Outside a play session the editor's watch on the module reloads a new build
however it was made; during one, a toast points at File > Reload Scripts. A
project that opens with no module is built once.

### The start screen

- **Projects** - recents, newest first, searchable. One made for another minor
  release (`compatibleEngine`, `io/project.h`) shows a "Made for" badge; opening
  it asks, then writes this engine's version into its `project.json`.
- **Examples** - a card per project in the engine's `examples/`, opening New
  Project on a copy (`EditorState::newProjectTemplate`): in an SDK the example
  sits inside an install the next update replaces.

## Actions that throw the live scene away

Quit (File > Exit, the titlebar close), New Scene, Open Scene and Open Project
all go through one entry point, which neither checks dirtiness nor acts - so a
new destructive action cannot forget to ask:

```cpp
state.requestSceneAction(EditorState::SceneAction::Open, path);
```

`EditorSystem::resolveSceneAction` answers it once per frame, **before the ImGui
frame opens**, because rebuilding the scene must not happen with a window on the
ImGui stack. Stages: **Ask** (clean goes straight to Run; dirty raises *Unsaved
Changes*), **Saving** (ends any play session, since a save inside one is refused;
backing out of the Save-As withdraws the request), **Run** (`performSceneAction`).

## Scene I/O

`SceneIOController` owns New / Open / Save / Save-As: the file-picker modals,
then `SceneSerializer::save` / `load` ([io.md](io.md)). A load restores the
editor's viewpoint for that scene and ends any play session (`endPlaySession`).

### A play session owns the scene

During a session the ECS holds the simulation's copy, and every panel stays live:

- **Save is refused** - it would write a scene nobody authored.
- **The viewport is framed** in the warning colour, captioned PLAY MODE.
- **Stop warns** when the session took an undo step or dirtied a scene that was
  clean at Play.
- **Stop restores the scene and the materials, not other assets**: a mesh,
  texture, rig, clip or sound edited during play keeps the edit.
- **Stop restores the render settings**, which a game's settings screen may
  have written.

What Play captures - the scene document, the asset list, where each prefab
instance's entities stood, the dirty flag - is `PlaySnapshot`
(`session/play_snapshot.h`), which needs nothing of the editor, so
`vkm_engine_tests play` covers the round trip. `SceneIOController::isPlaying()`
is the state; ask it, not the clock, which is paused in Edit mode too.
`SceneIOController::stopPlaySession` is all of Stop.

### What an open does to the session's imports

An open is a clean break - undo stack, selection, previews and a fresh
`ResourceManager` - ending in `afterSceneReplace`, after which an open reselects
the active entity by name. New Scene and Open Project reach it through
`beginSceneReplace`, which empties the scene first. An import the outgoing scene
never named - a sound not yet assigned - has nothing to be recreated from and is
dropped; the count goes in a toast, the names in the log.

**Stop is the opposite**: it puts the session back exactly as Play found it,
history included.

- The snapshot is read back into the slots it came from, prefab instances
  included, so every undo step still names what it did.
- `captureSnapshot` parks the authored history (`CommandStack::park`) and the
  session runs on an empty stack, so Ctrl+Z in play never reaches an authored
  step. Stop restores it (`unpark`) - or drops it with a toast if an instance
  could not return to its slots (`PlaySnapshot::restoredInPlace`).
- **The asset graph is kept, not rebuilt**, because undo steps hold handles into
  one `ResourceManager`: `restoreSnapshot` merges the snapshot into it
  (`AssetPolicy::Merge`). Materials reload in place (`LoadMode::Reload`), and
  an asset made during play is dropped (`AssetSerializer::dropAssetsNotIn`).

Steps that destroy by slot check it still holds their entity
(`EntitySnapshot::describes`), and a step reclaiming a slot checks it is free;
a mismatch skips the step with an error toast.

---

## How it works inside

### Where things are

Every directory under `src/editor/` is one responsibility:

| Directory | Holds |
|---|---|
| *(root)* | `EditorSystem`, `EditorState`, `EditorContext`, `EditorSettings`, and `editor_actions` - the scene mutations panels invoke |
| `command/` | Undo: `Command`, `CommandStack`, the commands, `EditScope`/`editStep`/`pushEdit`, `CommandHost::pushStep`, prefab-override bookkeeping. Reaches the editor through `CommandHost`, so it tests without a window |
| `session/` | What outlives a frame: `SceneIOController`, `ProjectController`, `PlaySnapshot`, `BuildController`, the material preview cache |
| `panels/` | One file per panel, owning only its own widget state |
| `overlays/` | Drawn over the viewport: gizmo, tool strip and view bar, playbar, navigation axes |
| `chrome/` | Drawn around the panels: menu bar, status bar, default layout, and the menus' dialogs, drawn at the menu bar's scope so a closing menu does not take them |
| `input/` | Keybinds and shortcut dispatch, `InputOwnership`, `CameraControllerSystem` and `ViewFraming` |
| `ui/` | The widget vocabulary: `prop*` rows, dialog scaffold, style, icons, asset picker, audition transport |

### What the editor tells the engine

The scene renders into the window's backbuffer inside the viewport's rect; the
Viewport window has no background. Once a frame the editor states that rect,
and whether its panels hold the pointer and keyboard, on `HostChrome`
(`ctx.chrome`). `RenderSystem`, `VisibilitySystem` and `UISystem` read the rect;
`InputMap::update` reads the capture ([input.md](input.md)). A runtime writes
neither, and the defaults are what a shipped game wants.

The world is open to edits (`HostChrome::setAuthoring`) in Edit mode, paused,
ejected, or with the panels shown. Fixed meshes (`Mesh::movable` off, the Mesh
card's Movable) keep nothing then ([visibility.md](visibility.md#fixed-meshes)),
so the work they save shows only in a running session with the editor hidden
(**Toggle Editor**, F5) or in `vkm_runtime`.

### The editor's view

`CameraControllerSystem` holds the `EditorViewpoint` and publishes it as
`ctx.hostView` at the Input stage, before every reader of a view
([visibility.md](visibility.md#where-the-view-comes-from)); `VisibilitySystem`
renders through it instead of the scene's camera. The picker, gizmo, wire
overlays and navigation axes project through `ctx.visibility`'s matrices, so
they follow whatever the frame rendered through.

Only the editor registers it - `vkm_editor`'s `main()` adds it after
`setupEngineApp` - so a shipped game cannot reach it. Its fly bindings
(`CameraActions`) live in an `InputMap` it owns, sampled against what the panels
hold (`setCapture`), never in the game's: an ejected session can fly while the
game hears nothing, and a game's map spends no command slots on `Camera/`
actions.

## Key files

- `src/editor/editor_system.h` - `EditorSystem`, the frame and the scene-action guard
- `src/editor/editor_state.h` - `EditorState`: selection, preferences, requests
- `src/editor/command/editor_commands.h` - every undo command
- `src/editor/session/project_controller.h` - `ProjectController::open`
- `src/editor/session/scene_io_controller.h` - `SceneIOController`
- `src/editor/session/play_snapshot.h` - `PlaySnapshot`
- `src/editor/input/input_ownership.h` - `InputOwnership`
- `src/editor/input/camera_controller_system.h` - `CameraControllerSystem`, `CameraActions`
