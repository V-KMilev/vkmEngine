# Editor

The editor is an ImGui-based shell registered as a `System` on
`SystemStage::Editor`. It owns the panel set, the workspace layout, the
camera controller wiring, the undo/redo stack, and the scene I/O
controller. Everything mutating goes through an `EditorContext`
aggregate, so panels do not reach into each other.

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

The scene renders straight into the window's backbuffer, inside the viewport's
rect. The editor states that rect once a frame on `HostChrome` - the
editor-to-engine channel `FrameContext` carries as `ctx.chrome` - and
`RenderSystem`, `VisibilitySystem` and `UISystem` each read it back from there.
The Viewport window has no background, so the frame shows through it, and the
overlays are drawn on top.

`HostChrome` carries the other thing an authoring host knows and the engine
does not: whether the host's own panels, rather than the scene, own the pointer
and the keyboard this frame. `InputMap::update` reads it: while the panels hold
the keyboard every key binding reads as up, and while they hold the pointer
every mouse-button binding, the wheel and the pointer's movement read as
untouched - so the fly camera stops, typing into a field does not walk the
character, and the game UI takes no click. The pointer's position still
travels, and `UISystem` asks the chrome directly for the one thing it answers,
hover. A runtime writes neither, and the defaults - the whole window, nobody
holding anything - are what a shipped game wants.

