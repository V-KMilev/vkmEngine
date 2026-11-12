# Editor

`vkm_editor` edits one project - its scenes, prefabs and assets - and plays it in a session
to try them. Its menus, tooltips and **Help > Keyboard & Mouse** say what each control does
and which key reaches it, and [features.md](features.md#the-editor) lists what it has. This
page holds what the controls cannot say: what the editor promises whoever authors with it,
and then, under [How it works inside](#how-it-works-inside), how it is built.

## What the viewport shows

The editor looks through a viewpoint of its own (`EditorViewpoint`), not an entity: flying
marks nothing unsaved, a scene needs no camera to be edited, and nothing of the editor's
writes the game's camera. The view bar's camera box picks what the frame renders through:

| Mode | The frame renders through | Flies |
|---|---|---|
| Edit, **Editor** (the default) | the editor's viewpoint | yes |
| Edit, a scene camera from the box | that camera, active or not, as a read-only preview | no |
| Play | the game's camera; the box reads **Game** | no |
| Play, ejected (F8) | the editor's viewpoint, or a previewed camera | unless a camera is previewed |

The camera looked through draws no marker and takes no gizmo, since moving it would move the
view. Which camera the game starts on is the Camera card's **Set as Main Camera**, and
**Create > Camera** makes the new camera the game's only when the scene has none. Each scene's
viewpoint is kept per project (`EditorState::sceneViews`); a scene with none is framed from
its active camera.

A Hierarchy row's eye and lock take an entity out of the editor's view and keep a click in
the view off it. Both are this session's alone: never saved, never the game's, and dropped when a
scene opens. Stop and a script reload keep them, as they keep the undo history, only when every
entity comes back under its id. Holding Alt as a gizmo handle is taken drags a copy of the selection and
leaves the originals where they stood: one step for the copy, one for the drag.

Scaling an entity, by the gizmo or the Transform card, writes the scale into its `Collider`'s
parts in the same step (`rescaleParts`), since physics reads no `Transform` scale: a box's
collider grows with the box, each selected entity's by its own scale. A child's collider keeps
its size when its parent is scaled, and a clip that poses a scale leaves the collider alone, as
it does in a game.

## A play session owns the scene

Play runs the game on the scene as it stands, and Stop puts the authored scene back as Play
found it - every entity under the id it had, the selection and the undo history. Every panel stays
live meanwhile, but:

- **Save is refused**: it would write a scene nobody authored.
- **Undo reaches only what the session did.** The session runs on a history of its own, so
  Ctrl+Z never reaches an authored step, and Stop says so when it throws away an edit made in
  play.
- **Stop restores the scene, its materials and its terrains, not other assets.** A mesh,
  texture, rig, clip or sound edited in play keeps the edit. A terrain comes back from the
  file Play wrote it to, so Play is refused when one cannot be written.
- **Ending a session restores the render settings** a game's settings screen may have
  changed - by Stop, or by opening a scene or a project during it.
- **A component copied in play pastes after Stop.** Tune a value while playing, Copy the
  component from its card's menu, Stop, then Paste Values. Only the authored fields travel,
  never the run's state - a voice started, a body asleep.
- **A Stop that cannot restore keeps the session**, paused, so Stop can be pressed again.

**The viewport is the game's.** With the pointer on it, or the cursor grabbed by the game,
keys and buttons go to the game and no editor shortcut acts: Ctrl+1 picks a weapon rather
than hiding the Hierarchy. Still live are **Toggle Editor** (F5), **Play / Stop**, **Pause /
Resume**, **Eject / Return** and the playbar. The game reads the frame's input before the
editor does, so a game that binds one of those keys sees the press too. **Eject** hands the
viewport back: the running game renders through the editor's viewpoint, which flies, picks
and edits as in Edit mode while the game hears nothing. Stop ends an ejection with the
session.

While the game holds the viewport, the playbar and the view bar sit in a band above the game's
view rather than over it, so they cover nothing a game draws at the top of its screen.

The playbar's speed (`Clock::setHostScale`) multiplies the game's own time scale, never
writes it, and outlives the session.

## Prefabs

A scene stores a prefab instance as a reference, a pose and its overrides, not its entities
([io.md](io.md#prefabs)), so anything done inside an instance that is not an override would
not be saved. The editor makes each gesture mean what it looks like, or refuses it.

### What an instance will not let you do

- **Duplicate and Paste instance the prefab again**, overrides included. A copy that would
  land inside an instance stands at the root, where it stood in the world.
- **Re-parenting into or out of an instance, and Create or Group inside one, are refused**
  with a toast; the root itself moves anywhere. One rule decides them all (`refusesNewChild`,
  `copy_parent.h`).
- **Add Component inside an instance works and warns**: saving the instance over its file is
  how a component is added to a prefab.
- **An entity picker is disabled inside an instance** - a Joint's Connected, a slider's Knob:
  an override is read in the prefab's numbering, and a pick names an entity in the scene's.
- **Revert to Prefab on a component the prefab no longer defines is refused**, and the
  override kept.
- **Undo of a delete** brings the instance back whole, its overrides included.

The **Prefab Instance** card, on any entity of an instance, offers what an instance is for:
**Apply to Prefab** saves the root over its file, so every instance takes the overrides;
**Revert All** drops them; **Unpack** keeps the entities and drops the reference, so the
scene stores them.

### Save as Prefab

Writes the selected subtree to the project's `prefabs/` and makes it an instance of the file
(`EditorActions::saveAsPrefab`). An instance saves back over its own file, which is how a
prefab is edited; anything else takes the first free name, which the toast names, so no
collision overwrites a file. The assets it names are cooked first, as a scene's save cooks
them. It is refused inside an instance: save the instance's root.

The save drops exactly the undo steps that name the subtree (`CommandStack::forget`): the
scene now stores a reference, so such a step would undo to a value no longer stored.

## Asset browser

**The Assets window is the project's library**, by name: what the session holds and, dimmed,
what the library lists that it does not - assets an earlier scene imported. Using one -
opening, dragging or assigning it, or picking it in a slot - loads it, and what a scene uses
goes into its `assets` block on save, so the scene loads it again. Fonts are not in it: a
project's fonts are a list in its Project Settings, baked by each host rather than loaded with
a scene. A double-click on a tile, or **Open** in its menu, opens the asset in a window of its
own.

**"In use"** - a tile's tooltip, the **Unused** filter and Delete's guard - is what a scene
save would name (`SceneAssets::collectAssetRefs`), plus what the project's loading screen
shows and what other assets hold: a terrain's layers and plants, a material's maps, the
skeleton a mesh or clip names. **Select Users** selects the entities that name an asset.

**Deleting an asset asks first instead of offering undo**: an asset added again takes a new
slot, so every handle to the old one stays dead. For the same reason it drops the undo history
and the copied component, either of which could hold such a handle.

**Import.** A kind's first button is its primary verb: **New** for materials and terrains,
**Import...** for the rest. Meshes, skeletons and clips all come out of one model file
([resources.md](resources.md#importing-a-model)). A texture imports as color; data and normal
maps come with their model, or through a material's slot, which knows the usage. A file
dropped on the editor's window imports as its kind would, after a copy into the project's
`assets/` when it lies outside; a file already there with the same bytes is used as it is.

## Where Create puts a new entity

Where the view is looking (`ViewPlacement::placeInView`): on the first visible surface at the
view's center, standing on it; else on the ground plane when that is near; else ahead at the
view's focus distance. A placed prefab or effect goes there too. A camera takes the view's own
pose, so it starts out seeing what the author sees. A terrain and a sea stay at the origin, as
ground or water for the whole scene. **Create Child** makes the same kinds under an entity,
at its origin.

**Primitives share their mesh and material.** A generated mesh has a fixed name
(`mesh:generator:cube`), so three cubes share one mesh and one `material:default`. A material
of its own comes from the Mesh card's **Duplicate** or the Assets window's **New**, each a
copy: renaming the default would pull it from under everything that names it.

### What Create > Effect brings into the project

The engine ships effects - an explosion, a campfire, a torch, a plume of smoke - as a pack
laid out as a project is, under `Roots::engineContent()`. Choosing one copies each file
of the pack the project lacks into the project, then places its prefab
(`EditorActions::placeEffect`). **The project owns its copy**: it cooks, ships and edits it as
its own, and a file it already has is never overwritten, so an edited effect stays edited and
an engine update does not reach it. Delete a file to take the engine's again.

## Unsaved changes outlive a crash

While a scene has unsaved changes outside a play session, the editor keeps a copy of it, and
of its terrains' unsaved strokes, in the user's folder, never the project's
(`session/scene_recovery.h`). A save, leaving the scene or a clean close removes the copy; a
crash leaves it. Opened again with a copy newer than its file, the scene offers to **Recover**
it, **Discard** it, or **Cancel**, which keeps the copy, writing no newer one, until the scene
is saved or left. The copy changes nothing the project already holds: a material's
unsaved values are not in it.

## Where settings live

- **What a scene has** - its environment, sky, fog, acoustics, waves and physics - is cards
  of the **World** row at the top of the Hierarchy, shown while nothing is selected. It is
  scene data, saved and undone with the scene.
- **How the project draws** is **Window > Render Settings**, written into `project.json`'s
  `render` block as each edit ends; the rest of `project.json` is **File > Settings...**. Both
  are outside the undo history.
- **How one person likes the editor** - the camera's feel, snap steps, the display, the keys -
  is **Edit > Preferences**, and the dock layout is ImGui's own `imgui.ini`; both follow the
  person across projects. Vsync, the frame cap and the window mode stand aside during a play
  session, where the game sets the window up its own way. Which windows show, the
  **View > Show** choices and each scene's viewpoint are the project's
  ([io.md](io.md#which-root-owns-a-path)).

## Building from the editor

New Project, **File > Build Scripts** and **File > Package Game** run `vkm new`, `vkm build`
and `vkm package` through the launcher beside the engine (`BuildController::launcher`): the
editor never builds itself. One runs at a time, and its output fills the Console's **Build**
view, which can stop it: asked to end first, on Windows as on POSIX, then ended
(`Platform::ChildProcess::stop`).

The editor reloads the gameplay module whenever a new build lands, however it was made, once
the build stamps it whole (`game.built`, beside the module). In a play session the reload
stops the session and plays again on the new code. A project that opens with no module is
built once, so its code runs without a terminal.

---

## How it works inside

`EditorSystem` is an ImGui shell registered on `SystemStage::Editor`, after Render, so its
frame lands over the drawn scene. It owns the panels, the layout, the undo stack and scene
I/O, and it is the one system that holds other systems
([architecture.md](architecture.md#what-everything-else-stands-on)). Panels change the world
through `EditorContext` - the frame, the editor's state and the services a panel may use -
never through each other.

Every panel is an ImGui window that docks, tabs or floats; multi-viewport lets one leave the
main window, drawn through `EditorRenderHooks::renderImGui`. The Viewport alone stays in the
main window, since the renderer draws the scene into its framebuffer. A window's name is what
`imgui.ini` keys its place by, so a name spelled in more than one place - the dock layout's, a
menu's focus - is a constant in `ui/window_names.h`.

### Where things are

Every directory under `src/editor/` is one responsibility:

| Directory | Holds |
|---|---|
| *(root)* | `EditorSystem` and the shortcuts it dispatches (`EditorShortcuts`); `EditorState`, a hub of owners - the `Selection`, the one-shot asks (`Requests`), `Toasts`, `SceneActions`, the `Workspace`, the `Preferences` and the copied entities (`EntityClipboard`); `EditorContext` |
| `actions/` | What the menus and shortcuts do, a file per subject: creating entities and the Create menu that lists them, copy and paste, prefabs, assets, components, what a dropped asset does (`asset_drop.h`), and the edits on the selection (`edit_actions.h`); what a copy of an entity is (`entity_copy.h`), and the prefab-interior rule every one checks (`copy_parent.h`) |
| `undo/` | `Command`, `CommandStack`, the commands, the edit helpers and prefab overrides. It reaches the editor through `CommandHost`, so it runs without a window |
| `session/` | The scene's session: its I/O and Play, the play snapshot, the unsaved-changes guard, the crash copy. `project/` is the open project - opening it, builds, the reload watch, content packs - and `asset/` the project's assets as the editor imports and writes them |
| `preview/` | Rendering previews of assets: the cache that holds them, `PreviewViewport` |
| `panels/` | A panel each, its header and source owning only its own widget state; the Inspector, the Console and the Asset Browser are folders, the Inspector's cards a file per Add Component heading |
| `overlays/` | Drawn over the viewport: the strips' frame (`overlay_strip.h`), the tool strip and view bar, the playbar, the navigation axes, the viewport's notices, the terrain brush; the gizmo and its markers in `gizmo/` |
| `chrome/` | Drawn around or instead of the panels: menu bar, status bar, default layout, toasts, the start screen, and the menus' dialogs (`dialog/`), drawn after the menu bar rather than in it, so a closing menu does not take them |
| `input/` | The key binds, and `InputOwnership` |
| `view/` | The editor's view of the scene: `CameraControllerSystem`, `ViewFraming`, `ViewPlacement`, the triangle pick, the viewport's projection |
| `asset_editors/` | An asset's own window: `AssetEditors`, and an `AssetEditor` per kind |
| `ui/` | The widget vocabulary: a reflected struct's rows (`Rows`), the widget each value type takes, dialogs, the file picker, the windows' names (`window_names.h`); the editor's look in `style/`, picking, dragging and naming an asset in `asset/` |

What needs no window is kept free of ImGui, so `tests/editor/` checks it headless: the
commands (through `CommandHost`), `InputOwnership`, `ViewPlacement`, `ViewFraming`,
`PlaySnapshot`, `SceneRecovery`, `LogTail`, `copyParent`, `clickTarget`, `dragRect` and the
triangle pick. A new rule of that kind goes beside them, not inside a panel.

The editor's own settings are reflected structs - `Preferences`, `Workspace`,
`GizmoVisibility` - written whole by `editor_settings.cpp`, so a new setting is a field: its
range, its tip and its place in the file follow from its `VKM_F`, and its Preferences row names
it (`PreferencesPanel`).

### The editor's frame

`EditorSystem::update` runs at the Editor stage, after Render, so its ImGui frame lands over
the drawn scene. Before that frame opens it applies the UI scale (the frame is laid out
against the theme) and the Preferences, services the frame - the module watch, the build, the
crash copy - resolves a waiting `SceneActions` request and decides who owns input. Whatever
replaces the world - an open, a recovery, a project switch, Stop, a reload - happens there, and
a gesture inside the frame only asks for it (`Requests`, or a `SceneActions` request): a world
replaced with a window on the ImGui stack frees the previews' textures that the frame has already
drawn with. Stop pauses the clock as it asks, so the game's systems, which run before the editor's
next frame, tick no more. A `SceneActions` request to be carried out then pauses it too, once
nothing more in the frame can ask; one waiting on Unsaved Changes does not
(`SceneActions::pauseForResolve`).

Under the splash, or a loading screen covering the whole window, it stops there: no ImGui
frame is begun, because ImGui's first frame builds its shader program, which would wait
behind the renderer's compiles and stall the splash. Otherwise come the transport and Toggle
Editor keys, the dialogs - drawn shown or hidden, as a Save the unsaved-changes prompt answers
may wait on a name - and, while the editor shows, the shortcuts, the workspace and its panels,
and the floating windows. Last, with no button held and no item active, it closes the undo
gesture (`CommandStack::endGesture`).

### Who has the pointer and the keyboard

Decided once a frame, before the ImGui frame opens, from the hovers the last frame drew
(`InputOwnership`, `input/input_ownership.h`), and held in `EditorContext::input`. The
shortcuts, the gizmo, the picker, the navigation axes and the capture told to the engine all
take that one answer, so no two of them act on one press. A grabbed cursor outranks
everything, then a gizmo drag, then the panels, the viewport's overlays, a gizmo handle, the
game's blocking UI and last the scene (`resolveInputOwnership`).

No keybind answers while a field is typed into, a menu, popup or dialog is open, or
Preferences waits for a key (`InputOwnership::keysHeldByUI`), nor while an edit is held under
the button - a gizmo drag or a terrain stroke whose step is not yet pushed - Play and undo
included (`InputOwnership::held`).

Every editor-wide shortcut is a keybind in `input/editor_keybinds.h`, dispatched by
`EditorShortcuts::process`. That does not run while the editor is hidden, so
`EditorSystem::update` reads Toggle Editor and the transport keys before it
(`PlaybackBar::processKeys`). A panel's own keys act while it has the keyboard: the Console's
(`EditorState::consoleHoldsKeys`, so Ctrl+C copies its line, not the entities) and a
preview's (`EditorState::previewHovered`), each set as the panel draws and read by the next
frame's shortcuts.

### What the editor tells the engine

The scene renders into the window's backbuffer inside the viewport's rect; the Viewport window
has no background. Once a frame the editor states on `HostChrome` (`ctx.chrome`) what an
authoring host may say about the frame:

- the viewport's rect, which `RenderSystem`, `VisibilitySystem` and `UISystem` read;
- whether its panels hold the pointer and the keyboard, which `InputMap::update` reads
  ([input.md](input.md));
- the slots the Hierarchy's eye hides, which `VisibilitySystem` leaves out only while the
  world is authored, so they are never the game's;
- the effects previewed while the world stands still (`HostChrome::effectPreview`), which
  `ParticleSystem` plays without writing a component;
- that the next world comes in under the loading screen (`HostChrome::coverNextWorld`).

A runtime writes none of these, and the defaults are what a shipped game wants. The world is
open to edits (`HostChrome::authoring`) in Edit mode, paused, ejected, or with the panels
shown. Fixed meshes (`Mesh::movable` off) keep nothing then
([visibility.md](visibility.md#fixed-meshes)), so the work they save shows only in a running
session with the editor hidden (**Toggle Editor**) or in `vkm_runtime`.

The editor's eye is the other host product: `CameraControllerSystem` publishes the
`EditorViewpoint`, or the camera previewed, as `ctx.hostView` at the Input stage
([visibility.md](visibility.md#where-the-view-comes-from)), and the picker, gizmo and markers
project through `ctx.visibility`'s matrices, so they follow whatever the frame rendered
through. Only `vkm_editor`'s `main()` registers that system, after `setupEngineApp`, and its
fly bindings (`CameraActions`) live in an `InputMap` of its own, never the game's: an ejected
session flies while the game hears nothing.

### Opening a project

The project opens from `EditorSystem::init`, through the same `ProjectController::open`
(`session/project/project_controller.h`) that File > Open Project runs, so a switch re-roots the
editor in place with no restart. With no argument, a `vkm_editor` shipped beside a project
opens it; a path naming a file opens the project it is in. When none opens, the editor draws
the start screen and no workspace, since every workspace path would resolve against the
engine's own directory.

`open`'s steps are in order because each composes paths or runs code the one before put in
place; its declaration lists them. It boots the world through `bootProjectWorld`, as every
host but `vkm_cook` does, and an entry scene that will not load opens the default scene with
an error toast - the editor is where you fix it
([io.md](io.md#what-each-host-does-when-a-project-will-not-open)).

### Actions that throw the live scene away

Quit, New Scene, Open Scene and Open Project all come in through one door, which neither
checks dirtiness nor acts, so a new destructive action cannot forget to ask:

```cpp
state.sceneActions.request(SceneActions::Kind::Open, path);
```

`SceneActions::resolve` (`session/scene_actions.h`) carries the request through the
unsaved-changes prompt once a frame, before the ImGui frame opens, because rebuilding the
scene must not happen with a window on the ImGui stack. A request made while another waits is
ignored, so a held key or a second click cannot stack prompts.

### What an open does to the session's imports

An open is a clean break - the undo history, the selection, the previews and a fresh
`ResourceManager` - since entity ids and handles do not carry across worlds. A load, a recovery
and New Scene go through `SceneSession::openWorld` and end in `afterSceneReplace`, which ends
any play session too (`PlaySession::end`): a snapshot outliving its world would let Stop put
that dead world back over the new one. Open Project empties the world through it first, then
boots the project's. A read that fails leaves the outgoing world, its path and the editor's
state as they were, but a play session, whose behaviors the open has ended, stops. An import
the outgoing scene never named - a sound not yet assigned - has nothing to be made again from,
and is dropped: the count goes in a toast, the names in the log. What the project's loading
screen names is built again from its file.

**Stop is the opposite**: it puts the session back exactly as Play found it. `PlaySession`
(`session/play_session.h`) runs the session - its snapshot, the ejected view, Edit mode put back
at its end - and `SceneSession::stopPlaySession` puts its world back, as an open replaces one. `PlaySnapshot`
(`session/play_snapshot.h`) holds the scene document, every asset, the slots each prefab
instance's entities stood in, every slot's generation (`Scene::slotGenerations`), every
reference to an entity already deleted, which the document holds as none, the world's epoch and
the dirty flag, and, while the session runs, whether the editor's view is ejected and the cursor
the game set; it needs nothing of the editor, so `tests/editor/play.cpp` covers the round
trip. Play parks the authored history (`CommandStack::park`), with the returns noted in it, and
the session runs on an empty one. Stop merges the snapshot's assets back into the same graph,
since undo steps hold handles into it, and reads the scene back under the ids it had at Play:
each slot starts at the generation it had (`Scene::startSlotsAt`), so a slot then free hands out
no id it gave before, and a reference to a deleted entity is written back, for an undo that
brings the entity back to point at it again. It restores the history and its returns as they
were, and the selection, the eye, the lock and the copied entities' parents by their ids, less
any the world it put back does not hold, as a slot free at Play hands out again the ids play took
in it. A world the game read in play numbers its ids afresh, so after one the selection, the eye
and the lock come back as Play found them (`PlaySession::marksAtPlay`), and only a copy taken in
Play's world keeps its parents. When an instance could not return to its slots
(`PlaySnapshot::restoredInPlace`), Stop drops all of these with a toast.

### Undo / redo

Every mutation goes through a `Command` holding the before and after state. The caller has
already applied the change, so pushing records it; undo and redo replay one side. The stack
holds `CommandStack::HISTORY_LIMIT` steps and is cleared when a scene opens.

- **A component's value** goes through `EditScope<T>` by default, `pushEdit` for a gesture
  spanning frames, or `editStep<T>` for a step that joins others (`undo/component_edit.h`). Each
  makes a `ComponentEditCommand<T>` or, inside a prefab instance, a `PrefabOverrideCommand`, so a
  call site never chooses. Both, and the snapshot an undone delete restores (`EntitySnapshot`),
  cover every value row of the scene format (`VKM_SCENE_VALUE_COMPONENTS`): a new component
  needs nothing here.
- **Everything else** has its own command: scene values and scripts in
  `undo/component_commands.h`, entities, subtrees, reparents and prefabs in
  `undo/entity_commands.h`, assets in `undo/asset_commands.h`; `CompositeCommand`
  (`undo/composite_command.h`) makes several steps one.
- **A gesture is one step.** `CommandStack::push` merges into the top step
  (`Command::tryMerge`) only while the gesture is open, and `EditorSystem` seals it once no
  button is held and no item is active, so a second drag of the same field is a second step.

The rules every command keeps:

- **Never create an entity outside the history**: freed slots are reused last-in first-out,
  so it lands in the slot a deleted entity's undo is waiting for.
- **A step names its entity by slot** (`liveEntity`, `undo/entity_snapshot.h`). An undone
  delete, or a redone create, comes back in its slot under a new generation, so an id held from
  before would never match again; a reference another entity holds to it, a Joint's other body,
  is pointed at it again (`SubtreeSnapshot::apply`), whether that entity stands or comes back
  too, before it or after, and so is one a step writes back later, an edit's or a component's
  (`followReturns`): the stack keeps the id each came back as (`CommandStack::returnOf`) while
  the world it came back in stands; a load or a clear replaces that world (`Scene::epoch`), its
  ids numbered afresh, while Stop puts back the one Play found under its own ids, and the
  returns noted in it with it (`CommandStack::unpark`). Last-in first-out makes the slot safe: a
  step runs only once every later one is undone, when its slot holds what the step was made
  against. Steps that destroy by slot check it still holds their entity
  (`EntitySnapshot::describes`), and a mismatch skips the step with an error toast.
- **Merging reaches only the top step**, so an edit that changes two things a frame - the
  Animation editor's `Animation` and the pose it writes - pushes one `CompositeCommand`, not two
  steps that would never merge over the drag. A composite pushed again under its label in one
  gesture merges in step by step, so no two steps of one group may edit the same component of
  the same entity.
- **An asset's value goes back through `ResourceManager::swapValue`**, which bumps the version
  previews and caches watch; a value assigned by hand would change the asset but not its
  picture. An asset's step does nothing once its handle is dead, since deleting an asset is not
  undoable. A step may hold the `ResourceManager` by pointer: the `Engine` destroys it after
  every system, so it outlives the history.
- **What an edit leaves to settle** on the live component - a body woken, a mesh collider's
  triangles rebuilt - is `settleEdit`'s (`undo/component_edit.h`), run before the step is
  cut, so the step holds it. A component with such state adds an overload.
- **A cache over the scene keys on `CommandStack::revision`**, which moves with every push,
  undo and redo, beside `Scene::epoch` and `ResourceManager::revision`. The revision misses a
  play session, which changes the world outside the history, so a cache walks every frame
  there; it misses too an edit an open step takes in without a push
  (`CommandStack::mergeTarget`).

### Inspector cards

A component card is its `cardInfo` (`panels/inspector/card_info.h`) and `drawCard` (`cards.h`),
one pair for every value row of the scene format (`VKM_SCENE_VALUE_COMPONENTS`), so a component
with no card does not link: it is authored in the editor as soon as it is in the scene format.
The Script row's card, declared beside them, is `drawScriptCard`: its behaviors, each a fold of
its fields. The panel draws them in the list's order, each edit one undo step that records
prefab overrides; `cardInfo` puts a card in Add Component under its `AddSection`.
`Hierarchy` has no card - parent and children are the Hierarchy panel's - and a behavior is
added from the Behaviors section, which gives the entity its `ScriptComponent` in the same
step.

**A control over session state pushes no edit.** What a card plays, scrubs or previews is
state the scene never saves, so it is no undo step and no unsaved change:

- **Animator**: play, stop and the scrubber write `Animator::playing` and `Animator::time`;
  **Play On Start** is what a shipped scene does. An `Animation`'s playhead is the exception:
  it poses the entity through `AnimationSystem::poseAt`, the function the system
  samples with, and outside play that pose is the authored `Transform`, so it is one
  **Scrub Animation** step per gesture.
- **Audio Source**: the audition plays the clip through the device, not
  `AudioSource::playing`, whose write would be a scene edit
  ([audio.md](audio.md#the-cursor-is-the-devices-not-the-components)).
- **Particle Emitter**: Play, Pause, Restart and Stop preview the selected effects, or all of
  them, while the world stands still, through `HostChrome::effectPreview`. `ParticleSystem`
  says how it plays them; it writes no component, so a preview leaves the scene as it was.

**Reset and Paste Values take the authored fields alone** (`withAuthored`,
`undo/component_edit.h`), so a run's state never lands in the scene. A card owning more
than its component (Ragdoll) has no menu.

**A card grays what the scene, not the author, writes.** With **Procedural Sky** on,
`SkySystem` drives the key light's rotation, color and intensity, so that Light card says so
and disables color and intensity. Both find the light with `findKeyLight`, so they cannot
disagree.

#### A card is rows

A card's rows name the members they edit, in the order the author reads them:

```cpp
void drawCard(Rows<Light>& c, CardContext& card) {
    const bool sun = c.value().type == LightType::Directional;
    c.row<&Light::type>();
    c.row<&Light::intensity>();
    if (!sun) c.row<&Light::radius>("%.1f m");
    if (sun) c.angle<&Light::sourceRadius>("%.2f deg");
    c.row<&Light::enabled>();
}
```

A row's label is its field's name in words (`fieldLabel`), and its range and tip are the field's
`VKM_F` ([scripting.md](scripting.md#fields)), so code and editor say the same words and a row
holds no number of its own; a card's own controls, for what no field reflects (a Ragdoll's build
settings), hold theirs. The member's type picks the widget (`ui/value_widgets.h`); a behavior's
fields go through the same widgets, so a component and a behavior read alike. Every reflected
field of a component or of the scene's settings is named by its card, and the docs suite fails
on one that is not ([building.md](building.md#tests)), so a field added is added to its card.

What an edit reaches is the rows' to say (`Rows::shared`): a row's field is carried to every
selected entity holding the component, while a list, an entity reference and whatever the
card's own code did stay the active entity's.

A card is built from the shared widgets in `ui/editor_widgets.h` - sliders, combos, folds,
list rows, the card's frame and its notes - so a look changed there changes everywhere; a
panel with no reflected struct uses the `prop*` rows.

#### A card names what its component is waiting for

Where the engine can tell a component will do nothing, its card says so in place with a
`cardNote`, in one sentence: `Note::Danger` for "does nothing", `Note::Warning` for "is not
what you think". For example a `Collider` with no `Rigidbody` (physics walks the `Rigidbody`
storage, so nothing collides with it), a dynamic `Rigidbody` with no `Collider`, a UI element
with no `UICanvas` above it, or an active camera that is not the game's eye. The checks live
with their cards in `panels/inspector/`; add one wherever a component can sit silently inert.

### Asset editors

An asset opens in a window of its own (`EditorState::openAssetEditor`), with an `AssetEditor`
per kind - mesh, material, texture, clip - that knows its asset by handle. `AssetEditors`
brings an open one forward rather than opening it twice, and closes it when the asset is
deleted or a scene or project opens. An editor's viewport is a `PreviewViewport`
(`preview/preview_viewport.h`): the backend's preview of its draws
(`EditorRenderHooks::renderPreview`), rendered again only when the view or what it draws
changes. The Assets window's material and mesh tiles render the same way, cached per asset
and version (`PreviewCache`).

### Markers and picking

Everything with no mesh - a light, a camera, a probe, a decal, an emitter, a sound, a joint -
draws an authoring marker through ImGui, and a click picks it by that marker, tested against
exactly the markers drawn that frame (`EntityMarkers::markEntity`). The shapes in the world - a
collider's wires, a camera's frustum, a rig's bones, the selection's boxes - are the engine's,
drawn through `DebugDraw` into the scene ([rendering.md](rendering.md#debug-drawing)); each
frame the editor hands `DebugDraw` a category per **View > Show** kind and the selection as its
focus (`GizmoOverlay::showWorldGizmos`). ImGui keeps what is 2D or answers the pointer: the
markers, the transform gizmo and a UI element's rect handles.

Otherwise a click picks by triangle (`mesh_pick.h`), but a probe, volume, decal or emitter
picks only by its marker, since its box would swallow every click inside it.

### Rules that fail quietly

The compiler enforces none of these:

- **Every mutation goes through an undoable `Command`.** Writing `scene.get<T>(e).field = v`
  compiles, works on screen, and breaks undo; [Undo / redo](#undo--redo) says which call a
  change takes, and its declarations their rules. A step pushed by hand goes through
  `CommandHost::pushStep`, which marks the scene unsaved as `CommandStack::push` alone does not.
- **A command's label outlives the command**: a string literal, or a string of static storage as
  the Inspector's are, never a temporary's `c_str()` (`Command::label` says why).
- **A menu item opens no modal.** A menu closes the frame its item is clicked, taking a popup
  opened inside it; the item asks (`Requests`, `requests.h`) and a dialog drawn outside the
  menu answers.
- **A handle does not outlive its asset graph.** An open swaps in a new graph
  (`ResourceManager::epoch`), which reuses slots and generations, so a handle kept across it
  names another asset: the asset editors close (`AssetEditors`), and a panel looks up the
  assets it made for itself - a preview's shapes - by name each draw.
- **A drag passes `PROP_CLAMP`**, without which Ctrl+click text entry ignores every inspector
  bound. The rows and the `prop*` wrappers pass it; a raw drag passes it itself.
- **Fixed metrics go through `EditorStyle::px(units)`** (`ui/style/editor_style.h`) - design pixels
  scaled to the loaded font; a raw pixel literal is wrong on a scaled display. The exception is
  the ImGui style: `applyEditorTheme(scale)` writes design pixels and scales them itself. A
  scaled number two files must agree on is a named nullary function beside its subject, as
  `OverlayStrip::button()` (`overlays/overlay_strip.h`).
- **Dialogs use `beginDialog` / `dialogButtons` / `endDialog`** (`ui/editor_dialogs.h`), which
  own Escape-cancels and Enter-confirms.

## Key files

- `src/editor/editor_system.h` - `EditorSystem`, the frame
- `src/editor/editor_state.h` - `EditorState`: selection, preferences, requests
- `src/editor/session/scene_actions.h` - `SceneActions`, the unsaved-changes guard
- `src/editor/session/project/project_controller.h` - `ProjectController::open`
- `src/editor/session/scene_session.h` - `SceneSession`
- `src/editor/session/play_session.h` - `PlaySession`; `src/editor/session/play_snapshot.h` - `PlaySnapshot`
- `src/editor/undo/entity_commands.h`, `component_commands.h`, `asset_commands.h` - the undo commands
- `src/editor/undo/component_edit.h` - `EditScope`, `editStep`, `pushEdit`
- `src/editor/input/input_ownership.h` - `InputOwnership`
- `src/editor/view/camera_controller_system.h` - `CameraControllerSystem`, `CameraActions`
- `src/editor/panels/inspector/card_info.h` - `cardInfo`; `cards.h` - `drawCard`
