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
|           |                                       |           |
|           | [T]      [Playback bar]   [View bar]  |           |
|           | [o]                                   |           |
|           | [o]                                   |           |
|           | [l]                       [Nav axes]  |           |
|           +---------------------------------------+           |
|           |      Assets | Animation | Console     |           |
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
are constants in `chrome/dock_layout.h`. Multi-viewport is on: a panel dragged
out of the window becomes a window of its own, on any monitor, drawn by
`EditorRenderHooks::renderImGui`. The scene's viewport alone stays in the main
window (`drawViewport`), as the renderer draws the scene into its framebuffer.

- **Default layout** - the one above, cut by `buildDefaultLayout`
  (`chrome/dock_layout.cpp`) when the ini holds no dockspace and on **Window >
  Reset Layout**, which also shows the Hierarchy, Inspector and Assets groups;
  Terrain opens on a terrain. **Window > Hide Editor** (F5, which brings it
  back) leaves the scene the whole window.
- **What is shown is the editor's.** Window > Hierarchy, Inspector and Assets
  (Ctrl+1 to Ctrl+3) show and hide the groups the default layout docks together:
  the Hierarchy; the Inspector; Assets, Animation and Console. Window >
  Terrain (Ctrl+5) shows the Terrain window, a tab beside the Inspector; Window >
  Console (Ctrl+6) brings the bottom row forward on the Console.
  Persisted per project in `editor_settings.json`. Windows have no close box, so a
  toggle is the one way to hide one and to get it back.
- **A panel's search** (`searchField`, `ui/editor_widgets.h`) - the Hierarchy's,
  Assets', the Console's and the start screen's: Ctrl+F reaches it while its panel
  has focus, and Escape empties it.