What the viewport looks through is the third thing, and it is a frame product
rather than a chrome statement: the editor's `CameraControllerSystem` publishes
`ctx.hostView` at the Input stage, and `VisibilitySystem` renders through it
instead of the scene's active camera - see [The editor's view](#the-editors-view).

### The panels dock, and the layout is ImGui's

The editor runs on Dear ImGui's docking branch. The root window holds the menu
bar, one dockspace and the status bar under it; every panel is a window of its
own that docks into the dockspace, tabs beside another, or floats: **Hierarchy**,
**Viewport**, **Inspector**, **Material**, **Assets**, **Animation** and
**Errors** (their names are in `chrome/dock_layout.h`, because a name is what the
ini files a window's place under). Where they are and how big is ImGui's: a
splitter drag resizes, a tab drag moves, and nothing of the editor's holds a
panel size. Multi-viewport is off - a panel dragged out of the window floats
inside it rather than becoming a second OS window.

The **default layout** is the one above. `buildDefaultLayout`
(`chrome/dock_layout.cpp`) cuts it with `DockBuilder` the first time - when the
ini holds no dockspace - and again on **Window > Reset Layout**, which also shows
every panel. Each side is given a fixed size in design pixels (`HIERARCHY_WIDTH`,
`INSPECTOR_WIDTH`, `BOTTOM_HEIGHT`) rather than a share of the window, because the
first frame can come before the window manager has sized the window; a window
too small for it squeezes the side, never the viewport, and the side grows back
when the window does.

A panel does not take the focus when it appears (`NoFocusOnAppearing`): a dock
node shows its focused window's tab, so a group shown again would otherwise open
on whichever of its windows began last rather than on the tab it was left on.

The viewport's overlays draw in a child that is exactly the scene's rect, so they
place themselves the same way whether or not a tab bar sits above it: the **tool
strip** down the left edge, the **view bar** top-right, the **playbar** centred
between them and the navigation axes bottom-right. The strips are one design
(`beginOverlayStrip`, `ui/editor_widgets.h`) with fixed sizes, so each is placed
from the others' in the same frame; a viewport too small to hold the axes clear
of the strips draws none.

**What is shown is the editor's; where it is, ImGui's.** The panel toggles -
Window > Hierarchy, Inspector and Assets, Ctrl+1 to Ctrl+3 - show and hide the
groups the default layout docks together: the Hierarchy; the Inspector and the
Material editor; Assets, Animation and Errors. They are persisted per project in
`editor_settings.json`. The windows have no close box, so a toggle is the one way
to hide one and the one way back.

**The layout is persisted per person, in ImGui's ini.** `io.IniFilename` is
`imgui.ini` in `ProjectPaths::userRoot()`, beside `editor_user.json`, so the
layout follows the person from one project to the next
([io.md](io.md#which-root-owns-a-path)). What is not a layout keeps out of it:
the root window, the toast, the hidden-editor hint and every popup and dialog are
`NoSavedSettings`, and `io.LogFilename` is null, so ImGui writes no log into the
working directory either.

### Who has the pointer and the keyboard

The editor decides it once a frame, before the ImGui frame opens, and every
reader of a press takes that one answer (`InputOwnership`,
`input/input_ownership.h`, carried as `EditorContext::input`): the capture
declared on `HostChrome`, the shortcuts, the transform gizmo, the picker and the
navigation axes. It is decided from the hovers the last frame drew - the frame a
click was aimed in. "On the viewport" is the Viewport window's scene rect being
hovered with nothing of ImGui's above it. The owner is the first row that holds:

| When | Owner | What answers a press |
|---|---|---|
| The cursor is hidden and grabbed, by the fly camera or a running game | `Captured` | Nothing of the editor's: ImGui is told it has no mouse, because an unseen cursor wanders over panels it is not pointing at. The viewport hover is held from before the grab (`nextViewportHover`) |
| A gizmo drag is held | `GizmoHandle` | The drag, wherever it has gone |
| The pointer is off the viewport | `Panel` | A panel, a popup, a floating window |
| Over the tool strip, the view bar, the playbar or the navigation axes | `Overlay` | The overlay |
| Over a gizmo handle | `GizmoHandle` | The handle |
| Over a blocking element of the game's UI | `GameUI` | Selected, unless the game has the viewport - then the game's |
| Anywhere else on the viewport | `Scene` | The picker |

A key is the editor's own UI's before it is anybody's binding: while a field is
typed into, a menu, popup or dialog is open, or Preferences waits for a key to
rebind, no keybind answers it (`InputOwnership::keysHeldByUI`) and the fly camera
hears none of it.

**A play session's viewport is the game's.** While a session runs and the
pointer is on the viewport, or the game has grabbed the cursor, the keyboard and
the mouse buttons belong to the game: no editor shortcut acts, so Ctrl+1 picks a
weapon rather than hiding the Hierarchy. Three things stay live - the
editor-toggle key, the transport keys (**Play / Stop**, Ctrl+P, and **Pause /
Resume**, Ctrl+Shift+P, in `PlaybackBar::processKeys`) and the playbar. The game
still sees a chord's keys on the frame it is pressed, since its input is read at
the Input stage; a pause discards the presses latched for the next tick. With the
pointer on a panel the keyboard is the editor's, and the engine is told so. The
editor's view stands down for the session (`CameraControllerSystem::setActive`):
the viewport renders through the game's camera, and framing, the navigation
axes, the gizmo and click-picking go with it (`InputOwnership::clickPicks`).

**An ejected session is the editor's again.** **Eject / Return** (F8, as live as
the transport keys, or the camera button on the playbar) renders the running
game through the editor's viewpoint, which flies, picks and edits as in Edit
mode, while the game hears nothing - the chrome declares the host holds both
devices (`InputOwnership::hostHoldsPointer`), and the fly camera samples its own
map against what the panels alone hold (`panelsHoldPointer`). A cursor the game
grabbed is freed on the way out, freed again whenever the game grabs it while
ejected, and given back as the game last set it on the way in. Stop ends an
ejection with the session (`PlaySnapshot`). The caption says `PLAY MODE, EJECTED`.

## Where things are

Every directory under `src/editor/` names a responsibility, and a file belongs
to exactly one of them:

| Directory | Holds |
|---|---|
| *(root)* | The state and the seams every other directory reads: `EditorSystem`, `EditorState`, `EditorContext`, `EditorSettings`, and `editor_actions` - the scene mutations the panels invoke rather than write |
| `command/` | Undo: the `Command` base, the `CommandStack`, the concrete commands, `EditScope`/`editStep`/`pushEdit`, `CommandHost::pushStep` - which records an applied step and marks the scene unsaved, the pair every finished edit owes - and the prefab-override bookkeeping a command has to keep. It reaches the editor through `CommandHost` - six methods to implement, not `EditorState` - so the machinery that decides whether an author's work survives an undo can be run, and tested, without a window |
| `session/` | What outlives a frame but not the editor: the open scene (`SceneIOController`), the open project (`ProjectController`), the Play/Stop snapshot and the ejection that lives and dies with it (`PlaySnapshot`), the vkm run in the Build window (`BuildController`) and the material preview cache |
| `panels/` | One file per panel, each drawing one region and owning only its own widget state |
| `overlays/` | What is drawn *over* the viewport: the transform gizmo - its maths, hit tests and drag state as well as its drawing - the tool strip and view bar, the playback bar, the axis navigation gizmo, and the wire primitives they share |
| `chrome/` | What is drawn *around* the panels: the menu bar, the status bar, the dockspace's default layout, and the four dialogs the menus ask for - Import Model, Place Prefab, New Project and Open Project - which draw at the menu bar's scope so a closing menu does not take them with it |
| `input/` | The keybind table and the shortcut dispatch that reads it, who has the input each frame (`InputOwnership`), and the editor's view (`CameraControllerSystem`) with its fly bindings and the framing that moves it (`ViewFraming`) |
| `ui/` | The widget vocabulary every panel is written in: the `prop*` rows, the dialog scaffold, the style and theme, the icons, the asset picker, the audition transport |

## Panels

| Panel               | File                                  | Description                                                                 |
|---------------------|---------------------------------------|-----------------------------------------------------------------------------|
| Hierarchy           | `panels/hierarchy_panel.cpp`          | Entity tree of every entity, a Transform or not - a UI element and a logic-only Script entity are rows like any other; drag a node onto another to reparent (cycle-safe); context-menu Unparent |
| Inspector           | `panels/inspector_panel.cpp`          | The **Inspector** window, docked right. Component editor (an Animation card is a compact summary; keyframes and easing are the Animation tab's); Camera "Set as Main"; Hierarchy Unparent; prefab-instance overrides |
| Animation           | `panels/animation_panel.cpp`          | The **Animation** window. A transport, a three-lane timeline whose keyframe dots drag, and a table per track, all posing the entity as the playhead moves |
| Render Settings     | `panels/render_settings_panel.cpp`    | Render quality tuning: `RenderSettings` (debug view / tonemap / grid / MSAA, texture filtering, GTAO, bloom, shadows, screen-space reflections, probes) the visibility pass's culling thresholds among them; opened from Window > Render Settings |
| Material Editor     | `panels/material_editor_panel.cpp`          | The **Material** window, tabbed beside the Inspector. One material under a live preview: a Base card, a grid of map tiles, and a card per secondary lobe the material actually uses |
| Asset Browser       | `panels/asset_browser_panel.cpp`            | The **Assets** window. One library for every asset kind: a kind rail, a uniform tile grid, and one verb slot per kind (import / create). Materials and meshes render thumbnails, a texture *is* its thumbnail, sounds audition from the tile |
| Project Settings    | `panels/project_settings_panel.cpp`   | Floating window over what `project.json` records: name, entry scene, tick rate, engine version, and the seats and port the game is served with, plus a read-only list of what it replicates; opened from File > Settings... (under the Project heading) |
| Preferences         | `panels/preferences_panel.cpp`        | Floating editor/app settings window (Edit > Preferences, Ctrl+,)            |
| Errors              | `panels/errors_panel.cpp`             | The **Errors** window: recoverable engine failures, newest first, filling the window. A function, not a class, because it remembers nothing |
| Build               | `session/build_controller.cpp`        | The **Build** window: the vkm command running or last run, how it ended, and its output. See [Building from the editor](#building-from-the-editor) |
| Viewport Overlay    | `overlays/viewport_overlay.cpp`       | The axis navigation gizmo, bottom-right of the viewport (click an axis to snap the editor's view) |
| Gizmo Overlay       | `overlays/gizmo_overlay.cpp`          | The transform gizmo's drawing and drag, and the viewport's click-to-pick     |
| Gizmo Drawing       | `overlays/gizmo_overlay_draw.cpp`     | Every `draw*Gizmos` body, plus the selection outline: lights, cameras, probes, volumes, decals, emitters, audio, colliders, joints, skeletons, bounds |
| Viewport Toolbar    | `overlays/viewport_toolbar.cpp`       | The tool strip (tool, space, snap) down the viewport's left edge, and the view bar (shading, camera, Frame All, Focus) in its top-right corner |
| Playback Bar        | `overlays/playback_bar.cpp`           | Top-centre Play / Pause / Step / Stop transport for the simulation; Pause and Step also hold the mixer's voices; frames and captions the viewport while a session runs |
| Start Screen        | `panels/start_screen.cpp`             | What the editor shows when no project is open: New, Open, the projects opened before and copies of the examples this engine ships - and no workspace at all. See [The start screen](#the-start-screen) |

The pieces of **chrome** are not panels - they belong to `EditorSystem` and
sit around the panel layout rather than in it:

| Chrome              | File                                  | Description                                                                 |
|---------------------|---------------------------------------|-----------------------------------------------------------------------------|
| Menu Bar            | `chrome/editor_menu_bar.cpp`          | File / Edit / View / Window / Entity / Help, inside the root window's menu-bar scope. Holds no command state: it reads and writes `EditorState` and forwards scene-file intents to the `SceneIOController`, whose Save-As and Load dialogs it also draws so they stay in that scope |
| Status Bar          | `chrome/editor_status_bar.cpp`        | Bottom edge, stateless: the dirty dot, the selection's parent breadcrumb and position, and the build banner. Drawn last inside the root window, under the dockspace |
| Dock Layout         | `chrome/dock_layout.cpp`              | The workspace windows' names and the default layout `DockBuilder` cuts the dockspace into, on a first launch and on Window > Reset Layout. Holds no state: the layout itself is ImGui's |

### Where world settings live

Scene-global settings are cards in the **World inspector** (pick the
**World** row at the top of the hierarchy): `Environment` (IBL /
skybox), `Procedural Sky`, `Volumetric Fog`, and `Physics` (gravity,
solver iterations - `Scene::physics()`, read by `PhysicsSystem` each
fixed step). They are scene data, so they sit beside the components
rather than in a settings window. Render Settings is the exception: it
is quality tuning rather than world content, so it has its own window.

### The Material editor is docked beside the Inspector

Both edit the properties of what is selected - the entity's components, and the
material it draws with - so they are the same job at two depths, and a material
editor wants height beside the viewport rather than the short, wide bottom row.

There is one way in, `EditorState::openMaterial(handle)`, used by the Inspector's
Mesh card, the Asset Browser's tile and its New verb. It sets the target, shows
the Inspector's group and raises a one-frame `revealMaterial` request, which
focuses the window wherever it is docked, because every caller is drawn before
the window that answers it. It has no close box: it is shown and hidden with the
Inspector (Ctrl+2).

### The Material tab shows what the material is, not what the struct holds

- **Base and Maps are always there.** Type, albedo, metallic, roughness, AO,
  IOR and emission; then the texture slots.
- **The five secondary lobes are cards only while the material uses one.**
  `FEATURES` in the panel carries, per lobe, how to tell it is on (read off the
  asset, not off a flag nothing serializes), how to switch it on at a value that
  can be seen, and how to put every field it owns back - its texture included,
  or the card just turned off would come straight back. `+ Add Feature` offers
  exactly the ones that are off. Transmission carries its volume (thickness,
  absorption), which means nothing without it.
- **A row that cannot do anything is not drawn.** Cutoff appears for AlphaMask
  only, emission Strength once the emission is not black, and Normal Scale and
  Height Scale sit beside the map each of them scales.

Maps are a tile grid in the Asset Browser's grammar (`tileFace`, `tileStrip`).
The six core slots always have a tile; the packed and secondary ones earn theirs
by being bound, and until then sit behind one `+` tile.

### Which material the tab edits

The tab follows the selection, as the Inspector beside it does. A material chosen
by hand - the chooser, an Asset Browser tile, New or Duplicate - is pinned in
`EditorState::materialEditorTarget` and outranks the selection until another
entity carrying a material of its own is picked, so a material nothing uses yet
can be worked on without the next viewport click throwing it away.

Duplicate, Rename, New and Load PBR Folder sit behind one button on the identity
row, beside a chip counting the entities that draw with this material (clicking
it selects them) - the blast radius of every slider under it, since materials
are shared by handle. Duplicate and Load PBR Folder hand the new material to the
selected entity only when it draws with the one the tab shows.

### Working panels vs Preferences

- **The bottom row** is four working surfaces: **Assets**, **Animation**,
  **Errors** and **Build**. Errors lists `EngineErrorLog` entries newest
  first - a script hook that throws, an asset reference a scene load could not
  resolve - with a Clear button; Build shows what vkm printed.
- **Preferences** is a floating window (`Edit > Preferences`, Ctrl+,) with
  `Camera`, `Gizmo` (snap defaults), `Display` and `Keybinds` tabs. It is one
  `Preferences` struct on `EditorState`, persisted whole in `editor_user.json`
  under `userRoot()` rather than with the project, held on load to what its
  controls could have set (`Preferences::bounded`), and put into effect by
  `EditorSystem::applyPreferences` every frame. Both settings files carry a schema
  version, and a file of another version is refused whole, with a warning. The
  window half - vsync, the frame cap and the window mode - stands aside during a
  play session, where a game may set the window up its own way. Outside a session
  an unfocused editor is held to 15 frames a second, or to the author's cap when
  that is lower.

Preferences, Render Settings and Project Settings open through
`beginToolWindow` (`ui/editor_dialogs.h`): floating, never docked.

#### UI Scale (Display tab)

A multiplier on top of the display's own content scale, kept apart from it
because only the content scale changes when the window moves to another monitor;
the editor asks for it every frame, so a monitor change and a preference edit
take one path. The theme is re-applied on a change, because ImGui's own metrics
are absolute pixels; everything the editor draws itself follows through
`EditorStyle::px()`. It is persisted per person in `editor_user.json`, read once
in `EditorSystem::init` before any project opens.

### Animation editor (`panels/animation_panel.cpp`)

Operates on the selected entity. Without an `Animation` the editor is drawn
disabled behind one centred **Add Animation Component** button; a new animation
defaults to a 5 s `length` so the timeline is usable at once.

- The transport row is `clipTransport` (`ui/editor_widgets.h`), shared with the
  Inspector's Animation and Animator cards, then **Set Key** on all three tracks.
  Play/Pause is a *preview* writing the runtime `Animation::playing`; what a
  shipped scene does is the card's **Play On Start**, the serialized flag.
- **Length** (`Animation::length`, serialized): `0` means the last keyframe. The
  timeline spans `max(last keyframe, length)`.
- A timeline with a dot per keyframe on three lanes and a playhead; drag empty
  timeline to scrub, a dot to retime it. `Time` is typed for exact placement.
- Per track: add or replace a key from the live transform, clear, the easing
  (`drawEnumCombo` over `Easing`), and a keyframe table. Re-keying at an existing
  time replaces that keyframe.

The world does not step in Edit mode, so whatever moves the playhead - the tab or
the Inspector card - poses the entity through `AnimationSystem::applyAnimation`,
the function the system samples with. Moving the playhead is session state, but
outside play the pose it writes is the authored `Transform` the scene saves, so it
is recorded as a **Scrub Animation** step that merges over the gesture. In play
the pose is the session's and is not recorded.

### Character cards (Inspector)

All four are ordinary `editComponentCard` sections, so they undo, record prefab
overrides and appear in Add Component like every other component.

- **Animator** - rig and clip pickers, the resolved rig's bone count, and the
  transport. Loop, speed and **Play On Start** round-trip with the scene, so they
  push an edit; play, stop and the scrubber do not, which is safe only because
  `Animator::playing` is not serialized. A clip cooked against another rig is
  called out in red. The clip's markers are listed read-only (they are authored in
  the clip's recipe). Blend state is absent: a crossfade is started from code
  through `Animator::crossFadeTo` and never serialized.
- **Bone Socket** - the bone picker, over the rig of the entity's *parent* (the
  only rig a socket can address), as the skeleton's tree indented by depth; a
  search flattens it to the matches. The Offset is the authored half - the
  entity's `Transform` is the socket's output, rewritten from the bone every
  frame, which the Transform card says. The card names a parent that is not a rig
  and a bone the rig does not carry.
- **Character Controller** - the tuning fields, the live grounded / ground angle
  / move-input readout, and the step the capsule rolls over
  (`radius * (1 - cos(maxSlopeAngle))`). It names the two ways a controller does
  nothing: no `Rigidbody` (or one whose rotation is not frozen) and no `Collider`.
- **Collider** - a shape picker on a single-part collider, the capsule's total
  height spelled out, and for a mesh part its mesh, scale and triangle count. A
  mesh-fitted compound shows its part count; Fit to Mesh rebuilds it.

The hierarchy names an entity carrying an `Animator` a **Rig**, ahead of Mesh.

### Audio cards (Inspector)

Ordinary `editComponentCard` sections. Auditioning needs the editor's audio
device, which the cards reach through the `EditorContext` every card takes.

- **Audio Source** - the clip picker and the clip's length, layout, rate and
  footprint, then gain / pitch / loop / play-on-start and the distance pair when
  spatial. It names a max distance at or under the min, a stereo clip on a
  spatial source, a spatial source with no `Transform`, a scene with no active
  `AudioListener`, and a listener at volume 0. Beneath them `auditionTransport`
  (shared with the Asset Browser's sound tiles) auditions the clip **through the
  device, not through `AudioSource::playing`**, whose write would be a scene edit;
  nothing on the row dirties the scene. Its slider reads
  `AudioDevice::voiceCursor`, which the mixer advances between frames
  ([audio.md](audio.md#the-cursor-is-the-devices-not-the-components)). The
  audition stops when the selection moves. The label beside it reports the
  source's own voice through `AudioSystem::voiceOf` - **Source: playing 12.40s**,
  or **held** under a Pause or a Step.
- **Audio Listener** - active and master volume, plus a listener that is not the
  ear (naming the one `findActiveListener` picked) or one without a `Transform`.

An `AudioSource` entity is a **Sound** in the hierarchy (**2D Sound** when not
spatial); an `AudioListener` one is a **Listener**.

### The identity header says what the other end will see

"Will the other player see this thing" is a question an author asks while
looking at the thing, so the answer sits under the entity's name rather than in
a window of its own: the wire slot and which of its components replicate, or a
warning that it is inside a prefab instance and so says nothing on the wire.
Both ends build such a subtree from the same prefab file into the same slots -
the root replicates and the hierarchy carries the rest, which is right for anything posed
the same way on both ends and wrong for a ragdoll
([networking.md](networking.md#identity-is-the-scene-slot)).

Drawn only for a project that replicates something, and only for an entity
carrying some of it. Most entities are not on the wire, and a line saying so on
every selection is one an author reads past within a day.

### A card greys what the scene, not the author, is writing

The Light card on the scene's key light is the case: with **World > Procedural
Sky** on, `SkySystem` writes that light's rotation, colour and intensity from
the Environment every frame, so a drag on Colour or Intensity there is undone
before the next frame draws and the value the scene saves is the sky's.
Offered like any other light's, those fields would make a working widget look
broken and an authored colour vanish into the file.

So the card says so - one line naming the three fields and where they are
authored - and disables the two it does not own, the same shape the Procedural
Sky card uses for the fields that depend on its own toggle. Which light the sky
is driving comes from `findKeyLight`, the engine's own answer, so the card and
the system cannot disagree.

### A card names what its component is waiting for

A component that cannot work is the editor's worst failure to report, because
the card goes on rendering in full in front of a viewport where nothing happens.
Where the engine holds both halves of the diagnosis, the card says it, in
`EditorStyle::DANGER` for "this does nothing at all" and `EditorStyle::WARNING`
for "this is not what you think it is" - on the card, where the mistake is made.

- **Collider** with no `Rigidbody` - *"No Rigidbody: nothing collides with
  this."* `PhysicsSystem::gatherBodies` walks the `Rigidbody` storage, so a lone
  collider is in no broadphase: it stops nothing and fires nothing.
- **Collider** mesh part with no triangles - *"No triangles: this collides with
  nothing."*
- **Rigidbody** (dynamic) with no `Collider` - *"No Collider: it falls through
  everything."* A static or kinematic body with no shape is inert, not lost, and
  is not warned about.
- **Mesh** with no mesh or no material - *"No mesh: this entity draws nothing."*
  `VisibilitySystem` returns on an empty mesh or material handle, so the entity is
  in no draw list and cannot be framed.
- **Decal** with no material - *"No material: this projects nothing."* It is the
  state `Entity > Create > Decal` hands you.
- **UI Element** with no `UICanvas` above it - *"No UI Canvas above this:
  nothing draws"*: `UISystem` lays out only what it reaches from a canvas.
- **UI Image / UI Text / UI Button / UI Scroll** with no `UIElement` - *"No UI
  Element: nothing to give it a rect"* (`warnNoUIElement`).
- **UI Scroll** whose content fits - *"The content fits, so there is nothing to
  scroll"*, under the measured content and view sizes.
- **UI Text** whose font does not resolve - *"No font named 'x' is loaded"*: the
  font is reached by name and an unresolved one draws nothing.
- **Camera** that is active but not the game's eye - *"Not the game's eye: it
  renders from 'X'."* In a session that shows the game it is named from
  `Visibility::cameraEntity`, the camera the frame went through; otherwise from
  `findActiveCamera`. **Set as Main Camera** stays offered while any other camera
  also claims Active.
- **Particle Emitter** in Edit mode - *"The world is not running - press Play to
  see them."* `ParticleSystem` returns on a zero sim delta, so `Live: N` is 0.
- **Animation** playing while the world is not - *"Held at 1.20s - it advances
  while the world runs."* Said by the card and the Animation panel, both of which
  toggle `Animation::playing`.

### Sliders and combos are the editor's own

Every slider is `sliderFloat` / `sliderInt` and every combo begins with
`beginCombo` (or is `comboList`), all in `ui/editor_widgets.h`; nothing calls
ImGui's directly, so a look changed there is changed everywhere.

### Property rows clamp what is typed into them

`propDrag` / `propSlider` / `propDragInt` / `propDrag3` pass
`ImGuiSliderFlags_ClampOnInput` (`PROP_CLAMP` in `ui/editor_widgets.h`). A
Drag/Slider clamps the mouse to its bounds, but Ctrl+click turns it into a text
field ImGui leaves unbounded, so without the flag every bound in the inspector
would be advisory on the one path that can type an arbitrary number.
`ClampOnInput` rather than `AlwaysClamp`, because `AlwaysClamp` also clamps a
`lo == hi == 0` range, which is how a row with no limit spells "unbounded". The
Camera card holds its clip planes `CLIP_PLANE_SEPARATION` apart: equal planes
divide by zero in the projection.

`pickAsset` offers a **(none)** row above the list, because an empty slot is a
state the editor hands you (`Create > Audio Source`, `Create > Decal`) and
round-trips through the scene file. The script behavior field and the bone
picker draw the same row.

## Undo / redo

Every editor mutation goes through a `Command` that holds the "before" and the
"after" state. The caller has already applied the change when it pushes the
step, so pushing records it rather than performing it; undo and redo replay one
side or the other. The stack is bounded (default 200 entries) and is cleared on
scene load (entity IDs and component topology are not comparable across a swap).

The commands are in `command/editor_commands.h`, each documented there. Which
one an edit takes:

- **A component's value** - `ComponentEditCommand<T>` through `editStep<T>`,
  which swaps in a `PrefabOverrideCommand` inside a prefab instance, where the
  value is the prefab's patched by the overrides. `EditScope<T>` and `pushEdit`
  are the two doors (`command/component_edit.h`). Every component the editor can
  put back is a row of `VKM_EDITOR_COMPONENTS`; `X` rows also get
  `AddComponentCommand<T>` / `RemoveComponentCommand<T>`, explicitly instantiated
  in `editor_commands.cpp` from that one list.
- **The Scene's own values** (`Environment`, `PhysicsSettings`) -
  `SceneValueEditCommand<T>`, pushed by the World cards through `editWorldCard`.
- **A Script** - `ScriptEditCommand`, over the component's serialized form,
  because a behavior list is move-only.
- **Entities appearing or going** - `CreateEntityCommand`, and for a whole
  subtree `DestroySubtreeCommand` (Delete) and its mirror `CreateSubtreeCommand`
  (Duplicate, Import Model). A subtree snapshot carries `PrefabInstance` and
  `PrefabEntity`, so a deleted instance comes back an instance. Either way a
  subtree goes, a selection anywhere in it goes with it. A create outside the
  history would be worse than unundoable: freed slots are reused last-in
  first-out, so it lands in the slot a deleted entity's undo is waiting for.
- **One subtree for another** (building or clearing a ragdoll) -
  `SubtreeReplaceCommand`, which holds both snapshots rather than replaying an
  operation that reads assets and settings.
- **A prefab placed** - `PlacePrefabCommand`, whose redo rebuilds the instance
  from the file into the slots the last build used.
- **Several steps as one** - `CompositeCommand`: multi-selection Delete and
  Duplicate, a gizmo drag over several roots, Bake All Probes, a keyframe edit
  plus the pose it writes, and `pushActiveCamera`.
- **Assets** - `MaterialEditCommand` (parameters only; the name has its own
  command) and `RenameAssetCommand<HandleType>`, one instantiation per kind the
  Asset Browser can rename.

`CommandStack::push` calls `Command::tryMerge` against the top of the undo stack
first; that is where drag coalescing happens - but only while the gesture is
still open. `EditorSystem` calls `CommandStack::endGesture` at the end of every
frame in which no mouse button is held and no ImGui item is active, which seals
the top of the stack. A gesture is a press, a motion and a release, and it is one
undo step; identity alone cannot tell the micro-edits inside one drag apart from
two separate drags of the same field, and without the seal the second drag would
be swallowed by the first. A `CompositeCommand` with the same label inside one
gesture is absorbed step by step, so a per-frame composite is one step per drag.

`Command::addresses(slot)` is the other half: the steps that name an entity say
so, and `CommandStack::forget` drops exactly those. It is the narrow half of
`clear()`, for an operation that outlives part of the history rather than all of
it - see Save as Prefab below.

## Opening a project

The editor edits *a project*, and without one it says so rather than pretending
otherwise. `EditorSystem::init` keeps the answer `ProjectController::open` gives
it: on failure the editor draws the **start screen** - New, Open, the recent
list and the examples - and no workspace at all, because every path a workspace would compose
would resolve against the engine's own directory. The `engineRoot()` fallback in
`ProjectPaths::projectRoot` is for the other hosts, for which "beside the
executable" *is* the project; the editor asks that a `project.json` exist, so
`vkm_editor` shipped beside a project still opens it with no argument.

`ProjectController::open` (`src/editor/session/project_controller.h`) holds the
one sequence that roots the editor in a project, and re-roots it in place - no
restart. Order matters, because each step composes paths or reads code the one
before it put in place:

1. Save the outgoing project's `editor_settings.json`, while its root is still
   current - otherwise its tuning would land in the project being opened.
2. `ProjectPaths::setProjectRoot(root)` - every path composed after this points
   at the new project.
3. Read its `project.json` into a fresh `Project` - reset first, so a field the
   file leaves out does not keep the outgoing project's value - and take the
   look the game ships from its `render` block, with the editor's own view
   defaults (`EditorSettings::applyViewDefaults`, the grid on) over it.
4. Tear the scene down through `SceneIOController::beginSceneReplace`: behaviors
   get `onDestroy` while the old module still holds their code, and the undo
   stack, material previews, play snapshot, saved-scene path and the whole
   `ResourceManager` go with it - a generated world never swaps the resources
   the way a scene load does.
5. `AssetLibrary::get().load(Truth::Recipes)`, then the project's own editor
   settings, which put this editor's persisted view of the game - the debug
   buffer and the grid - over the look step 3 took. The two sets are disjoint
   (`visitShippedRenderFields` against `visitRenderFields`), so neither
   overwrites the other.
6. Swap the gameplay module to the new project's `bin/`, or unload it when the
   project brings none.
7. Build the wire schema from the new module's `vkmSetupNetwork`, the same entry
   a runtime uses. The editor never hosts and never joins; it does this so
   Project Settings and the inspector can tell an author what the game puts on
   the wire. A project with no such entry gets an empty schema and neither
   surface says anything. File > Reload Scripts rebuilds it too, because
   unloading a module drops what it registered.
8. Boot its world through `bootProjectWorld` - tick rate, scene and the
   scene's fingerprint - the same call both binaries make, and adopt the path it
   opened so that scene is the file this session edits - without it, Save would
   ask for a name for a file the editor had just read.
   A project whose entry scene will not load still opens - the default scene
   stands in, carrying no save path - with an error toast, because the editor is
   where you fix that. The runtime refuses the same project instead; see
   [io.md](io.md#what-each-host-does-when-a-project-will-not-open).
9. Push the project onto the recent list, so the project you are in is in its
   own Recent Projects menu.

A path that names a file rather than a directory still works - `findProjectRoot`
walks up to the owning `project.json`, so dropping in a scene opens its project.

**Command-line `vkm_editor <project>` runs the same sequence**, from
`EditorSystem::init`. Steps 1 and 4 run only while a project is open
(`EditorState::projectOpen`), not merely after startup: the start screen's first
open would otherwise write `editor_settings.json` into the engine root.
`ProjectController::OpenKind` says only whether a path that is not a project is
reported: silently at startup, with a toast when somebody asked. A session that
never opens a project saves only the per-user settings when it quits.
`app/editor/main.cpp` therefore opens nothing itself. See
[io.md](io.md#projects-and-the-three-roots).

**Choosing** a project is separate from opening one. `OpenProjectDialog`
(`chrome/open_project_dialog.h`) draws a path field, File > Open Recent and the
start screen the recent projects, and each hands what it picks to
`EditorState::requestSceneAction` - the same guard New Scene and Open Scene go
through, because opening a project throws the current scene away too.

### Building from the editor

The editor makes, builds and packages a project by running vkm, never by doing it
itself: New Project runs `vkm new`, File > Build Scripts `vkm build` and File >
Package Game `vkm package`, each through the launcher beside the engine
(`BuildController::launcher`, the one that picks vkm's Python). What vkm prints fills
the **Build** window, which comes forward as a run starts and can stop it, the
compilers under it included (`ChildProcess`). A build that changes the module is
reloaded by the editor's watch on it, as one made from a terminal is. A project that
opens with no module is built once, so a new one runs its code without a terminal.

### The start screen

One panel over the dimmed sky, with New Project and Open in its header and two tabs:

- **Projects** - the recents, newest first, with a search. A project whose folder
  is gone sinks to the bottom, faint, until Remove from List; one whose
  `engineVersion` is another minor release (`compatibleEngine`, `io/project.h`)
  wears a "Made for" badge, and opening it asks first, then writes this engine's
  version into its `project.json` - the move a module's build otherwise refuses.
- **Examples** - a card per project in the engine's `examples/`, with the
  `description` its `project.json` gives. Each opens New Project on a copy
  (`EditorState::newProjectTemplate`), never the example itself, which in an SDK
  sits inside the install that the next update replaces.

The menu bar holds File, Edit and Help alone until a project is open.

## Actions that throw the live scene away

Quit, New Scene, Open Scene and Open Project all replace or destroy the world, so
all four go through one entry point:

```cpp
state.requestSceneAction(EditorState::SceneAction::Open, path);
```

A caller says what it wants and nothing else. It does not ask whether the scene
is dirty, does not park the target in a field of its own, and does not perform
the action - which is what stops the next destructive action added from being
the one that forgets to ask.

`EditorSystem` answers it, in `resolveSceneAction`, once per frame **before the
ImGui frame opens**. The request carries a stage:

- **Ask** - nobody has answered yet. A clean scene goes straight to Run; a dirty
  one raises the *Unsaved Changes* prompt, which is drawn in both the visible and
  hidden editor states.
- **Saving** - the prompt's *Save* answer, waiting for the write. It ends any
  play session first, because a save inside one is refused outright and the
  scene the save is for is the authored one Stop puts back. The write landing
  clears the dirty flag and advances to Run; backing out of the Save-As it opened
  withdraws the request instead.
- **Run** - approved. `performSceneAction` does it.

Performing before the ImGui frame is the point: all four rebuild the scene, which
must not happen with a window still on the ImGui stack. Nothing in the editor
opens a project or replaces a scene from inside its own draw.

**File > Exit** goes through the same entry point rather than raising the
window's close flag, which the frame loop reads before the guard could ask. The
titlebar close is intercepted at the top of the Editor stage, withdrawn, and
re-raised by `performSceneAction` once the scene is safe.

## Scene I/O

`SceneIOController` owns the New / Open / Save / Save-As flow. It drives the
file-picker modals and hands off to `SceneSerializer::save` / `load`
([IO and serialization](io.md)). After a successful load it clears the command
stack and restores the editor's viewpoint for that scene
([The editor's view](#the-editors-view)). It ends any play session the outgoing
scene was in, through the `endPlaySession` New Scene and Open Project share: a
snapshot that outlived its scene would let Stop restore that dead world over the
scene just opened, under its name. Every scene it opens or saves goes onto the
recent-scenes list, which `editor_settings.json` persists per project, relative
to its root.

### A play session owns the scene

The values a session turns on - the scene document, the session's whole asset
list, where each prefab instance's entities stood and the dirty flag - are one
object, `PlaySnapshot` (`session/play_snapshot.h`), meaningful only together and
only between one capture and one restore. The controller decides *when*: when to
cook, what to tell the author, and what a restore does to the selection and the
undo stack. A snapshot depends on nothing from the editor, which is why the
play/stop round trip is covered without a window, by `vkm_engine_tests play`.

Play hands the world to the simulation, so what the ECS holds during a session
is the simulation's copy. Every panel stays live, so the editor is explicit about
what an edit made there comes to:

- **Save is refused while a session is live** - greyed in the menu, and a toast
  on Ctrl+S. Writing the played scene over the authored file would store a scene
  nobody wrote and clear a dirty flag Stop puts back.
- **The viewport says which mode it is in**: a live session frames it in the
  warning colour, captioned PLAY MODE - edits are discarded on Stop.
- **Stop says what it discarded**: a session that took an undo step of its own,
  or dirtied a scene that was clean at Play, has authored work in it, and Stop
  warns.

`SceneIOController::stopPlaySession` is the whole of Stop, because the transport
is not its only caller: answering **Save** to the unsaved-changes prompt ends the
session first. `SceneIOController::isPlaying()` is the state itself, and
everything that must not run against the simulation's copy asks it by name - not
the clock, which is paused in Edit mode as well.

### What an open does to the session's imports

An open is the editor's clean break: it drops the undo stack, the selection,
the material previews and any preview through one of its cameras. The asset
graph is replaced by the swap too, so an asset the *outgoing* scene never
named - a sound imported and not yet assigned to a source - has no name in the
new document to be recreated from, and goes with the session that imported it.

**New Scene and Open Project answer this the same way**, through
`beginSceneReplace`: it swaps a fresh `ResourceManager` in (keeping the font
slot, which is engine-owned and never written to a scene) and counts the strays
into the same toast. They throw a whole world away, so the reasoning above
applies to them at least as strongly - and left in place, the outgoing graph
would leak into the seed scene the next New Scene builds: `buildDefaultScene`
takes its cube and default material through `addGeneratedMesh` and
`generateDefaultMaterial`, which reuse whatever the graph already holds as
`mesh:generator:cube` and `material:default` - so the new scene's cube would
wear the outgoing session's default material, edits and all.

That is deliberately the opposite of what **Stop** does. Stop promises to put
one session back exactly as Play found it - the undo history included. The
snapshot is written from these entities at these slot indices and read back
through `createEntityAt`, so every step on the stack still names what it named
before Play, and edit / Play to check / Stop / undo the bad edit is a loop that
works. The one slot the scene document does not carry is where a prefab
instance's own entities stood, because it stores an instance as a reference;
`PlaySnapshot` records that beside it (`Prefab::instanceSlotsOf`) and the
restore builds each instance back into those slots. Without it the instance
would take whatever was free - the slot of an entity deleted before Play - and
undoing that delete would fail while redoing it destroyed the instance's entity.
A panel is live in play mode, so an edit made during one addresses the world
about to be discarded. `captureSnapshot` therefore parks the authored history
(`CommandStack::park`) and the session takes its steps on an empty one: Ctrl+Z
inside a session undoes the session's own edits and stops there, never reaching
an authored step against the simulation's copy. Stop discards the session's
history, says so with a toast if it held anything, and puts the parked one back
(`unpark`). The parked history is dropped instead, with its own toast, when an
instance could not go back into its slots (`PlaySnapshot::restoredInPlace`): a
prefab changed on disk during the session builds other entities. The
selection comes back the same way, by slot and whole; an open, whose entities
are other ones, matches only the active entity, by name.

Beneath both, the steps that destroy by slot - redoing a delete, undoing a
create or a placement, swapping a rebuilt subtree - first check the slot still
holds the entity they were made against (`EntitySnapshot::describes`: its Name
and prefab uid), and a step that reclaims a slot checks it is free. Either
mismatch skips the step with an error toast rather than acting on another
entity.

**The asset graph is kept, not rebuilt.** An undo step holds the asset it is to
put back, as a handle, and a handle is a slot index into one `ResourceManager`.
An open swaps in a graph built from the file it opened, which is why an open
drops the stack; a Stop that did the same would leave the surviving steps
addressing a manager that no longer exists, and since a rebuilt graph restarts
at the same indices and generations those steps would resolve - to whatever
landed in the slot instead. So `restoreSnapshot` reads the snapshot into the
graph it was captured from (`AssetPolicy::Merge`), and puts each asset's
*contents* back in place first - `loadAssets` with `LoadMode::Reload`, over the
`saveAllAssets` document `captureSnapshot` recorded beside the scene - so a
material edited during the session reverts like everything else while its handle
goes on naming it (see [IO and serialization](io.md)).

An open makes no such promise - it
is leaving that world for another one - and carrying the strays forward would
grow the graph by a scene's worth of assets per open and cook every one of them
into the project library at the next save.

What the open does owe the author is the fact. The ones that went are counted
into a toast ("N unused import(s) stayed with the previous scene") and named
one per line in the log, so re-importing them needs nothing but the message.
Only assets the outgoing scene did not name are counted, and only those the new
graph does not already hold - two scenes sharing a sound are not a loss.

## Material preview / Asset browser

### A texture is its own thumbnail

Materials and meshes get a rendered preview (below). A texture does not: the
tile draws the GPU mirror the renderer already samples, and the GPU minifies it.
What it costs is *residency*: `GLView::sync` reaches a texture only through a
material something draws, so a texture nothing binds has no mirror.
`EditorRenderHooks` therefore has two calls:

- `textureId(handle)` - reports the mirror, or 0. Never uploads.
- `ensureTexture(handle, resources)` - uploads if there is no mirror, then
  reports; the first call per texture pays a full upload.

The grid asks the first, and spends `TEXTURE_UPLOADS_PER_FRAME` calls to the
second per frame, so a rail of 4K maps fills in over a few frames instead of
stalling one.

**Known: an sRGB texture's thumbnail draws darker than the file.** ImGui samples
a `GL_SRGB8_ALPHA8` mirror - which linearises - and writes the result to a
framebuffer that is not sRGB-encoded. Linear maps read exactly as authored. The
Material Editor's slot thumbnails take the same path; correcting it needs a
per-image ImGui draw callback, since the material and mesh thumbnails come out
of the composite pass already display-encoded.

### Live PBR previews

The Material Editor and the Asset Browser render previews through the backend's
preview path (`EditorRenderHooks::renderPreview`, backed by `GLPreview`), a
minimal forward + composite render into a small offscreen target, kept apart
from the frame's pass list. Results are cached per asset (handle + version) with
a per-frame bake budget (`MaterialPreviewSession`). The Material Editor's live
view skips the budget but not the version gate: it re-renders when anything it
shows changes. Each kind has its own key space (`previewKey`), and key 0 is the
Material Editor's live pane.

A tile's context menu assigns it to the selected entity - a material or mesh to
its `Mesh`, a sound to its `AudioSource`, a skeleton or a clip to its `Animator`
- through an `EditScope<T>`, the Inspector's road, so it has an undo step and
becomes a prefab override on an instance.

### The browser is a table of kinds, not a template over two of them

The browser is drawn into the Assets window `EditorSystem` begins. The panel is a
`KINDS[]` table of `AssetKind` descriptors, each made by a builder that names its
fields (a field left out is null), and the body that draws the rail, the tiles
and the menus names an asset type only for what one kind alone does: a material
tile opens the Material tab, and a sound tile carries the audition transport. A
descriptor carries a label, a glyph, an `Accent::` colour, its primary verb, and
function pointers: enumerate, describe, preview, assign, rename, delete, and the
walk that proves a delete is safe. The only place a C++ asset type appears is
`KindOps<Asset>`.

**All six of `AssetType`'s kinds are in the table.** A slot is null where the
kind has nothing to put there: `thumb` for sounds, skeletons and clips (the tile
draws the kind's glyph); `assign` for textures, which go into one of a material's
slots no entity can name; `rename` for **skeletons only** - a skinned mesh and a
clip carry the rig's name as a string, so renaming a rig would silently unbind
every one of them. `used` is never null: every kind can say whether a delete is
safe.

**`FontAsset` is not a kind.** `AssetType` leaves it out
(`ASSET_TYPE<FontAsset>` is `Count`) because the library does not hold it: it is
baked once at startup, referenced by name, and has no importer or entity slot.

The browser stays on `ResourceManager` rather than `AssetLibrary::namesOf`,
because thumbnails and assignment need handles: the library is what a *saved*
name resolves against, the manager is what is *loaded*.

### Each kind is a colour, and the strip says use

A rail row wears its own kind's hue, and the rail pairs the kinds that are about
each other; the hues are `EditorStyle::Accent` entries named in `KINDS[]`, never
a status colour. The tile keeps the same strip, and how solid it is says whether
**anything in the project** uses the asset - the same `used` walk the delete
guard asks, so the strip claims exactly what the guard does.

#### What "in use" walks, per kind

What entities reference is not walked here at all. It is
`AssetSerializer::collectAssetRefs` (`io/asset/asset_serializer.h`), the walk a
scene save builds its `assets` block from: the handles of every component that
names an asset (the `R` rows of `VKM_SCENE_COMPONENTS`), the `AssetRef` fields a
behavior authors, and names a load left unresolved. So a component or a behavior
field that names an asset is in use exactly when a save would name it - a clip
only a behavior plays is not offered for deletion. The browser resolves the
names back through `findByName` and marks each kind's slots.

The scene is not the whole project, though. Half of what holds a reference is
an asset, not an entity, and a walk that missed those would offer a Delete that
breaks something far from where it was pressed. So three kinds walk the
`ResourceManager` too:

| Kind      | Also referenced by |
|-----------|--------------------|
| Textures  | all eleven `TextureHandle` slots on **every** `MaterialAsset`, drawn or not |
| Skeletons | `MeshAsset::skeleton` and `AnimationClipAsset::skeleton`, resolved back from their **name** strings |
| Clips     | `Animator::fadeFrom` - session state no save names, but a fading clip is still being sampled |

The texture slots expand from `VKM_MATERIAL_MAPS` (`MATERIAL_TEXTURE_SLOTS` in
the panel), the one list the serializer's field table and the backend's binding
table expand from too, so a map added there is a reference here without an
edit.

### One tile, whatever the kind

A tile is a square face, a name and a one-line detail, drawn by `tileFace` and
`tileStrip` (`ui/editor_widgets.h`), which the Material tab's map grid shares.
The face is a thumbnail where the kind has one and the kind's glyph where it does
not - the rule `editor_icons.h` states for viewport markers - and a kind waiting
on its bake draws the glyph faintly, so "no picture" and "picture coming" differ.

The name is clipped to one line in the **middle** (`elidedLine`), because these
names share their starts and differ at their ends, with the full name in the
tooltip. The detail has a short form for the tile and a verbose one for hover;
the short form carries the one fact that separates assets of that kind:

A mesh says `926 tris . skinned` - a skinned mesh's bind-pose thumbnail can look
like nothing, and the tile says why - and a material its roughness, or its
render path when that is not Opaque, which a preview sphere cannot show.

| Kind      | Tile | Hover |
|-----------|------|-------|
| Textures  | `2048x2048` (or `decoding...`) | channels, usage, levels and size, or what the cooked cache says |
| Skeletons | `24 bones` | and the root bone's name |
| Clips     | `2.00s . 57 ch`, or **`2.00s . no rig`** | channels, the rig, the markers |
| Sounds    | `0.50s . mono` | rate and size |

**`no rig` is the diagnostic the Clips rail exists for.** A clip names its
skeleton by string, and `SkeletalAnimationSystem` throws out a clip whose name
does not answer - so it animates nothing while looking like one that works. The
tile reports the rig it *resolved*, and the tooltip names the one that is missing.
A multi-channel sound's hover says that positioning wants mono, because the mixer
routes each channel to the output channel it was authored for.

### What a tile answers to

Hovering a tile draws a border in the kind's hue, over a thumbnail and a glyph
alike, rather than the theme's button colour, which paints behind what fills it.
The pointer, not a selection, is what an operation acts on: **F2** renames the
tile under the cursor, as the Hierarchy renames the row under its own. Delete is
**not** bound beside it - `deleteEntity` owns that key, and
`EditorShortcuts::process` reads it before any panel draws. The context menu
names its target before it offers anything.

**Rename** goes through `renameDialog` (`ui/editor_dialogs.h`), shared with the
Material tab. `ResourceManager::rename` keeps names unique per type by suffixing
a taken one, and the editor toasts it; the undo command records the name
**assigned**, so redo repeats what happened.

**Delete asks first**, rather than offering an undo, because an asset cannot
come back: re-adding one takes a new slot, so every handle that named the old
one - those on the undo stack included - stays dead. `RenameAssetCommand` guards
`isAlive` for that case. Deleting the clip that is auditioning stops the voice
first, since `AudioSystem` holds its samples by `shared_ptr`.

### One verb slot

The first control in the toolbar is always the chosen kind's primary action -
`New` for materials, `Import...` for the rest - a value in the descriptor table,
not a branch in the toolbar; the formats are in its tooltip. Search narrows the
rail's counts and the grid together, and Escape empties the box rather than
ImGui's default of reverting it.

Materials create; meshes, skeletons and clips raise
`EditorState::requestModelImport`, because all three come out of one model
import; textures and sounds each run an `AssetPicker` the panel owns, so their
popup ids stay unique.

**A texture is imported as colour (`TextureUsage::Color`).** Data and normal
maps arrive with their model, or through the Material Editor slot, which knows
the usage. The import refuses a file the project already holds: `loadTexture`
names the asset by its path, and `ResourceManager::add` under a taken name
replaces that asset in place, under every material using it. A Material tab map
tile binding a file already imported reuses that asset instead, since a filled
slot is what was asked for; a file wanted as another usage is a second texture.

### Auditioning, from the tile

The transport sits on the sound tile's face, the Inspector card's
`auditionTransport`: Play on every tile, and on the tile that is sounding a Pause
and a Stop, with the detail line become the position slider. The face is
submitted with `SetNextItemAllowOverlap()`, or the face button would take every
click on the square.

One voice serves the panel, so a Play replaces whatever was sounding, and the
panel remembers which clip the voice came from as a full handle, so a recycled
slot cannot hand another clip a running transport. An audition does not stop
when the pointer leaves the panel. Pause, Stop and the slider are lit off the
device, not off a remembered id, which outlives the voice it named.

While the Sounds row is showing, the toolbar names the two ways a clip goes
unheard with nothing here wrong: no audio device, and an `AudioListener` at
volume 0. Importing a file the project already holds is answered with a toast
and nothing else.

### What Create > Primitive puts in the asset graph

A generated mesh carries a deterministic name - `mesh:generator:cube`,
`mesh:generator:sphere:32:16` - read back out of its source descriptor
(`generatorName`), and `addGeneratedMesh` and `generateDefaultMaterial` reuse
what the graph holds under that name, so three cubes share one
`mesh:generator:cube` and one `material:default` rather than three copies a scene
would save. A material meant to be its own is made by **Duplicate** or **New**,
which copy the default rather than renaming it: renaming it would take
`material:default` out from under everything that resolves that name.

### When the scene has no camera

The editor's view is its own, so a scene with no camera opens, renders and edits
like any other. A session in one has nothing to render from, and
`VisibilitySystem`'s warning goes to a log the editor cannot show, so
`ViewportOverlay::drawNoCameraNotice` puts **"No active camera - the game has
nothing to render from"** in the viewport, with the routes back - and F8, which
ejects to the editor's view. `Entity > Create > Camera` activates the camera it
creates **when the scene has no active one**; otherwise it creates it inactive,
so a new camera does not take the game's view.

## Transform gizmo

Viewport-space handles for translate, rotate and scale, axis-constrained; a
fourth mode, **Select**, draws no handles so a click always selects.
`transform_gizmo.cpp` holds the whole gizmo: `manipulate()`, its maths, the
visuals, the pick tests and the drag state machine. A drag pushes one step when
it ends: the dragged entity's `editStep<Transform>`, or one `CompositeCommand`
when the selection has several roots.

Handles project through the near-plane test the viewport wires use
(`overlays/wire_draw.h`): a point behind that plane has no screen position, so
its handle is dropped from the draw and the hit test. A click-to-pick and every
drag cast the ray `viewportRay` builds - the pointer unprojected at the near and
the far plane, starting at the near one, not at the camera, whose position an
orthographic camera's parallel rays never pass through. The gizmo hovers and
starts a drag only where the frame's input ownership lets it
([Who has the pointer and the keyboard](#who-has-the-pointer-and-the-keyboard)).

**Snapping** (the tool strip's toggle, or Ctrl held) moves a drag in whole steps
*from where it began*, along the handle held (`overlays/gizmo_snap.h`): only what
the handle moves is snapped, and a value off the step keeps its offset - rounding
the values would drag the other axes onto the grid and round an import at scale
0.01 to zero. A scale stops at one step rather than reach zero.

Tool keybinds, live only while the cursor is not captured: `Q` Select, `W` Move,
`E` Rotate, `R` Scale, `X` Local / World, rebindable in Preferences > Keybinds.

Everything with no mesh of its own draws an authoring gizmo in
`gizmo_overlay_draw.cpp`, so it can be found and placed: lights, cameras,
reflection probes and irradiance volumes, decals, particle emitters, audio
sources and listeners. Lights, cameras and the audio pair mark themselves with a
glyph on a disc at one size, drawn by `drawEntityMarker` (`ui/editor_icons.h`),
which also states the radius the picker answers within. The `View` menu adds
**Show Colliders**, **Show Bounds** and **Show Skeletons** (posed bones from
`FrameContext::poses`, with an axis triad per bone on the selected rig), off by
default because they draw for every matching entity.

## Entity selection and shortcuts

- A viewport click casts against the visible set's world boxes, and each box it
  enters against its mesh's triangles (`overlays/mesh_pick.h`), so a level mesh
  does not swallow a click aimed inside its box; a posed mesh answers by its
  posed box. An entity with no mesh is picked by its **marker**:
  `GizmoOverlay::markEntity` is the one door every marker is drawn through, and
  the picker tests exactly the markers drawn that frame, in screen space at the
  radius they were drawn - so what draws a marker answers a click on one, at any
  distance and under an orthographic camera. A light also records a box scaled by
  its reach. A scene camera the viewport renders through draws no marker and is
  not picked.
- Probes, irradiance volumes, decals and particle emitters are selected from the
  hierarchy only: their boxes run to tens of units and would swallow every click
  inside them.
- The selection is an ordered list - the active entity is the last one clicked -
  with `EditorState::selectedAt` beside it, the same set indexed by slot, so
  `isSelected` is one comparison for the outline and the gizmo colours over
  thousands of entities. Only `EditorState`'s selection helpers write either.
- The inspector shows the active entity. Inside a prefab instance an edit becomes
  a per-instance override, and each overridden field offers "Revert to prefab"
  (`command/prefab_overrides.h`).
- Every shortcut is a keybind in `input/editor_keybinds.h`, dispatched by
  `EditorShortcuts::process`.

## What an instance will not let you do

A scene stores a prefab instance as a reference, a pose and its overrides, and
skips the subtree underneath it - so anything done inside an instance that is
not an override is not written at all. The editor either makes the gesture mean
what it looks like, or refuses it where it happens:

- **Duplicate** instances the prefab again, carrying the overrides over, instead
  of copying the subtree - which would write out as the block of loose entities
  the reference exists to replace.
- **Undo of a delete** restores the marker and the uids with the rest of the
  subtree, so the instance comes back whole with its overrides intact.
- **Re-parenting into or out of an instance** is refused with a toast in
  `EditorActions::reparentKeepingWorld`, which is where every interactive move
  goes. The root itself still moves anywhere: its `Hierarchy` is the scene's.
- **Add Component** on an instance entity works and warns, because saving the
  instance back over its file is how a component is added to a prefab. The
  inspector carries the rule above the button and toasts it on the add.
- **A Joint's Connected picker** is disabled inside an instance, with a line
  saying why. An override is read back in the prefab's namespace and a pick names
  an entity in the scene's, so no override can carry it; a pick would change the
  joint on screen and be gone on the next load.
- **Revert to prefab** on a field whose component the prefab no longer defines is
  refused and the entry kept - there is no value left to give the field back.
- **Save as Prefab** on an entity inside an instance is refused: that subtree is
  not the new file's to define, and writing it would renumber the entity's uid
  and stamp an instance inside an instance. Save the instance root instead.

## Save as Prefab

`EditorActions::saveAsPrefab` writes the selected subtree to the project's
`prefabs/` and turns it into an instance of what it wrote, so the scene stores it
as a reference from then on. Two consequences to know before reaching for it:

- **No existing prefab is overwritten because a name collided.** An entity that
  is already an instance saves back over its own source - that is how a prefab is
  edited - and anything else takes the first free file name (`Enemy`,
  `Enemy 2`, ...). Overwriting a stranger's file would re-point every instance of
  it at a different subtree, so the toast names the file that was actually
  written.
- **A successful save drops the history entries that describe the subtree, and
  only those.** The subtree is the prefab's from then on: the scene keeps a
  reference and the overrides against it, so a step that assigns a component in
  there would undo to a value the scene has stopped storing. Everything else in
  the history is untouched - an entity elsewhere in the scene is not rebuilt
  from anything, and an edit to it an hour ago can still be taken back. (A
  scene load clears the whole stack for a reason that does not hold here: it
  replaces every entity, and this replaces none.) A refused save leaves the
  history alone.

---

## How it works inside

Everything above is what an author sees. What follows is how the editor does
it, for whoever maintains that half.

### The editor's view

The editor looks at a scene from a viewpoint of its own - a position and a yaw
and pitch (`EditorViewpoint`), held by `CameraControllerSystem` - not from an
entity, for three reasons:

- **Looking around is not an edit.** The viewpoint is in no scene file, so a
  fly, a scroll dolly, Frame Selected and a view-axis snap change nothing the
  scene stores, mark nothing unsaved and push no undo step.
- **A scene needs no camera to be edited.** The view renders whether or not the
  scene has one; only a play session needs the scene's (see
  [When the scene has no camera](#when-the-scene-has-no-camera)).
- **The game's camera is the game's.** Nothing of the editor's writes it, so a
  session never has two writers for one Transform.

It reaches the frame through the engine's one authoring seam for it,
`FrameContext::hostView` ([visibility.md](visibility.md#where-the-view-comes-from)):
the controller publishes a `HostView` at the Input stage and `VisibilitySystem`
renders through it in place of the scene's active camera. The picker, the
transform gizmo, the wire overlays and the navigation axes all project through
`ctx.visibility`'s matrices, so they follow whatever the frame rendered through
without asking the controller.

**What the viewport shows**, from the camera box on the view bar at the top-right
of the viewport:

| Mode | The frame renders through | Flies |
|---|---|---|
| Edit, **Editor** (the default) | the editor's viewpoint | yes |
| Edit, a scene camera picked from the box | that camera, active or not: a read-only preview, labelled `(main)` on the one the game starts on | no |
| Play | the game's camera - nothing is published | no; the box reads **Game** |
| Play, ejected (F8) | the editor's viewpoint, or a previewed camera | yes |

A preview ends when its camera stops being a camera with a pose, when the scene
is replaced, and when Frame Selected, Frame All or a navigation axis moves the
viewpoint - each of which shows the viewpoint they moved. A previewed camera,
like the one the game renders through, draws no frustum or marker and takes no
gizmo (`Visibility::cameraEntity` names it), because a drag measured in a view
would move that view. **Set as Main Camera** in the inspector and the hierarchy
is a separate thing: it decides the game's eye, which the editor's view is not.

**Where it starts.** `editor_settings.json` keeps the viewpoint per scene, by
its path relative to the project, so the entries move with it, under
`sceneViews` (`EditorState::sceneViews`); the open scene's is written into it as
the scene is left - a New Scene, an open, a project switch, the editor closing -
and read back when that scene is opened again. A scene with no entry is framed
from its active camera, so the first look at a scene is the one its author set
up for the game, or from `(0, 2, 6)` facing the origin when it has none. A Stop
leaves the viewpoint where it was. An entry for a scene no longer on disk is
dropped on load, as the recent scenes are.

#### The fly controls

`CameraControllerSystem` is a `System` on `SystemStage::Input`, before every
reader of a view: a viewport that resolved matrices from last frame's pose would
lag the pointer by a frame on every drag.

**The editor registers it and no other host does.** `vkm_editor`'s own `main()`
adds it after `setupEngineApp` returns, which makes fly controls an authoring
tool rather than a switch a shipped game could reach: holding the right button
puts the window in `CursorMode::Disabled`, and a game could not decline it,
because `BehaviorContext` carries services and no systems.

**Its actions are in a map of its own.** The fly bindings (`CameraActions`)
live in an `InputMap` the controller owns, sampled each frame against what the
editor's panels hold (`setCapture`), never in the game's. That is what lets an
ejected session fly while the game, told the host holds both devices, hears
nothing - one map cannot answer "held" to the fly camera and "up" to the game
for the same key - and it is why a game's map holds no `Camera/` actions to
take its command slots.

| Input                          | Action                            |
|--------------------------------|-----------------------------------|
| Right mouse button (hold)      | Enable look mode (cursor hidden)  |
| Mouse movement (in look mode)  | Rotate the view (yaw/pitch)       |
| W / A / S / D                  | Move forward / left / back / right|
| Q / E                          | Move up / down                    |
| Shift (hold)                   | Speed boost                       |
| Scroll wheel (in look mode)    | Dolly forward / back              |

Its speeds and sensitivities (`CameraControllerSystem::Settings`) are the
Preferences window's Camera tab, held in `Preferences::camera` and handed to the
controller every frame, so they persist with the person rather than the project. Its bindings are input actions, not
editor keybinds.