- **Where it is, is per person**: `io.IniFilename` is `imgui.ini` in
  `ProjectPaths::userRoot()`, so the layout follows the person across projects
  ([io.md](io.md#which-root-owns-a-path)). Popups, dialogs, the root window
  and the asset editors' windows are `NoSavedSettings`.

## Panels

| Panel               | File                                  | Description                                                                 |
|---------------------|---------------------------------------|-----------------------------------------------------------------------------|
| Hierarchy           | `panels/hierarchy_panel.cpp`          | Every entity; drag a row to reparent, reorder or unparent it; Shift-click spans rows; a selection made elsewhere is opened up to and scrolled to |
| Inspector           | `panels/inspector_panel.cpp`          | Component cards; prefab-instance overrides |
| Animation           | `panels/animation_panel.cpp`          | Transport, three-lane keyframe timeline, a table per track |
| Render Settings     | `panels/render_settings_panel.cpp`    | Floating window over `RenderSettings` (Window > Render Settings) |
| Material Editor     | `panels/material_editor_panel.cpp`    | What a material's window draws: a live preview beside its values, above them when narrow |
| Terrain Editor      | `panels/terrain_editor_panel.cpp`     | The **Terrain** window: tools, brush, layers and plants ([terrain.md](terrain.md#making-one)) |
| Asset Browser       | `panels/asset_browser_panel.cpp`      | The **Assets** window: one library for every asset kind |
| Project Settings    | `panels/project_settings_panel.cpp`   | Floating window over `project.json` (File > Settings...) |
| Preferences         | `panels/preferences_panel.cpp`        | Floating editor settings window (Edit > Preferences, Ctrl+,) |
| Console             | `panels/console_panel.cpp`            | This run's log, followed from the file the logger writes (`LogTail`, `hostLog()`); filter by level and text. Its Errors view: the failures `reportError` recorded (`EngineErrorLog`), repeats counted, newest first; the tab shows their count |
| Console > Build     | `session/build_controller.cpp`        | The vkm command running or last run, and its output; a run brings it forward |
| Viewport Overlay    | `overlays/viewport_overlay.cpp`       | The axis navigation gizmo |
| Gizmo Overlay       | `overlays/gizmo_overlay.cpp`          | The transform gizmo's drawing and drag, and click-to-pick |
| Gizmo Drawing       | `overlays/gizmo_overlay_draw.cpp`     | The markers (`mark*`), the selected UI rects, and what the engine's world gizmos show (`showWorldGizmos`) |
| Viewport Toolbar    | `overlays/viewport_toolbar.cpp`       | Tool strip (tool, space, snap) and view bar (shading, camera, projection, Frame All, Focus) |
| Playback Bar        | `overlays/playback_bar.cpp`           | Play / Pause / Step / Stop / Eject and the speed; frames and captions the viewport during a session |
| Start Screen        | `panels/start_screen.cpp`             | Shown when no project is open |
| Asset Editors       | `asset_editors/asset_editors.cpp`     | A window for each asset open in its editor: a Mesh, a Texture, a Material, a Clip |

The **chrome** belongs to `EditorSystem` and sits around the layout:

| Chrome              | File                                  | Description                                                                 |
|---------------------|---------------------------------------|-----------------------------------------------------------------------------|
| Menu Bar            | `chrome/editor_menu_bar.cpp`          | File / Edit / View / Window / Entity / Help (Keyboard & Mouse lists every binding as bound now); reads and writes `EditorState`, forwards scene-file intents to `SceneIOController` |
| Status Bar          | `chrome/editor_status_bar.cpp`        | Dirty dot, the selection's breadcrumb and position; errors, warnings and a build running or failed, each a click from its Console view |
| Dock Layout         | `chrome/dock_layout.cpp`              | Window names and the default layout |

Preferences, Render Settings and Project Settings open through
`beginToolWindow` (`ui/editor_dialogs.h`), which never docks.

### Where world settings live

Scene-global settings are cards in the **World inspector** (the **World** row at
the top of the hierarchy): `Environment`, `Procedural Sky`, `Volumetric Fog`,
`Acoustics` and `Physics` (`Scene::physics()`). They are scene data. Render
Settings, quality tuning rather than content, has its own window.

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

A material opens in its own window (`MaterialEditor`, under "Asset editors")
through `EditorState::openMaterial(handle)`.

- **Surface and Base are always there**, each map a slot on its value's row;
  Packed Maps starts open when one is bound and UV when off its defaults. Each of the
  five secondary lobes is a card only while the material uses it. `FEATURES` in
  the panel says per lobe how to tell it is on, switch it on, and reset its
  fields; `+ Add Feature` offers the ones that are off.
- **A row that cannot do anything is not drawn** - Cutoff only for AlphaMask,
  emission Strength only once the emission is not black.
- **The selection moves nothing.** Each window edits its one material; its
  preview offers the selected entity's mesh as a shape.

### Asset editors

An asset opens in a window of its own - a **double-click** on its tile in the
Assets panel, or **Open** in the tile's menu (`EditorState::openAssetEditor`) -
which docks, floats or leaves the main window like any panel. Opening one
already open brings its window forward; closing it, deleting the asset, or
opening a scene or project (a new resource graph) closes it (`AssetEditors`,
`asset_editors/`). Each kind's editor is an `AssetEditor`, which knows its asset
by handle.

- **Mesh** (`MeshEditor`): the mesh alone in a viewport of its own, beside its
  triangles, vertices, size, rig and lightmap UVs; it previews in a material
  picked there (a plain grey for none) and **Place in Scene** adds it where the
  editor's view looks.
- **Material** (`MaterialEditor`): the material editor above - its preview to
  orbit, its maps and parameters.
- **Clip** (`ClipViewer`): the animation clip played on the meshes rigged to
  its skeleton, posed as the frame poses a rig (`composePose`, the preview
  drawing each with its palette), beside the animation panels' transport
  (`clipTransport`), a scrubber, and its length, rig and markers; a clip that
  does not fit its rig (`clipFitsRig`) says so.
- **Texture** (`TextureViewer`): the texture over a checkerboard, zoomed about
  the cursor by the wheel, panned by a drag and fitted by a double-click, beside
  its size, channels, usage, storage, levels and memory; its red, green and blue
  show apart.

An editor's viewport is a `PreviewViewport` (`ui/preview_viewport.h`): the
backend's preview of its draws (`EditorRenderHooks::renderPreview`, at the
viewport's own size), rendered again when the view or what it draws changes.
Drag to orbit, middle-drag or Shift-drag to pan, the wheel to zoom, F or a
double-click to frame; a strip in its corner picks the backdrop and swings the
light.

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

A component card is an `editComponentCard` section, so it undoes and records
prefab overrides; its `VKM_INSPECTOR_CARDS` row puts it in Add Component under
the row's `AddSection`, unless the row's hint is null. A right click on its header offers
**Reset** (the component's defaults), **Copy**, **Paste Values** (a copy of the
same type, from `EditorState::componentClipboard`) and **Remove Component**, each
one step like a field's edit. Reset and Paste take the authored fields alone
(`withAuthored`, `command/component_edit.h`), so a run's state - a voice
started, a body asleep - never lands in the scene. The copy survives a Stop
that puts every entity back in its slot, so values tuned in a session can be
pasted back into the authored scene; opening another scene drops it. A card
owning more than itself (Ragdoll) has no menu. Controls over session state push
no edit:

- **Animator** - loop, speed and **Play On Start** push an edit; play, stop and
  the scrubber do not, which is safe only because `Animator::playing` and
  `Animator::time` are session state, never serialized.
- **Bone Socket** - the Offset is authored; the entity's `Transform` is the
  socket's output, rewritten from the bone every frame.
- **Audio Source** - `auditionTransport` plays the clip **through the device, not
  `AudioSource::playing`**, whose write would be a scene edit
  ([audio.md](audio.md#the-cursor-is-the-devices-not-the-components)).
- **Terrain** - the asset, shadows and Edit Terrain, which opens the Terrain
  window. There the brush is editor state (`TerrainToolSettings`, its `target` the
  terrain edited); layers and plants are the asset's, each change a
  `TerrainEditCommand`, and a stroke a `TerrainStrokeCommand` holding the region
  it touched ([terrain.md](terrain.md#making-one)).
- **Particle Emitter** - in Edit mode, Play / Pause / Restart / Stop preview the
  selected effects (or **All**) while the world stands still. The editor states
  the preview on `HostChrome::setEffectPreview`; `ParticleSystem` steps only the
  previewed emitters, plays a burst once per run and loops a spent one after a
  rest. Pools are cleared on every start and stop, and no component is written,
  so a preview leaves the scene as it was.

**The identity header** shows the entity's wire slot and which components
replicate, or warns that it is off the wire and why (`netSilence`)
([networking.md](networking.md#identity-is-the-servers-slot)). Drawn only when
the project replicates something and the entity carries some of it.

**A card greys what the scene, not the author, writes.** With **Procedural Sky**
on, `SkySystem` drives the key light's rotation, color and intensity, so that
Light card says so and disables color and intensity. Both find the light with
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
value keeps its offset. A right click on the toggle picks a common step for each
tool; Preferences takes any other.

**Alt held as a handle is taken** drags a copy of the selection away and leaves
the originals (`duplicateSelection` in place), one step for the copy and one for
the drag; a copy never dragged off is taken back. Not when the active entity sits
under another selected one. Duplicate keeps the active entity's copy active.

**Copy, Cut and Paste** (Ctrl+C, X, V; Edit menu) hold the selection's subtrees
as a scene document (`EntityClipboard` in `editor_actions.h`), assets by name, so
a copy pastes into another scene as into its own. A paste lands where the copy
stood: under the same parent in the world it came from - the scene back from
Play included (`carryClipboard`) - else at its world pose at the root. A copy is
a duplicate's, except that references outside what was copied are dropped
(`SnapshotSlots::Foreign`); between the copied subtrees they are kept
(`SubtreeSnapshot::applyTogether`). Ctrl+A selects every entity.

Keys, while the cursor is not captured: `Q` Select, `W` Move, `E` Rotate, `R`
Scale, `X` Local / World - rebindable in Preferences > Keybinds.

A selected UI element has no Transform; with a tool other than Select its rect
takes handles that move and resize it in its canvas's reference pixels, one step
per drag (`GizmoOverlay::drawUIRectTool`, `overlays/ui_rect_drag.h`).

While the Terrain window is open with a tool picked, a left drag over the terrain
it edits brushes (`TerrainToolOverlay`) and no gizmo is drawn; a click that misses
it picks as ever, and Esc puts the tool down before it deselects.

Everything with no mesh draws an authoring marker (`gizmo_overlay_draw.cpp`):
lights, cameras, probes, planar reflectors and irradiance volumes, decals,
particle emitters, audio sources and listeners, and joints. The shapes in the
world - a collider's wires, a camera's frustum, a rig's bones, the selection's
boxes and the rest - are the engine's, drawn through `DebugDraw` into the scene,
depth-tested, and dotted where something hides them
([rendering.md](rendering.md#debug-drawing)). The editor hands `DebugDraw` a
category per kind and the selection as its focus each frame
(`GizmoOverlay::showWorldGizmos`). A light's reach - a point light's sphere, a
spot's cone, an area light's range - an emitter's shape, a sound's spheres, a
volume's probes and a rig's axes are drawn for the selection alone.
**View > Show** toggles each kind (`GizmoVisibility`, per project in
`editor_settings.json`); a hidden kind cannot be picked. ImGui keeps what is 2D
or answers the pointer: the markers, the transform gizmo and the UI rects.

### Entity selection and shortcuts

- A viewport click tests the visible set's world boxes, then each hit mesh's
  triangles (`mesh_pick.h`); a skinned mesh, whose stored triangles are
  in bind pose, is picked by its box. A meshless entity is picked by its marker:
  `GizmoOverlay::markEntity` draws every marker, and the picker tests exactly
  those drawn that frame.
- Probes, irradiance volumes, decals and particle emitters pick by their marker
  only, never by their box, which would swallow every click inside it.
- A click inside a prefab instance selects the outermost instance first, as a
  placed thing is moved whole; once the selection is inside one, the next click
  goes a level in - a nested instance, then the part (`overlays/click_target.h`).
- The selection is an ordered list (`EditorState::selection`); the active
  entity is `EditorState::selectedEntity`, always in it, and
  `EditorState::selectedAt` indexes it by slot. Only `EditorState`'s selection
  helpers write them.
- The inspector shows the active entity. Inside a prefab instance an edit is a
  per-instance override with "Revert to prefab" (`command/prefab_overrides.h`).
- With several selected, an edit reaches each entity holding the component, as
  the fields it changed, in one step (`editSelectionStep`,
  `command/component_edit.h`). A list (a collider's parts, LOD levels) and a
  field naming an entity are not shared (`changedFields`); the name, the Ragdoll
  card, the Scripts and Remove Component act on the active entity alone.
- Every editor-wide shortcut is a keybind in `input/editor_keybinds.h`, dispatched
  by `EditorShortcuts::process`. A panel's own keys act while it has the
  keyboard: the Console list's Up, Down and Ctrl+C (`consoleHoldsKeys`, so the
  copy takes the line, not the entities) and a search's Ctrl+F; over a preview,
  Focus Selected frames the preview (`previewHovered`).
- **Entity > Align With View** moves the active entity to the view's pose (a
  camera then sees what the author sees); **Align View to Selected** is the
  reverse, without roll. **Drop to Ground** lets each selected subtree fall
  straight down onto the first surface not its own (`ViewPlacement::dropBelow`),
  or the ground plane, lifting one sunk up to `SINK_REACH` out; one undo step.

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

**View controls**: the view is flown, orbited, panned and dollied with the mouse
and W A S D Q E, bound by `CameraControllerSystem`
(`input/camera_controller_system.cpp`); **Help > Keyboard & Mouse** lists them.
The fly speed the wheel sets while looking lasts the session; the Preferences
Camera tab (`Preferences::camera`) holds the rest of
`CameraControllerSystem::Settings`.

**Frame Selected** fits the whole selection and everything under it
(`ViewFraming::frameSelected`): the frame's posed boxes where it drew one, else a
mesh's or terrain's own box, else the entity's position; `fitDistance` stands back
until the sphere around them fills the view's height. **Frame All** fits the
visible boxes the same way.

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
capture declared on `HostChrome` take that one answer. The first row that
holds wins:

| When | Owner | What answers a press |
|---|---|---|
| The cursor is hidden and grabbed, by the fly camera or a running game | `Captured` | Nothing of the editor's: ImGui is told it has no mouse |
| A gizmo drag is held | `GizmoHandle` | The drag, wherever it has gone |
| The pointer is off the viewport | `Panel` | A panel, a popup, a floating window |
| Over the tool strip, the view bar, the playbar or the navigation axes | `Overlay` | The overlay |
| Over a gizmo handle | `GizmoHandle` | The handle |
| Over a blocking element of the game's UI | `GameUI` | Selected, unless the game has the viewport - then the game's |
| Anywhere else on the viewport | `Scene` | The picker - or, over the terrain with a Terrain tool up, the brush first |

While a field is typed into, a menu, popup or dialog is open, or Preferences
waits for a key to rebind, no keybind answers and the fly camera hears nothing
(`InputOwnership::keysHeldByUI`). While an edit is held under the button - a
gizmo drag or a terrain stroke, its undo step not yet pushed - no keybind
answers either, Play and undo included (`InputOwnership::held`).

A panel's own keys are outside it: `EditorState::previewHovered` and
`consoleHoldsKeys` are set by the preview and the Console as they draw, and
read by the shortcuts the next frame.

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

**The grid is the project's library**, by name: what the session holds, and,
dimmed and marked `not loaded`, what `AssetLibrary` lists that it does not -
assets an earlier scene imported. Opening, dragging or assigning one loads it
(`AssetSerializer::loadNamed`); so does picking one in an inspector slot
(`pickAsset`), which lists the library the same way. What a scene uses goes into
its `assets` block on save, so a scene opened later loads it again.

**Operations act on the tile under the pointer**, not a selection: **F2**
renames it; its context menu assigns it to the selected entity through an
`EditScope<T>` (an undo step, and an override on an instance). **Rename**
suffixes a taken name (`ResourceManager::rename`). **Delete asks first** instead
of offering undo: a re-added asset takes a new slot, so every handle to the old
one stays dead.

**A tile drags** (`AssetDrag`, `ui/asset_drag.h`). Dropped in the viewport
(`AssetBrowserPanel::acceptViewportDrop`) on a surface whose entity, or the nearest
one holding it, its kind assigns to - a material on a Mesh, a sound on an Audio
Source, a rig or clip on an imported character's Animator - it is assigned as the
context menu would. A mesh always becomes a new entity standing where the
cursor's ray meets the scene (`EditorActions::createFromAsset`), so one dropped
on the floor is set on it; so does a sound dropped elsewhere, and a terrain
becomes ground at the origin. Dropped on
a Hierarchy row it is assigned to that entity, a mesh too
(`AssetBrowserPanel::acceptAssetOn`); below the rows it is placed where the view
looks. Dropped on an inspector asset slot (`pickAsset`, `ui/asset_combo.h`) of
its kind, it fills the slot; a slot's list of more than a few assets opens with a
search box. **Open** on a terrain opens the Terrain window on the entity placing
it, placing one first when none does.

**Import.** The toolbar's first control is the kind's primary verb - `New` for
materials and terrains, `Import...` for the rest. Meshes, skeletons and clips
raise `EditorState::requestModelImport`, since all three come out of one model.
**A texture imports as color (`TextureUsage::Color`)**; data and normal maps
come with their model or through the Material Editor slot, which knows the
usage. A file the project already holds is reported as already imported, not
loaded again. The pickers list `assets/` alone.

**A file dropped on the window** from anywhere is imported as its kind's Import
would (`FileImport::importDropped`, `session/file_import.h`), after a copy into the
project when it lies outside `assets/`: a model into a folder of its own under
`assets/models/`, with the buffers, material libraries and images it names; an
image under `assets/textures/`; a sound under `assets/audio/`. A file already
there with the same bytes is used as it is; another of the name takes a free one.

**"In use"** - the strip on every tile, and Delete's guard - is
`AssetSerializer::collectAssetRefs` (`io/asset/asset_serializer.h`), the walk a
scene save builds its `assets` block from, so an asset a save would name is in
use - and over the project's loading screen while it is built, whose pictures
the scene does not name. Five kinds also count references that walk does not
see:

| Kind      | Also referenced by |
|-----------|--------------------|
| Materials | every terrain's layers and plants |
| Meshes    | every terrain's plants |
| Textures  | every `TextureHandle` slot on **every** `MaterialAsset`, drawn or not (`VKM_MATERIAL_MAPS`) |
| Skeletons | a skinned mesh's `MeshAsset::skeleton` and `AnimationClipAsset::skeleton`, resolved from their **name** strings |
| Clips     | every `Animator`'s `fadeFrom` - session state, but a fading clip is still sampled |

**Select Users** on an asset in use selects the entities naming it
(`AssetKind::named`, the entity half of the walk); an asset only another asset
holds - a texture in a material - or only the project's loading screen says
which instead.

A clip tile marked **`no rig`** names a skeleton that does not resolve, so it
animates nothing.

**Previews.** Materials and meshes render through
`EditorRenderHooks::renderPreview`, cached per asset and version, a few baked per
frame (`MaterialPreviewSession`). A texture tile draws the renderer's own GPU
copy, uploaded through `EditorRenderHooks::ensureTexture`. **Known:** an sRGB
texture's thumbnail draws darker than the file - ImGui writes the linear sample
to a framebuffer that is not sRGB-encoded.

**Inside**, the browser is a `KINDS[]` table of `AssetKind` descriptors, one per
`AssetType`; each is a set of function pointers, so the rail and the grid need no
template over the asset types. `rename` is null for skeletons, whose name meshes
and clips carry as a string, and for terrains, whose name is their file's path.
`FontAsset` is not a kind: the library does not hold it.

### Where Create puts a new entity

Where the view is looking (`ViewPlacement::placeInView`, from
`EditorState::viewAim`, restated each frame): on the first visible mesh or
terrain the view's centre meets, raised so a mesh's base stands on it; else on
the ground plane if that is near; else ahead of the view at its focus distance,
held between `MIN_DISTANCE` and `MAX_DISTANCE`. A placed prefab or effect goes
there too, by its origin. A camera takes the view's own pose, so it starts
seeing what the author sees; a terrain, ground for the whole scene centred on
its entity, stays at the origin, and so does a sea (**Create > Sea**: an `Ocean`
and a dense surface on a Sea material of its own) - or, in a scene with an ocean,
the new surface lies where that ocean does. **Create > Water** is a pool on a
water material of its own, so setting one's flow runs no other.

**Create Child** on a hierarchy row makes the same kinds under that entity, at its
origin. It is refused inside a prefab instance, whose interior is rebuilt from the
prefab.

### What Create > Primitive puts in the asset graph

A generated mesh has a deterministic name - `mesh:generator:cube`,
`mesh:generator:sphere:16` - and `addGeneratedMesh` and
`generateDefaultMaterial` reuse what the graph holds under it, so three cubes
share one mesh and one `material:default`. A material of its own comes from
**Duplicate** or **New**, which copy the default rather than rename it -
renaming would pull `material:default` from under everything that names it.

### What Create > Effect brings into the project

The engine ships effects - an explosion, a campfire, a torch, a plume of smoke -
as a pack under `ProjectPaths::engineContent()`, `content/effects/`, laid out as
a project is: textures and sounds in `assets/effects/`, prefabs in
`prefabs/effects/`, their recipes in `library/`. The menu lists the pack's
prefabs; choosing one copies each file of the pack the project lacks to the same
path in the project, then places the prefab as **Prefab** does
(`EditorActions::placeEffect`).

- **The project owns its copy.** It cooks, ships and edits it as its own; a
  file it already has is never overwritten, so an edited effect stays edited
  and an engine update does not reach it. Delete a file to take the engine's
  again.
- **An effect cleans up after itself.** Each root is `NetLocal` - every end of a
  game spawns its own - and a one-off is rooted on its longest-lived emitter,
  whose `destroyWhenSpent` takes the rest with it, the explosion's scorch mark
  included.

## Prefabs

### What an instance will not let you do

A scene stores a prefab instance as a reference, a pose and its overrides, not
the subtree - so anything done inside an instance that is not an override is not
saved. The editor makes each gesture mean what it looks like, or refuses it:

- **Duplicate** and **Paste** instance the prefab again, with its overrides. A
  copy that would land inside an instance - of one of its parts, or pasted under
  one - stands at the root where it stood in the world instead (`copyParent`).
- **Undo of a delete** brings the instance back whole, overrides intact.
- **Re-parenting into or out of an instance** is refused with a toast
  (`EditorActions::reparentKeepingWorld`); the root itself moves anywhere.
- **Create** under an instance's root or one of its parts is refused with a
  toast (`EditorActions::createEntity`). It, a re-parent and a copy ask the one
  rule, `refusesNewChild`.
- **Add Component** inside an instance works and warns: saving the instance over
  its file is how a component is added to a prefab.
- **An entity picker** - a Joint's Connected, a slider's Knob - is disabled
  inside an instance: an override is read back in the prefab's namespace, a pick
  names an entity in the scene's.
- **Revert to prefab** on a component the prefab no longer defines is refused
  and the entry kept.
- **Save as Prefab** inside an instance is refused; save the instance root.

### The instance's card

The **Prefab Instance** card, on the root or any entity of an instance, offers
what an instance is for: **Apply to Prefab** saves the root over its source
(Save as Prefab), so every instance takes the overrides; **Revert All** drops
them as one step (`PrefabOverrides::revertAll`); **Unpack** keeps the entities as
they are and drops the reference (`EditorActions::unpackPrefab`), so the scene
stores them. Save as Prefab is also in the **Entity** menu, on the selection.

### Save as Prefab

`EditorActions::saveAsPrefab` writes the selected subtree to the project's
`prefabs/` and turns it into an instance of the file.

- **Nothing is overwritten by a name collision.** An existing instance saves back
  over its own source - that is how a prefab is edited; anything else takes the
  first free name (`Enemy`, `Enemy 2`, ...), which the toast names.
- **The assets it names are cooked first**, as a scene's save cooks them, so the
  file resolves in a session that did not import them.
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

**Edit > History** lists the steps by label (`undoLabel(back)`,
`redoLabel(ahead)`) either side of where the scene stands; a click undoes or
redoes to that step.

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

**What opening looks like** is a game's start: the engine's splash, then the
loading screen over the world until it is ready, then the workspace fading in
as the screen lifts. A project or scene the editor opens asks for that cover
(`HostChrome::coverNextWorld`); Play and Stop put a world back without it. With
no project the start screen comes up the same way, after the splash and,
while the renderer's programs compile, the loading screen.

Choosing a project - `OpenProjectDialog` (`chrome/open_project_dialog.h`), Open
Recent, the start screen - goes through `EditorState::requestSceneAction`,
because opening one throws the scene away.

### Building from the editor

New Project, File > Build Scripts and File > Package Game run `vkm new`, `vkm
build` and `vkm package` through the launcher beside the engine
(`BuildController::launcher`); the editor never builds itself. Output fills the
Console's **Build** view, which can stop the run and its compilers (`ChildProcess`).
One runs at a time: another asked for meanwhile is refused with a toast.
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

### Unsaved changes outlive a crash

While the scene is dirty outside a session, `SceneIOController::serviceRecovery`
keeps a copy of it, and of its terrains' unsaved strokes, in the user's folder,
never the project's (`session/scene_recovery.h`). It changes nothing the project
already holds - a material's unsaved values are not in the copy - and only
records an asset the library has no record of, so the copy can name a new import
(`keepAssets`, `session/asset_writes.h`). A save, leaving the scene or
a clean close removes the copy; a crash leaves it, and the scene opened with a
copy newer than its file offers to **Recover** it, **Discard** it, or
**Cancel**.

### A play session owns the scene

During a session the ECS holds the simulation's copy, and every panel stays live:

- **Save is refused** - it would write a scene nobody authored.
- **The viewport is framed** in the warning color, captioned PLAY MODE.
- **Stop warns** when the session took an undo step or dirtied a scene that was
  clean at Play.
- **Stop restores the scene, the materials and the terrains, not other
  assets**: a terrain comes back from the file Play wrote it to
  (`readTerrainFiles`, `session/terrain_files.h`), so Play is refused when one
  cannot be written, as a save that cannot write one leaves the scene unsaved;
  a mesh, texture, rig, clip or sound edited during play keeps the edit.
- **Ending a session restores the render settings**, which a game's settings
  screen may have written - Stop, or an open, a new scene or a project switch
  during it.
- **The playbar's speed** (`Clock::setHostScale`) runs the session slow or fast,
  multiplied with the game's own time scale, which it never writes. It outlives
  the session and reads in the warning color while not 1x; it slows an effect
  preview too.

What Play captures - the scene document, the asset list, where each prefab
instance's entities stood, the dirty flag - is `PlaySnapshot`
(`session/play_snapshot.h`), which needs nothing of the editor, so
`vkm_engine_tests play` covers the round trip. `SceneIOController::isPlaying()`
is the state; ask it, not the clock, which is paused in Edit mode too.
`SceneIOController::stopPlaySession` is all of Stop.

### What an open does to the session's imports

An open is a clean break - undo stack, selection, previews and a fresh
`ResourceManager` - and every one, a load, a recovery, New Scene and Open
Project, goes through `openWorld`, ending in `afterSceneReplace`; a load then
reselects the active entity by name. New Scene and Open Project reach it through
`beginSceneReplace`, which empties the scene. An import the outgoing scene never
named - a sound not yet assigned - has nothing to be recreated from and is
dropped; the count goes in a toast, the names in the log. What the project's
loading screen names is not one: it is built again from its file.

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
| *(root)* | `EditorSystem`, `EditorState`, `EditorContext`, `EditorSettings`, `editor_actions` - the scene mutations panels invoke - where a copy lands (`copy_parent.h`) and the triangle pick (`mesh_pick.h`) |
| `command/` | Undo: `Command`, `CommandStack`, the commands, `EditScope`/`editStep`/`pushEdit`, `CommandHost::pushStep`, prefab-override bookkeeping. Reaches the editor through `CommandHost`, so it tests without a window |
| `session/` | What outlives a frame, and the project's files: `SceneIOController`, `ProjectController`, `PlaySnapshot`, `BuildController`, the material preview cache, `ContentPack` (the engine's packs brought into a project), `FileImport`, the terrains' files (`terrain_files.h`), what a save writes beside its document (`asset_writes.h`), `SceneRecovery` (a crash's copy) and `LogTail` (the log file followed) |
| `panels/` | One file per panel, owning only its own widget state |
| `overlays/` | Drawn over the viewport: gizmo, tool strip and view bar, playbar, navigation axes |
| `chrome/` | Drawn around the panels: menu bar, status bar, default layout, and the menus' dialogs, drawn at the menu bar's scope so a closing menu does not take them |
| `input/` | Keybinds and shortcut dispatch, `InputOwnership`, `CameraControllerSystem`, `ViewFraming` and `ViewPlacement` |
| `asset_editors/` | An asset's own window: `AssetEditors` and an `AssetEditor` per kind - mesh, material, texture, clip |
| `ui/` | The widget vocabulary: `prop*` rows, dialog scaffold, style, icons, asset picker, audition transport, `PreviewViewport` |

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
renders through it instead of the scene's camera. The picker, gizmo, markers and
navigation axes project through `ctx.visibility`'s matrices, so they follow
whatever the frame rendered through.

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
