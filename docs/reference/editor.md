# Editor

The editor is an ImGui-based shell registered as a `System` on
`SystemStage::UI`. It owns the panel set, the workspace layout, the
camera controller wiring, the undo/redo stack, and the scene I/O
controller. Everything mutating goes through an `EditorContext`
aggregate, so panels do not reach into each other.

## Layout

```
+---------------------------------------------------------------+
|                          Menu bar                             |
+---------------------------------------------------------------+
|                                                |              |
|                   Viewport (3D scene)          |              |
|                                                |  Inspector   |
|  [Toolbar]                       [Nav gizmo]   |              |
|  [Playback bar (top-centre)]                   |              |
|  [Hierarchy panel docked left]                 |              |
|                                                |              |
+---------------------------------------------------------------+
|        Bottom panel (Assets / Animation / Errors tabs)        |
+---------------------------------------------------------------+
|                          Status bar                           |
+---------------------------------------------------------------+
```

The viewport renders into a dedicated render target; the editor calls
`WindowManager::setSceneViewport(x, y, w, h)` so `FrameContext` carries
the viewport rect, and `RenderSystem` draws into that rect. The result
is presented as an ImGui image inside the docked viewport area, with
overlays drawn on top.

## Key files

| File                                                  | Responsibility                                                        |
|-------------------------------------------------------|-----------------------------------------------------------------------|
| `src/editor/editor_system.h`                          | `EditorSystem` (System subclass; owns the panel set + workspace)      |
| `src/editor/framework/editor_state.h`                 | `EditorState` (selection, gizmo mode, snap, command stack, ...)       |
| `src/editor/framework/editor_context.h`               | `EditorContext` aggregate passed to every panel                       |
| `src/editor/framework/command.h`                      | `Command` abstract base for undo/redo                                 |
| `src/editor/framework/command_stack.h`                | `CommandStack` (bounded undo + redo with merge-on-coalesce)           |
| `src/editor/framework/editor_commands.h`              | Concrete commands: Transform, Add/RemoveComponent, Create/DestroySubtree, Reparent |
| `src/editor/framework/scene_io_controller.h`          | Save/Save-As/Load modal + file pickers, post-load housekeeping        |
| `src/engine/system/camera/camera_controller_system.h`        | FPS fly-cam System used by the editor                                 |
| `src/editor/gizmo/transform_gizmo.h`                  | Transform gizmo (one `transform_gizmo.cpp`: math, visuals, hit tests, drag state) |

## Panels

| Panel               | File                                  | Description                                                                 |
|---------------------|---------------------------------------|-----------------------------------------------------------------------------|
| Hierarchy           | `panels/hierarchy_panel.cpp`          | Entity tree; drag a node onto another to reparent (cycle-safe); context-menu Unparent |
| Inspector           | `panels/inspector_panel.cpp`          | Component editor; animation easing/keyframes; Camera "Set as Main"; Hierarchy Unparent; prefab-instance overrides |
| Bottom              | `panels/bottom_panel.cpp`             | Three tabs: Assets (the Asset Browser), Animation (keyframe editor) and Errors (recoverable engine failures) |
| Render Settings     | `panels/render_settings_panel.cpp`    | Render quality tuning: `RenderSettings` (debug view / grid / MSAA, texture filtering, GTAO, bloom, shadows, probes) plus the `VisibilitySystem` culling thresholds; opened from Window > Render Settings |
| Material Editor     | `panels/material_editor_panel.cpp`          | Per-material PBR inspector with live preview (renders the real pipeline)    |
| Asset Browser       | `panels/asset_browser_panel.cpp`            | The bottom panel's Assets tab. One library for all six asset kinds: a kind rail, a uniform tile grid, and one verb slot per kind (import / create). Materials and meshes render thumbnails, a texture *is* its thumbnail, sounds audition from the tile |
| Preferences         | `panels/preferences_panel.cpp`        | Floating editor/app settings window (Edit > Preferences, Ctrl+,)            |
| Viewport Overlay    | `overlays/viewport_overlay.cpp`       | The axis navigation gizmo, top-right of the viewport (click an axis to snap the camera) |
| Gizmo Overlay       | `overlays/gizmo_overlay.cpp`          | The transform gizmo's drawing and drag, and the viewport's click-to-pick     |
| Gizmo Drawing       | `overlays/gizmo_overlay_draw.cpp`     | Every `draw*Gizmos` body, plus the selection outline: lights, cameras, probes, volumes, decals, emitters, audio, colliders, skeletons, bounds |
| Viewport Toolbar    | `overlays/viewport_toolbar.cpp`       | In-viewport icon tool box: tool/space/snap + selection actions              |
| Playback Bar        | `overlays/playback_bar.cpp`           | Top-centre Play / Pause / Step / Stop transport for the simulation; Pause and Step also hold the mixer's voices; frames and captions the viewport while a session runs |

### A tab bar says which tab is open, not where the pointer is

Selection is the louder state. A hovered tab gets a neutral lift; the open one
gets the accent fill and the accent overline above it, which hover has no
counterpart for. The theme used to have this the other way round - a hovered
tab was painted `ACCENT` at 0.70 alpha while the open one sat in a muted slate
- so resting the pointer on a neighbour made that neighbour the brightest thing
in the bar, and the bar answered "which tab am I on" with the pointer's
position. The overline is the mark that settles it: its colours
(`TabSelectedOverline`, `TabDimmedSelectedOverline`) were already in the
palette while `TabBarOverlineSize` was 0, so none of them were drawn. Tab bars
opt in with `ImGuiTabBarFlags_DrawSelectedOverline`.

### Where world settings live

Scene-global settings are cards in the **World inspector** (select
nothing, or pick the world row in the hierarchy): `Environment` (IBL /
skybox), `Procedural Sky`, `Volumetric Fog`, and `Physics` (gravity,
solver iterations - `Scene::physics()`, read by `PhysicsSystem` each
fixed step). They are scene data, so they sit beside the components
rather than in a settings window. Render Settings is the exception: it
is quality tuning rather than world content, so it has its own window.

### Bottom panel vs Preferences

The editor separates **per-scene working data** from **editor/app
preferences**:

- **Bottom panel** is a tab bar over per-scene working surfaces:
  **Assets** (the Asset Browser below), **Animation** (the keyframe editor
  below) and **Errors**. Assets leads because it is the surface an author
  reaches for most often and the one that wants the width. The Errors
  tab is where recoverable engine failures surface - a script hook that
  throws does not kill the frame, it lands here, and so does an asset
  reference a scene load could not resolve, named by kind and by asset,
  which is the one failure that costs the author a field they had filled
  in - listing `EngineErrorLog` entries newest first with a Clear button.
- **Preferences window** is a floating, closeable window opened from
  `Edit > Preferences` (Ctrl+,). Tabs: `Camera` (fly-cam), `Gizmo`
  (snap defaults), `Display`, `Keybinds`. These are user/app config, not
  scene data. The Preferences gizmo section is snap-only; the active
  tool and Local/World space live on the viewport toolbar.

### Animation editor (Bottom panel, Animation tab)

Operates on the selected entity. When it has **no** `Animation`, the
whole editor is shown disabled (preview of the UI) with a single
centered **Add Animation Component** button. New animations default to
a 5 s `length` so the timeline is immediately usable.

The button is centred on **what is on screen**, not on the ghost behind it. The
disabled editor is taller than the panel at its shipped height, so centring on
the ghost parked the one control the empty state exists to offer below the fold:
the panel looked fully populated and ready, the transport's tooltips answered on
hover, and clicking any of them did nothing. It is measured against
`GetContentRegionAvail().y` taken before the ghost is drawn, which is the same
thing the Inspector's own empty state does.

Controls (icon buttons, shared `editor_icons.{h,cpp}`):

- Playback: Play/Pause, Stop (rewind), global **Set Key** (add/replace
  a keyframe on all three tracks at the current time), Loop, Speed. Play/Pause
  is a *preview* transport writing the runtime `Animation::playing`; what a
  shipped scene does is the Inspector card's **Play On Start**, which is the
  serialized flag - the same split the Audio Source card makes.
- **Length** (`Animation::length`, serialized): explicit animation
  duration in seconds; `0` means auto from the last keyframe. The
  timeline spans `max(last keyframe, length)`, so you can set a length
  and place keys anywhere along it (looping uses this duration too).
- A scrubbable timeline: ruler, per-track keyframe dots (P/R/S),
  playhead. Drag empty timeline to scrub; drag a keyframe dot to retime
  it (hover highlights; cursor switches to resize).
- `Time` is a typed `InputFloat` so you can place the playhead exactly.
- Per track (Position/Rotation/Scale): `+` add or replace a key from
  the live transform, trash to clear, easing dropdown, plus an editable
  keyframe table (XYZ values for pos/scale, Euler degrees for rotation;
  per-row Time editable; per-row delete).

Scrubbing or editing while paused live-previews the pose in the
viewport. Re-keying at an existing time **replaces** that keyframe
instead of stacking a zero-length segment. The Inspector's Animation
section is a compact summary (play/stop, loop, speed, time slider,
track/key counts) that points here for full keyframe editing.

### Character cards (Inspector)

Four cards cover the character components; all four are ordinary
`editComponentCard` sections, so they undo, record prefab overrides and appear
in Add Component like every other component.

- **Animator** - rig and clip pickers over `SkeletonAsset` /
  `AnimationClipAsset`, the bone count of the rig actually resolved, and a
  transport (play / stop / loop / speed / time scrub) that mirrors the Animation
  card: loop, speed and **Play On Start** round-trip with the scene so they push
  an edit, while play, stop and the scrubber do not. Which is only safe because
  `Animator::playing` is runtime state - it used to be serialized, so the
  preview transport quietly wrote what a shipped scene does, and a Pause pressed
  once froze a character that never animated again. Scrubbing works while paused
  because the pose system composes every frame. A clip cooked against a different rig is
  called out in red on the card, where the pairing is being made, rather than
  only in the log. The clip's markers are listed read-only beneath the scrubber
  the way the Animation card lists its keyframe counts - a marker belongs to the
  clip and is authored in the clip's recipe, so what the card owes an author is
  the ability to see what the clip they just picked will announce and when.
  Blend state is deliberately absent: a crossfade is started from code through
  `Animator::crossFadeTo` and is never serialized.
- **Bone Socket** - the bone picker, over the rig resolved from the entity's
  *parent* - the only rig a socket can address, because it is placed relative to
  that parent's world matrix. The list is the skeleton's own bone array indented
  by depth, since a rig is a tree and finding a hand under an arm is not the same
  job as finding it in a hundred flat names; typing in the search box flattens it
  back to the matches. Below it, the Offset (position / rotation / scale) that is
  the authored half - the entity's `Transform` is the socket's *output* and is
  rewritten from the bone every frame, which the Transform card now says out
  loud so an edit that vanishes - typed there or dragged with the gizmo - is
  explained rather than mysterious. The card names the two authoring mistakes
  where they are made: a parent that is not a rig (with what to do about it) and
  a bone name the rig does not carry.
- **Character Controller** - the four tuning fields, plus the live grounded /
  ground angle / move-input readout, which is what answers "why is it not
  jumping". Under them, the step height the capsule rolls over
  (`radius * (1 - cos(maxSlopeAngle))`), because that number answers "why does it
  stop at that kerb" and both halves of it are set on this entity. It also names
  the two ways a controller silently does nothing: no `Rigidbody` (or one whose
  rotation is not frozen) and no `Collider`.
- **Collider** - a shape picker on a single-part collider, showing half-extents
  for a box and radius / half height for a capsule, with the capsule's total
  height spelled out because that is the number an author matches to a model.
  A mesh-fitted compound shows its part count instead; rebuild it with Fit to
  Mesh, which always produces boxes.

The hierarchy names an entity carrying an `Animator` a **Rig**, ahead of Mesh -
an entity with an `Animator` is the rig whatever else it carries, and its meshes
are the entities under it. The hover tooltip's component digest lists `Animator`,
`Socket` and `Character` beside the rest.

### Audio cards (Inspector)

Two cards, both ordinary `editComponentCard` sections, so they undo, record
prefab overrides and appear in Add Component like everything else. They are the
only cards that take the whole `EditorContext`, because auditioning a clip needs
the editor's audio device and that is reachable from neither the scene nor the
asset graph.

- **Audio Source** - the clip picker over `AudioClipAsset`, the clip's length,
  layout, rate and memory footprint, then gain / pitch / loop / play-on-start,
  and the distance pair when the source is spatial. Four authoring mistakes are
  named where they are made rather than left to the log: a max distance at or
  under the min (nothing is attenuated), a stereo clip on a spatial source (its
  two channels already encode a position, so panning one is meaningless at
  best), a spatial source with no `Transform` (heard at the world origin rather
  than where it was placed), and a scene with no active `AudioListener` at all -
  the one the engine cannot report at edit time, since its own warning waits for
  a positioned voice to actually start. Beneath them is a transport that mirrors
  the two animation cards' - play / pause / resume on one button, stop, and a
  position slider - auditioning the clip **through the device, not through
  `AudioSource::playing`**: writing that flag would be a scene edit, undoable
  and dirtying and audible again on the next Play, when all that was asked for
  was to hear the file. Nothing on the row dirties the scene, which is the same
  rule the animation cards follow for play, stop and scrub. It is drawn by
  `auditionTransport`, shared with the Asset Browser's Sounds rows, because two
  surfaces auditioning the same kind of thing with two vocabularies is how they
  drift apart. On a host with no audio device the whole transport is disabled
  and its tooltip says why, rather than answering a press with silence. A fifth
  warning sits just above it, outside the spatial four: an `AudioListener` at
  volume 0 silences the whole mix, this source and the audition with it, and
  nothing else on the card would explain a cursor running with no sound.

  The slider reads `AudioDevice::voiceCursor` rather than a field on the
  component, because the mixer advances that cursor between frames and a
  mirrored copy would be stale by construction - see [the audio
  reference](system/audio.md#the-cursor-is-the-devices-not-the-components). With
  nothing playing there is no cursor, so the slider is disabled rather than
  inventing a start offset. The audition belongs to the card that started it and
  stops when the selection moves, so the slider can never run against another
  entity's clip. Stop and the scrubber are lit off the device, and so is the
  label beside them. `AudioSource::playing` is the scene's word and not the
  sound's - a voice the transport is holding keeps it true while nothing is
  audible - so the label asks `AudioSystem::voiceOf` for the source's own voice
  and reports what the mixer says about it: **Source: playing 12.40s**, or
  **Source: held 12.40s** under a Pause or a Step. With no voice at all it falls
  back to the flag, which is the only thing there is to say in the frame before
  one exists. The position lives here rather than on the slider because that
  slider belongs to the audition; the two are different sounds and the row keeps
  them apart.
- **Audio Listener** - active and master volume, plus the two things nothing
  else on screen would show: a listener that is not the ear, which names the one
  `findActiveListener` picked instead of merely counting the candidates, and a
  listener without a `Transform`, which has no position to hear from. Never
  both: the rule joins on `Transform`, so a listener missing one is not
  competing for the ear at all and would lose to a listener *later* in storage
  order - it is told what is actually wrong instead.

An entity carrying an `AudioSource` is named **Sound** in the hierarchy - **2D
Sound** when it is not spatial, which is the same split by kind a `Light` makes
between Dir / Point / Spot - and one carrying an `AudioListener` is a
**Listener**; the tooltip digest lists both.

### A card greys what the scene, not the author, is writing

The Light card on the scene's key light is the case: with **World > Procedural
Sky** on, `SkySystem` writes that light's rotation, colour and intensity from
the Environment every frame, so a drag on Colour or Intensity there is undone
before the next frame draws and the value the scene saves is the sky's. The
card offered all three like any other light's, which made a working widget look
broken and an authored colour vanish into the file.

It now says so - one line naming the three fields and where they are authored -
and disables the two it does not own, the same shape the Procedural Sky card
already uses for the fields that depend on its own toggle. Which light the sky
is driving comes from `findKeyLight`, the engine's own answer, so the card and
the system cannot disagree.

### A card names what its component is waiting for

A component that cannot work is the editor's worst failure to report, because
the card goes on rendering in full - every field live, every value plausible -
in front of a viewport where nothing happens. Where the engine holds both halves
of the diagnosis, the card says it, in `EditorStyle::DANGER` for "this does
nothing at all" and `EditorStyle::WARNING` for "this is not what you think it
is". It is said on the card because that is where the mistake is being made; the
log is where it is found afterwards, which is too late and, in the editor, not
visible at all.

- **Collider** with no `Rigidbody` - *"No Rigidbody: nothing collides with
  this."* `PhysicsSystem::gatherBodies` walks the `Rigidbody` storage and reads
  a `Collider` off the entities it finds there, so a lone collider is in no
  broadphase: it stops nothing and, `Trigger` ticked or not, fires nothing.
- **Rigidbody** with no `Collider` - *"No Collider: it falls through
  everything."* Scoped to a **dynamic** body, which is the one this ruins: it
  integrates gravity with nothing to land on and leaves the world. A static or
  kinematic body with no shape is inert rather than lost, and is not warned
  about. Both are the sentence the Character Controller card has always printed
  for the same two absences.
- **Mesh** with no mesh - *"No mesh: this entity draws nothing."* The material
  half of this card has always had its `else`; the mesh half did not, so an
  empty mesh slot took the vert/tri/bounds readout away and put nothing in its
  place - the card lost its reporting surface exactly when it had something to
  report. It is the harsher of the card's two absences: `VisibilitySystem`
  returns on an empty mesh handle, so the entity is in no draw list, has no
  selection bounds and cannot be framed.
- **Decal** with no material - *"No material: this projects nothing."* Harsher
  than the Mesh card's "No material assigned" because the consequence is: a mesh
  with no material still draws with the shader's defaults, while
  `GLDecalPass` skips a decal whose material is null outright. It is also the
  state `Entity > Create > Decal` hands you.
- **UI Element** with no `UICanvas` above it - *"No UI Canvas above this:
  nothing draws"*, plus where to drag it. `UISystem` lays out only what it
  reaches walking down from a canvas, so an element outside every canvas is
  never visited: no rect, no draw, no hit test. This is the default outcome of
  `Entity > Create > UI > Text` with nothing selected.
- **UI Image / UI Text / UI Button** with no `UIElement` - *"No UI Element:
  nothing to give it a rect"*. `resolveElement` returns before it looks for any
  of the three, so all three cards render in full for a component that is never
  reached. One helper, `warnNoUIElement`, says it for all three.
- **UI Text** whose font name does not resolve - *"No font named 'x' is
  loaded"*. The font is reached by name every frame and an unresolved one draws
  nothing, which looks exactly like an element that is hidden or off-screen.
  Every other asset reference on the panel reports a name the project cannot
  answer; this one is a plain text box, so the card has to.
- **Camera** that is active but not the one being rendered from - *"Not the eye:
  'X' is rendered from."* The eye's half of what the Audio Listener card says
  for the ear. Named from `CameraControllerSystem::getCameraEntity()` rather
  than from storage order: the controller and the visibility pass each keep the
  camera they resolved and hold it while it stays active, so "the first one
  wins" is a rule that is often not what happened, and printing it would hand
  the author a false reason. The hierarchy's **Set as Main Camera** stays
  offered while any other camera also claims Active, for the same reason -
  greying it on `cam.active` alone said "already main" about a camera that may
  well not be the one on screen.
- **Particle Emitter** in Edit mode - *"The world is not running - press Play to
  see them."* `ParticleSystem` returns on a zero sim delta, so the card's `Live:
  N` readout is structurally `0` there however well the emitter is set up, and
  an author reading "Emitting, Rate 20, Live: 0" is being told the opposite of
  what is true.
- **Animation** whose transport says it is playing while the world is not -
  *"Held at 1.20s - it advances while the world runs."* Both surfaces that own
  an animation transport print it (the card and the Bottom panel), because both
  toggle `Animation::playing`, which only `AnimationSystem` advances and which
  only advances on a non-zero sim delta. The button still sets the flag - it is
  the serialized "plays when the simulation starts" - it just no longer implies
  a playhead that is moving.

### Property rows clamp what is typed into them

`propDrag` / `propSlider` / `propDragInt` / `propDrag3` pass
`ImGuiSliderFlags_ClampOnInput` (`PROP_CLAMP` in `ui/editor_widgets.h`). A
Drag/Slider clamps the *mouse* to its bounds, but Ctrl+click turns the widget
into a text field that ImGui leaves unbounded by default, so every bound in the
inspector was advisory on the one input path that can type an arbitrary number -
`5000` into a Near Clip whose declared max is the Far Clip, and the camera
renders nothing. `ClampOnInput` rather than `AlwaysClamp`, because `AlwaysClamp`
also clamps a `lo == hi == 0` range, which is how the rows with no meaningful
limit spell "unbounded". The Camera card holds its two clip planes
`CLIP_PLANE_SEPARATION` apart rather than merely ordered: equal planes divide by
zero in the projection and the cluster pass takes `log(zFar / zNear)`.

`pickAsset` offers a **(none)** row above the list. An empty slot is a state the
editor hands you (`Create > Audio Source` and `Create > Decal` both arrive with
one), it is what the combo previews, and it round-trips through the scene file -
so a combo listing everything except the value it is showing could only be left
by deleting the component and authoring it again. The same row the script
behavior field and the bone picker have always drawn.

## Undo / redo

Every editor mutation goes through a `Command` that captures the
"before" state and applies the "after" state. The stack is bounded
(default 200 entries) and is cleared on scene load (entity IDs and
component topology are not comparable across a swap).

Available commands (in `framework/editor_commands.h`):

- `TransformChangeCommand`: position / rotation / scale on an entity.
  Coalesces consecutive edits on the same entity *within one gesture*, so a
  gizmo drag or a stream of inspector micro-edits collapses to one undo step
  while the next drag starts a new one.
- `ComponentEditCommand<T>`: a generic field edit on an existing component
  (snapshots before/after), the inspector's catch-all undo step.
- `AddComponentCommand<T>` / `RemoveComponentCommand<T>`, instantiated for
  every type in `VKM_EDITOR_COMMAND_COMPONENTS`: `Mesh`, `Light`, `Camera`,
  `Animation`, `Rigidbody`, `Collider`, `ReflectionProbe`, `Decal`,
  `ParticleEmitter`, `IrradianceVolume`, `LOD`, and the five UI components
  (`UICanvas`, `UIElement`, `UIImage`, `UIText`, `UIButton`). Add also covers
  `Name`, which has no Remove - an entity without a name falls back to its type
  label. Remove snapshots the prior value so undo restores it exactly, not a
  default-constructed copy.
- `ScriptEditCommand`: the Script card's whole vocabulary - the component
  added or removed, a behavior attached or removed, a field typed into - as one
  step over the component's serialized form, with "no ScriptComponent" spelled
  as an empty document. A behavior list is move-only, so there is no value for
  `ComponentEditCommand<T>` to copy; the JSON `EntitySnapshot` already
  resurrects a deleted entity's scripts from copies fine. Coalesces within a
  gesture like the rest, so a drag on a behavior's float field is one step.
- `CreateEntityCommand`: captures the post-create slot so redo
  recreates at the same slot.
- `DestroySubtreeCommand`: captures the entire subtree (entity plus
  every descendant) including parent/child wiring, so undo can
  resurrect a non-leaf delete exactly. `PrefabInstance` and `PrefabEntity` are on
  the snapshot's component list, so a deleted instance comes back as an instance
  rather than as the entities it had expanded to.
- `ReparentCommand`: (child, oldParent, newParent), inverse via
  `HierarchyOperations::setParent` / `removeFromParent`.
- `SetActiveCameraCommand`: backs the inspector's "Set as Main" camera action.
- `PlacePrefabCommand`: redo rebuilds the instance from the prefab file - source,
  overrides and all - into a root reclaimed at its original slot, because that is
  what a placement is: a reference, a pose, and the entries against it.
  Duplicating an instance pushes one of these too.
- `PrefabOverrideCommand`: takes the place of `ComponentEditCommand` on an
  entity inside a prefab instance. The value there is the prefab's, patched by
  the instance's overrides, so both directions restore an entry set and re-read
  the component from the file; it coalesces a drag the same way. It names its
  target by prefab uid rather than by slot, because redoing a placement pins
  only the root's slot and rebuilds the rest into whatever is free.
- `MaterialEditCommand`: a PBR field edit in the Material Editor. Restores the
  parameters and re-commits so the previews and the viewport re-read the asset;
  the asset's identity (name, uid, source) is deliberately left as it is, since
  the name is renamed through its own command.
- `RenameAssetCommand<HandleType>`: undoable asset rename (routes through
  `ResourceManager::rename` so the name index stays consistent). Instantiated
  once per asset kind the Asset Browser lets an author rename, so that list and
  the browser's `KINDS[]` table say the same thing.

Templated commands are emitted out of line via `extern template` in the
header and instantiated once in `editor_commands.cpp` so each
translation unit doesn't recompile the bodies. Both blocks expand from
the single `VKM_EDITOR_COMMAND_COMPONENTS` list, so they cannot drift.

`CommandStack::push` calls `Command::tryMerge` against the top of the
undo stack first; that is where transform drag coalescing happens - but only
while the gesture is still open. `EditorSystem` calls `CommandStack::endGesture`
at the end of every frame in which no mouse button is held and no ImGui item is
active, which seals the top of the stack. A gesture is a press, a motion and a
release, and it is one undo step; identity alone cannot tell the micro-edits
inside one drag apart from two separate drags of the same field, and without the
seal the second drag was swallowed by the first.

`Command::addresses(slot)` is the other half: the steps that name an entity say
so, and `CommandStack::forget` drops exactly those. It is the narrow half of
`clear()`, for an operation that outlives part of the history rather than all of
it - see Save as Prefab below.

## Opening a project

The editor edits *a project*, not the repo it was built in. `ProjectController`
(`src/editor/framework/project_controller.h`) owns **File > Open Project...** and
the **File > Recent Projects** list, and re-roots the whole editor in place - no
restart. Order matters, because each step depends on the previous one:

1. Save the outgoing project's `editor_settings.json`, while its root is still
   current - otherwise its tuning would land in the project being opened.
2. `ProjectPaths::setProjectRoot(root)` - every path composed after this points
   at the new project.
3. Tear the scene down through `SceneIOController::beginSceneReplace`: behaviors
   get `onDestroy` while the old module still holds their code, and the undo
   stack, material previews, play snapshot and saved-scene path all go with it.
4. Drop the outgoing project's assets - a generated world never swaps the
   `ResourceManager` the way a scene load does.
5. `AssetLibrary::get().load()` and the new project's own editor settings.
6. Swap the gameplay module to the new project's `bin/`, or unload it when the
   project brings none.
7. Boot its scene through `bootProjectScene`, the same rule both binaries use,
   and adopt the path it opened so that scene is the file this session edits -
   without it, Save would ask for a name for a file the editor had just read.
   A project whose entry scene will not load still opens - the default scene
   stands in, carrying no save path - with an error toast, because the editor is
   where you fix that. The runtime refuses the same project instead; see
   [system/io.md](system/io.md#what-each-host-does-when-a-project-will-not-open).

A path that names a file rather than a directory still works - `findProjectRoot`
walks up to the owning `project.json`, so dropping in a scene opens its project.

Command-line `vkm_editor <project>` does the same thing at startup, before any
path is composed. See [system/io.md](system/io.md#projects-and-the-three-roots).

## Scene I/O

`SceneIOController` owns the New / Open / Save / Save-As flow:

- Drives the file-picker modals.
- Hands off to `SceneSerializer::save` / `load` (engine-side; see
  [IO and serialization](system/io.md)).
- After a successful load it clears the command stack and rebinds the
  camera if the loaded scene defined one.
- Ends any play session the outgoing scene was in - the snapshot, its asset
  list and the clock's pause/scale all go back to Edit mode. A snapshot that
  outlived the scene it was taken from leaves the transport reading as playing,
  and Stop then restores that dead world **over the scene just opened**, under
  the opened file's name. New Scene and Open Project both clear it through the
  same `endPlaySession`.
- Maintains a recent-scenes list cached when the Open dialog is opened
  (so re-opening doesn't re-scan disk every frame).

### A play session owns the scene

Play snapshots the authored scene and hands the world to the simulation, so
what the ECS holds during a session is the simulation's copy of one. Every
panel stays live inside a session, which is useful - and used to be silent.

- **Save is refused while a session is live.** File > Save Scene and Save Scene
  As are greyed under a "Stop the play session to save" line, and Ctrl+S answers
  with a toast. Writing the played scene over the authored file stores a scene
  nobody wrote, and then clears a dirty flag that Stop restores to its pre-Play
  value - so the editor would go on reporting the file as current while the
  authored scene was gone, with an ordinary INFO line as the only word said.
- **The viewport says which mode it is in.** A live session frames the viewport
  in the warning colour and captions it PLAY MODE - edits are discarded on Stop.
  Before, the only thing separating the two modes on screen was a 20px transport
  glyph changing shape.
- **Stop says what it discarded.** A session that moved the undo history, or
  dirtied a scene that was clean when Play began, has authored work in it; Stop
  warns rather than withdrawing the undo step and the dirty marker it raised for
  that work without a word.

`SceneIOController::stopPlaySession` is the whole of Stop in one place, because
the transport's button is not its only caller: answering **Save** to the
unsaved-changes prompt ends the session first, the scene that save is for being
the authored one Stop puts back.

**File > Exit** asks the unsaved-changes question itself rather than raising the
window's close flag for the frame's close-intercept to catch. That intercept is
right where it runs - the top of the UI stage, after the window has reported a
titlebar close - but a menu item raises the flag from inside that same stage,
and the frame loop reads it before the next frame begins. So the X prompted and
Exit, the same intent said another way, quit without asking.

### What an open does to the session's imports

An open is the editor's clean break: it drops the undo stack, the
selection, the material previews and the camera binding. The asset graph is
replaced by the swap too, so an asset the *outgoing* scene never named - a
sound imported and not yet assigned to a source - has no name in the new
document to be recreated from, and goes with the session that imported it.

**New Scene and Open Project answer this the same way**, through
`beginSceneReplace`: it swaps a fresh `ResourceManager` in (keeping the font
slot, which is engine-owned and never written to a scene) and counts the strays
into the same toast. They throw a whole world away, so the reasoning above
applies to them at least as strongly - and left in place, the outgoing graph
collided with the seed scene the next New Scene builds: `buildDefaultScene` adds
its cube and default material unconditionally, `ensureUniqueName` gave them a
`" (2)"` suffix, and names being the serializable identity, that suffix became
the new scene's frozen identity in the file and in the cooked manifest. A scene
authored after two New Scenes in one session named `material:default (3)` and
had no `(1)` or `(2)` anywhere in it.

That is deliberately the opposite of what **Stop** does. Stop promises to put
one session back exactly as Play found it - the undo history included. The
snapshot is written from these entities at these slot indices and read back
through `createEntityAt`, so every step on the stack still names what it named
before Play, and edit / Play to check / Stop / undo the bad edit is a loop that
works. `captureSnapshot` records `CommandStack::revision()`; if the session
moved the history - a panel is live in play mode, so an edit made during one
addresses the world about to be discarded - that half of the open's reasoning
does apply, and the stack is dropped with a toast saying so.

**The asset graph is kept, not rebuilt.** An undo step holds the asset it is to
put back, as a handle, and a handle is a slot index into one `ResourceManager`.
An open swaps in a graph built from the file it opened, which is why an open
drops the stack; a Stop that did the same would leave the surviving steps
addressing a manager that no longer exists, and since a rebuilt graph restarts
at the same indices and generations those steps would resolve - to whatever
landed in the slot instead. Measured: create a Sphere and a Cone, point the
Sphere entity at the cone mesh, Play, Stop, Ctrl+Z, and the undo put
`mesh:generator:cube` on it. So `restoreSnapshot` reads the snapshot into the
graph it was captured from (`AssetPolicy::Merge`), and puts each asset's
*contents* back in place first - `loadAssets` with `LoadMode::Reload`, over the
`saveAllAssets` document `captureSnapshot` recorded beside the scene - so a
material edited during the session reverts like everything else while its handle
goes on naming it (see [IO and serialization](system/io.md)).

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
tile draws the GPU mirror the renderer already samples, at tile size, and the
GPU minifies it. Nothing is rendered, nothing is copied, and a 4K map costs a
tile no more than a 64px one.

What it does cost is *residency*. `GLView::sync` reaches a texture only through
a material something draws, so a texture no drawable, caster or decal binds has
no mirror at all - which is right for a frame and wrong for a library that shows
every texture the project holds. `EditorRenderHooks` therefore has two calls,
and the difference between them is the whole point:

- `textureId(handle)` - reports the mirror, or 0. Never uploads.
- `ensureTexture(handle, resources)` - uploads if there is no mirror, then
  reports. Idempotent and version-gated, but the first call per texture pays a
  full upload.

The grid asks the first, and only spends `TEXTURE_UPLOADS_PER_FRAME` (3) calls
to the second per frame, so opening a rail of 4K maps fills in over the next few
frames instead of stalling one. That is the same bargain `MaterialPreviewSession`
strikes for thumbnail bakes.

**Known: an sRGB texture's thumbnail draws darker than the file.** ImGui samples
a `GL_SRGB8_ALPHA8` mirror - which linearises - and writes the result straight to
a framebuffer that is not sRGB-encoded, so the transfer function is applied once
and never undone. Linear maps (normal, roughness, AO) are unaffected and read
exactly as authored. The Material Editor's slot thumbnails take the identical
path and have always done the same thing; correcting it needs a per-image ImGui
draw callback, since the material and mesh thumbnails come out of the composite
pass already display-encoded and must *not* be converted.

### Live PBR previews

Both the Material Editor and the Asset Browser show live PBR previews.
These are rendered by the backend's dedicated preview path
(`RenderBackend::renderPreview`, backed by `GLPreview`) - **not** the full
frame pipeline. It is a minimal forward + composite render of the material on
a preview mesh into a small offscreen target, kept separate from the main
19-pass path. Results are cached per asset (keyed by handle + version) with a
small per-frame bake budget, so the Asset Browser grid amortizes thumbnail
generation across frames while the Material Editor's live view re-renders each
frame. Each kind gets its own key space (`previewKey`), and none of them is 0 -
that one is reserved for the Material Editor's live pane.

Right-clicking a tile assigns it to the selected entity - a material or mesh to
its `Mesh`, a sound to its `AudioSource`, a skeleton or a clip to its `Animator`
- and that assignment is the same edit the Inspector's asset dropdown makes - so it takes the same road, `pushEdit`, which is what gives it an undo
step and what turns it into a prefab override when the entity is an instance.
Writing the component directly here instead left the instance's override list
empty while the viewport showed the new asset, and the next save wrote the
prefab's own back over it with nothing said.

### The Assets tab has no window of its own

The browser is drawn by `BottomPanel` as its first tab and opens nothing. It
was a floating `Window > Asset Browser` (Ctrl+6) until it was docked, and the
window went in the same change rather than surviving beside the tab: two ways
into one panel is the half-finished refactor `implementation.md` s7.3 names,
and it costs an author a second answer to "where is my library" and every
future fix a second place to land. The menu item, the `showAssetBrowser` flag
and the keybind went with it.

The bottom panel's default height grew with the tab, to what one whole row of
default-size tiles needs. A grid clipped mid-tile reads as a broken tile
rather than as a panel that wants dragging - which is not true of a timeline
clipped mid-track, and is why a height that suited the Animation tab does not
suit this one.

### The browser is a table of kinds, not a template over two of them

The panel used to be a template parameterised on the asset type, with
`static_assert`s admitting `MaterialAsset` and `MeshAsset` and nothing else -
because a thumbnail needs a type to render. Everything that arrived afterwards
had to work around that: audio got a tab of its own with its own import button
and its own row shape, and the skeletons and animation clips that landed in 1.6
got no surface at all. The toolbar showed the cost. `Import Model...` and
`New Material` were drawn at panel scope while `Import Sound...` sat inside the
Sounds tab, so on that tab the panel's most prominent row - the two buttons
top-left where the eye lands, plus a thumbnail-size slider - was entirely dead.

It is now a `KINDS[]` table of `AssetKind` descriptors, and the body that draws
the rail, the tiles and the menus names no asset type at all. A descriptor
carries a label, a glyph, an `Accent::` colour, its primary verb, and a handful
of function pointers: enumerate, describe, preview, assign, rename, delete,
and the walk that proves a delete is safe. The only place a C++ asset type
appears is `KindOps<Asset>`, a three-method template the table's entries
instantiate.

**All six of `AssetType`'s kinds are in the table** - materials, textures,
meshes, skeletons, clips and sounds - and the three that joined last are what
settled which of the table's slots were real. Three nullable slots earned their
keep and two did not:

- `thumb` null - sounds, skeletons and clips have no picture, so the tile draws
  the kind's glyph on the same square.
- `assign` / `assignLabel` null - a texture has no entity target, because it
  goes into one of a material's eleven slots and no entity can say which. The
  context menu omits the item rather than offering a greyed one.
- `rename` null - **skeletons only**. A skinned `MeshAsset` and an
  `AnimationClipAsset` each carry the rig's name as a *string*, and
  `SkeletalAnimationSystem` refuses a clip whose `skeleton` no longer matches
  the rig it is handed. Renaming a rig therefore unbinds every mesh and clip
  bound to it, silently; putting them back means editing assets the author did
  not select. The menu item is greyed with `noRename` as the reason.
- `used` was nullable and is not any more: all six kinds can be walked, so the
  null branch and the `noDelete` string that explained it were dead and went.
- `undoLabel` went with them - every assignment passed its own literal to
  `pushEdit`, so the field was written six times and read never.

**`FontAsset` is deliberately not a kind.** It is a `Resource`, but `AssetType`
leaves it out (`ASSET_TYPE<FontAsset>` is `Count`) because the library does not
hold it, and every slot in the table agrees: a font is baked once at startup,
referenced by name rather than by handle, has no importer, has no entity slot
to be assigned to, and renaming one would orphan every `UIText` naming it. It
would join as a row that only counts - and only after `AssetKind` stopped being
keyed by `AssetType`, since the rail row, the rename target and the preview key
space are all keyed on that tag. That is a wider table bought for a row that
does nothing.

`AssetLibrary::namesOf(type)` is the second tier of the same seam - names per
kind with no concrete type needed - but the browser stays on `ResourceManager`,
because thumbnails and assignment need handles and the library only has names.
The two disagree on purpose: the library is what a *saved* name resolves
against, the manager is what is *loaded*.

### Each kind is a colour, and the strip says use

A rail row wears its own kind's hue, thinned with alpha until white text sits on
it, rather than the editor's one blue for whichever row is selected. Six rows
highlighted in the same blue read as one list whose entries happened to have
different words in them, and the accent strip beside them is three pixels wide
and cannot carry the difference alone.

| Rail row  | `EditorStyle::Accent::` | Why |
|-----------|-------------------------|-----|
| Materials | `MatBase`   | warm orange; the Material Editor's own base group |
| Textures  | `MatSurface`| teal |
| Meshes    | `Mesh`      | green; the Mesh card's hue |
| Skeletons | `Transform` | deep blue (`AXIS_Z`) |
| Clips     | `Anim`      | purple; the Animator card's hue, and a clip is half that card |
| Sounds    | `Audio`     | magenta; the Audio Source card's hue |

Every hue is already in the registry - nothing was added for the browser, and
no panel-local colour exists. Two constraints picked the three new ones.
`Accent::MatTexture` is the obvious name for Textures and is **not usable**: it
is defined as `AXIS_Y`, which is exactly `Accent::Mesh`, so it would put the
identical green on two adjacent rows. And none of the six may be `WARNING`,
`SUCCESS` or `DANGER`, which stay status colours so that no asset kind can read
as an error. Skeletons and Clips both belong to the Animator card and cannot
share its one hue, so the rig takes the registry's deep blue.

The rail order is neither `AssetType` order nor alphabetical: it pairs the kinds
that are about each other. A material is made of textures; a rig poses a mesh; a
clip drives a rig; a sound belongs to none of them and goes last. That also puts
maximum hue distance between neighbours.

The tile keeps the same strip, and how solid it is says whether **anything in
the project** uses the asset. On a grid showing one kind at a time the strip was
identical on every tile - sixty bars repeating what the rail had already said -
while the one thing a library is actually asked about its rows was legible only
as a greyed-out Delete. It is the same `used` walk behind both, so the strip
claims exactly what the delete guard claims and no more. The hover tooltip
spells it out: *"Nothing in this project uses it"*.

#### What "in use" walks, per kind

The walk takes the scene **and** the `ResourceManager`, because the scene is not
the whole project. Half of these references are not on any entity, and a walk
that missed them would offer a Delete that breaks something far from where it
was pressed.

| Kind      | Referenced by |
|-----------|---------------|
| Materials | `Mesh::material`, `Decal::material` |
| Textures  | all eleven `TextureHandle` slots on **every** `MaterialAsset`, drawn or not |
| Meshes    | `Mesh::mesh`, every `LODLevel::mesh` |
| Skeletons | `Animator::skeleton`, plus `MeshAsset::skeleton` and `AnimationClipAsset::skeleton` resolved back from their **name** strings |
| Clips     | `Animator::clip` and `Animator::fadeFrom` (a fading clip is still being sampled) |
| Sounds    | `AudioSource::clip` |

`MaterialAsset`'s eleven texture members are now enumerated in a fourth place
(`MATERIAL_TEXTURE_SLOTS` in the panel, beside the serializer's `TexField`
table, `GLMaterial`'s binding table and the Material Editor's rows). Each of the
other three pairs the member with something of its own - a JSON key, a binding
point and flag, a row label and colour space - so there is nothing to borrow;
the day a fifth appears is the day the bare list belongs on `MaterialAsset`.

### One tile, whatever the kind

A tile is a square face, a name and a one-line detail. The face is one square
whatever fills it: `FramePadding` is zeroed under it, because an `ImageButton`
frames its picture with that padding and a `Button` sized by hand does not, so
on the theme's `(8, 4)` a thumbnail tile stood eight pixels shorter than a
glyph tile - no two kinds' name lines could share a baseline, and inside one
kind the tiles waiting on a bake sat off it too. The same zero puts the
picture's left edge on the name's left edge instead of eight pixels right of
it. The face is a rendered thumbnail where the kind has one and the kind's
glyph, in the kind's accent, where it does not. That is not a new idea - it is the rule `editor_icons.h`
already states for viewport markers: the marker says something is there and the
glyph inside says what, so a sound does not need a picture invented for it to
sit beside a mesh. A kind that *has* thumbnails but has not had its bake turn
yet draws the same glyph faintly, so "there is no picture for this" and "the
picture is coming" do not look alike.

The name is clipped to one line with the full name in the tooltip, and the cut
lands in the **middle**. These lines share their starts and differ at their ends
- a clip named by its project-relative path, the sixtieth material out of one
file - so a tail cut left the two sounds in a test project both reading
`assets/audio/to...`, and `BrainStem:mat10` indistinguishable from
`BrainStem:mat11`. One line is the older half of the rule: the name used to be
`snprintf`'d to 20 characters inside a `PushTextWrapPos`, so a long name was
truncated *and* wrapped, and the second line pushed every tile after it off the
baseline.

The detail line is where the missing picture goes, and it comes in two forms:
a short one for the tile, which has about fourteen characters to live in, and a
verbose one for the hover tooltip. That is how the Sounds table's Format and
Size survive losing their columns - `0.50s . mono` on the tile,
`0.50s . mono 44100 Hz . 0.0 MB` on hover. A mesh says `926 tris . skinned`,
and `skinned` is doing real work there: the preview draws bind-pose vertices
with no rig behind them, so a skinned mesh's thumbnail can look like nothing
recognisable, and the tile says why rather than leaving it to be guessed at.
(The framing itself is not the problem - `GLPreview` already centres on the
mesh bounds and measures `PreviewRequest::distance` in bounding radii, so every
mesh is framed alike.)

A material's short line is its roughness, or its render path when that is not
Opaque: the thumbnail already shows colour and gloss, so the line says the
thing the picture cannot, and a transparent material looks like an opaque one
on a preview sphere while behaving nothing like it.

The three kinds added last follow the same rule - the short form carries the one
fact that separates assets of that kind, the verbose one carries the rest:

| Kind      | Tile | Hover |
|-----------|------|-------|
| Textures  | `2048x2048` (or `decoding...` while an async import is in flight) | `2048x2048 . 4 ch . sRGB . 16.0 MB` |
| Skeletons | `24 bones` (or `no bones`) | `24 bones . root 'Armature'` - the root bone is what an author recognises a rig by, and two rigs out of one file differ there before they differ in count |
| Clips     | `2.00s . 57 ch`, or **`2.00s . no rig`** | `2.00s . 57 channels . rig 'X'`, `. N markers` when it has any |

**`no rig` is the diagnostic the Clips rail exists for.** A clip names its
skeleton by string, and `SkeletalAnimationSystem` throws out a clip whose name
does not answer - so a clip bound to a rig the project no longer holds animates
nothing while looking exactly like one that works. The tile reports the rig it
*resolved*, not the name it carries, and the tooltip names the rig that is
missing: `rig 'CesiumMan:skeleton' is not in this project`. It is text and not
`WARNING` for the reason the Mesh card's `Skinned: rig 'x' not loaded` is text:
`WARNING` / `SUCCESS` / `DANGER` are status colours, and a mis-bound clip is a
fact about the asset, not a failure of the frame.

A multi-channel sound's hover carries the equivalent for its kind: *"Each
channel sticks to one ear - positioning wants mono."* The mixer routes each of a
voice's channels to the output channel it was authored for and attenuates it
there, so a wide clip put on a spatial source loses half its field wherever the
emitter goes, and a stereo file whose channels happen to be identical behaves
exactly like the mono equivalent - which is what makes the mistake quiet. It is
said here because the browser is where a clip is *picked*; the Inspector's Audio
Source card says it again where one is *put on a source*, and `AudioSystem` logs
it once per clip for the request path that has no card.

### What a tile answers to

Hovering a tile draws a two-pixel border in the kind's own hue, over a thumbnail
and a glyph alike. The theme's button accent cannot be that signal: both
`ImageButton` and `Button` paint it *behind* what fills them, so it is a flat
blue slab under a glyph and invisible under a thumbnail - one gesture with two
answers. The face's hovered and active colours are pushed back to the idle one
and the border carries it instead.

The pointer, not a selection, is what an operation acts on: **F2** renames the
tile under the cursor, exactly as the Hierarchy renames the row under its own.
Delete is deliberately **not** bound beside it - `deleteEntity` already owns that
key, and `EditorShortcuts::process` reads it before any panel draws, so a second
meaning would destroy an entity and open this dialog in one keystroke.

The context menu names its target before it offers anything. It covers the tiles
either side of the one it belongs to, and a grid of one kind is a row of
near-identical squares, so `Delete` without a name is a guess.

**Rename** opens the shared dialog with the field focused and the old name
selected, so the gesture is F2, type, Enter with no reach back for the mouse.
`ResourceManager::rename` keeps names unique per type by suffixing a taken one,
which used to happen in silence - an author typed `Rock`, got `Rock (2)`, and
nothing said so. It is now a toast, and the undo command records the name that
was **assigned** rather than the one that was asked for, so redo repeats what
happened rather than what was requested. (The Material Editor has a second
rename modal of its own, a near-copy of this one; it now shares the keyboard
half, but the two are still two implementations of one operation.)

**Delete asks first**, and it asks rather than offering an undo because an asset
cannot come back: re-adding one takes a new slot, so every handle that named the
old one - including the ones already on the undo stack - would still be dead.
`RenameAssetCommand` guards `isAlive` for exactly that case. The dialog names the
asset and states the consequence: *"Undo cannot bring it back."* Deleting the
clip that is auditioning stops the voice first - `AudioSystem` holds the samples
by `shared_ptr`, so the sound would otherwise play on with no tile left anywhere
to stop it.

### One verb slot

The first control in the toolbar is always the chosen kind's primary action, at
the same place whatever kind that is. The label is the verb alone - `Import...`
for five kinds, `New` for materials - because the rail two inches to its left
already says which kind is showing and `Import Sound...` spent half a button
saying it twice. What the noun carried, the formats behind an import, moved to
the button's tooltip and reads fuller there than it ever did on the face:
*"Import a model - glTF, GLB, OBJ, FBX, DAE, STL, PLY or 3DS"*.

Five of the six say `Import` and the sixth says `New`, and that difference is
the kinds', not the panel's: everything else arrives from a file, a material is
authored, and there is no material file to import. It is a value in the
descriptor table like every other per-kind fact, not a branch in the toolbar,
and `New` is what the Material Editor has always called the same action. Search
and the tile size sit to the button's right, always. Nothing in the toolbar is
ever inert.

Search narrows the rail's counts and the grid together, so a kind with nothing
matching reads `0` rather than offering an empty grid to walk into. Escape
empties the box: ImGui's default is to revert a field to what it held when it
took focus, which on a search field puts back the needle the author is trying to
drop.

Three verbs stand behind the six buttons. Materials create; meshes, skeletons
and clips all raise `EditorState::requestModelImport`, because all three come
out of one model import and the tooltip narrows the formats to the ones that
carry a rig or an animation; textures and sounds each run a panel-owned
`AssetPicker` of their own, so their popup ids stay unique and one import in
flight cannot be handed the other's file.

**A texture is imported as colour (sRGB).** A file picked by hand off a picker
is art; the data maps that want linear arrive with the model that uses them, or
through the Material Editor slot, which knows which of the eleven it is filling
and passes the colour space for it. The import refuses a file the project
already holds and says so in a toast, because `loadTexture` decodes and adds
without looking and `ResourceManager` keeps names unique - so a repeat import
would otherwise leave two assets for one file. (The Material Editor's own `Set`
button does *not* guard this, and setting one file into two slots does produce
two assets; that is its bug to fix, in its panel.)

### Auditioning, from the tile

Hearing a clip is what previewing one means, so the transport sits on the
sound tile's face the way a play control sits on a video thumbnail - the tile
keeps every other kind's height and gains no row of its own. It is the
Inspector card's transport, drawn by the same `auditionTransport`: Play on
every tile, and on the tile that is sounding a Pause that holds it and a Stop
that cuts it short. The face is submitted with `SetNextItemAllowOverlap()`,
without which the face button would hold the mouse over the whole square and
the transport drawn on top of it would never register a click.

While a tile owns the voice its detail line becomes the position slider, for
the reason the old table put the slider in the Length column: a position
measured against a length belongs where the length was stated. Every other tile
keeps its detail as text.

One voice serves the whole panel, so a Play replaces whatever was sounding
rather than layering over it, and the panel remembers **which clip** that voice
came from - that is what lets one tile own the transport instead of the panel
owning a Pause and a Stop for all of them. The remembered clip is a full handle
rather than an id, so a slot recycled by a remove and an add cannot hand a
different clip a running transport; a graph swapped underneath it cannot
either, since `AudioSystem` stops every voice when the asset epoch moves.

An audition does not follow the user out of the panel: leave it and a
ninety-second ambience plays on, because this panel is the only thing holding
the voice's id - come back and the tile is still sounding, with its Stop lit.
Pause, Stop and the slider are lit off the device rather than off a remembered
id, here and on the Inspector's copy of them: an id outlives the voice it
named, so a clip that ran to its end would otherwise leave a Stop offering to
cut something that already stopped.

The toolbar carries the two ways a clip goes unheard with nothing here wrong -
a host with no audio device, and an `AudioListener` at volume 0, which silences
the mix an audition plays through as surely as an absent device does - and only
while the Sounds rail row is the one showing, since that is the only kind they
are about.

The Sounds `Import` decodes a wav / mp3 / flac into the project, which is the
only way a clip enters one. Picking a file the project already holds is
answered with a toast saying so and nothing else: `loadAudioClip` keys on the
project-relative name and hands back the clip it already has, so there is no
new tile to look for, and the scene is not dirtied for an import that did not
happen.

### What Create > Primitive puts in the asset graph

A generated mesh carries a deterministic name - `mesh:generator:cube`,
`mesh:generator:sphere:32:16` - built from the same parameters as its source
descriptor, and `stampGenerated` says why: *"identical generator calls land on
one asset: the name is the serializable identity, and two meshes generated the
same way are the same mesh rather than two copies a scene would save twice."*
The menu broke that promise, because it added what the generator handed it
without asking whether the graph already held that name. `ensureUniqueName`
then did what it is for, and three cubes in one scene were
`mesh:generator:cube`, `mesh:generator:cube (2)` and `mesh:generator:cube (3)` -
three identical 24-vertex meshes, three recipes in `library/`, three
indistinguishable rows in every mesh picker, and the suffix frozen into the
scene file as the identity of two of them. The default material went the same
way, so `Edit Material` on one cube reached a copy the others did not use.

`addGeneratedMesh` and `generateDefaultMaterial` both reuse what the graph holds
under the name they would have taken - the rule the built-in 1x1 textures beside
them already followed. Nothing edits a generated mesh, and a material meant to
be its own is made by **Duplicate** or **New**, which copies the
default rather than renaming it: renaming it would take `material:default` out
from under everything that resolves that name, including a cold-start load.

### When the scene has no camera

The editor has no camera of its own: `CameraControllerSystem` retargets each
frame onto whichever entity holds an active `Camera`, so unticking Active on the
last one - or deleting it - empties the viewport and freezes navigation, and
`VisibilitySystem`'s "No active camera found for visibility" goes to a log file
the editor cannot show. `ViewportOverlay::drawNoCameraNotice` puts it on screen
instead, centred in the viewport it is explaining: **"No active camera - nothing
to render from"**, with the two routes back under it.

`Entity > Create > Camera` is one of those routes, so it activates the camera it
creates **when the scene has no active one**. Inactive is right when another
camera already owns the view - it stops a new camera hijacking it - and wrong in
the one case where creating one is the recovery, where an inactive result looks
like the menu item did nothing.

## CameraControllerSystem

FPS-style fly camera; a `System` on `SystemStage::Input`. Updates the
active camera's transform from input each frame.

**It starts disabled and the editor is what enables it.** The shared bootstrap
registers the controller for both hosts, so the switch decides whether a
shipped game gets fly controls, and right-button-down puts the window in
`CursorMode::Disabled` - hidden, grabbed and re-centred every frame. Measured
on `vkm_runtime` before this was settled: holding the right button warped the
pointer to the centre of the window and snapped it back whenever it moved, and
the game had no way to decline, because `BehaviorContext` carries the scene,
the resources, the window and the events, and no systems. Off by default rather
than turned off by the runtime, so a host that says nothing gets a controller
that does nothing instead of one that takes the cursor.

### Controls

| Input                          | Action                            |
|--------------------------------|-----------------------------------|
| Right mouse button (hold)      | Enable look mode (cursor hidden)  |
| Mouse movement (in look mode)  | Rotate camera (yaw/pitch)         |
| W / A / S / D                  | Move forward / left / back / right|
| Q / E                          | Move down / up                    |
| Shift (hold)                   | Speed boost                       |
| Scroll wheel                   | Zoom (adjust FoV)                 |

### Configuration

```cpp
class CameraControllerSystem : public System {
    struct Settings {
        float zoomSensitivity   = 0.02f;
        float lookSensitivity   = 0.002f;
        float moveSpeed         = 10.0f;
        float speedBoost        = 3.0f;
        float scrollMultiplier  = 2.0f;
        float minPitch          = -90.0f;
        float maxPitch          = 90.0f;
    };
    // ...
};
```

Keybindings are configurable through the keybinds system; see the
Preferences window's Keybinds tab.

### Flying the camera is an edit

There is no separate editor camera: the controller flies the scene's active
`Camera` entity, which is what makes "you move what you see" true and what lets
a viewport click pick through the same view the renderer draws. That entity's
`Transform` is also a value the scene file stores - so a look around, a scroll
dolly, Frame Selected and a view-cube snap all change authored data.

They mark the scene unsaved, through `CameraControllerSystem::takeCameraMoved`:
the controller reports that it moved the camera and `EditorSystem` marks the
scene dirty, outside a play session (inside one the world is the simulation's
copy and Stop puts the camera back with it). Before that, navigation left the
title clean while the pose in the world and the pose on disk drifted apart, and
the next save made for an unrelated reason wrote the parked viewport over the
framing somebody had chosen, silently.

Navigation deliberately pushes **no** undo step. A drag is not a discrete edit,
and a bounded history filled with how you got to a viewpoint would push the
edits worth undoing off the end of it. The dirty marker is the honest half: it
says the file no longer matches the scene, which is the fact a save needs.

## Transform gizmo

Viewport-space manipulation handles for translate, rotate, and scale.
Axis-constrained operations are supported. A fourth mode, **Select**,
draws no handles (pick-only) so clicks always select rather than drag.

`transform_gizmo.cpp` holds the whole gizmo: the `manipulate()` entry point and
its shared math (world<->screen projection, ray construction, screen scale), the
visuals, the ray casts and pick tests, and the drag state machine. A drag emits
a single `TransformChangeCommand`, so undo steps back over the whole gesture
rather than each frame of it.

Default tool keybinds (active only when the camera is **not** in fly mode):
`Q` Select, `W` Move, `E` Rotate, `R` Scale, `X` toggles Local / World.
All are rebindable from the Preferences > Keybinds tab.

Everything with no mesh of its own draws a gizmo in `gizmo_overlay_draw.cpp`,
so it can be found and placed at all: lights (directional rays, cone
projections, area-light edges), cameras (frustum lines), reflection probes and
irradiance volumes (influence boxes, and the selected volume's probe grid),
decals (projection box) and particle emitters (marker plus velocity), and audio
sources and listeners (a billboard icon each - the source's keeps the speaker's
arcs only while it is spatial; plus the selected source's `Min` / `Max Distance`
spheres, and the listener's facing arrow). These are authoring shapes rather
than debug overlays, so none of them is behind a `View` toggle.

Four of those - lights, cameras, audio sources and listeners - mark themselves
with a glyph on a dim disc, all at one size, because a marker says *something
is here* and the glyph inside it says what. One call draws it
(`drawEntityMarker`, in `ui/editor_icons.h`) and the picker answers clicks
within the radius that same header states, so a marker cannot be resized into
a click target that no longer matches it.

The `View` menu adds three overlays that are off by default because they draw
for every matching entity rather than the selection: **Show Colliders** (the
boxes the solver collides against), **Show Bounds** (the world AABB of every
visible entity) and **Show Skeletons** (each posed rig's bones, straight out of
`FrameContext::poses` - segments joint to joint, plus an axis triad per bone on
the selected rig, which is what makes a bone's *orientation* visible and not
just its position).

## Entity selection and shortcuts

- Click in the viewport to pick entities (ray-AABB against the visible
  set's cached world AABBs). Everything else is picked by its **billboard
  marker**: a light, a camera, an audio source and a listener have no mesh to
  be hit through, and the gizmo that moves one only appears once it is
  selected, so a marker that cannot be clicked is an entity that cannot be
  placed - and the click that missed deselects, which is the worst answer
  available. The rule is `drawEntityMarker` itself: what draws a marker is what
  answers a click on one, tested in screen space against the same radius the
  marker is drawn at, so it holds at any distance and under an orthographic
  camera - where a world-sized proximity box, which agrees with a screen marker
  at exactly one distance, would not. A light keeps such a box as well, scaled
  by its reach, because a light's wireframe *is* a volume the user points at;
  the marker covers the case where that wireframe has shrunk to nothing. A
  sound has no volume to point at, which is also what keeps a 60-unit `Max
  Distance` from swallowing every click near it. The flown editor camera is
  excluded, the same way it draws no gizmo - it is the viewer.
- Probes, irradiance volumes and decals draw a wire box rather than a marker,
  and are selected from the hierarchy only: those boxes run to tens of units,
  so answering a click anywhere inside one would swallow everything standing in
  it. A particle emitter has no box either way - a ring and a dot at its
  origin, plus its velocity line - and is also selected from the hierarchy,
  because it is the marker that carries the click and an emitter draws none.
- The hierarchy panel highlights the selection.
- The inspector shows components of the selected entity. Entities inside a
  prefab instance are selected and edited like any other; an edit to one becomes
  a per-instance override, the card marks the fields the instance owns, and each
  offers "Revert to prefab" (`framework/prefab_overrides.h`).
- Focus, duplicate, delete, undo, redo, save, save-as, open, preferences
  have dedicated keybinds; the full list lives in `input/editor_keybinds.h`.

## What an instance will not let you do

A scene stores a prefab instance as a reference, a pose and its overrides, and
skips the subtree underneath it - so anything done inside an instance that is
not an override is not written at all. The editor either makes the gesture mean
what it looks like, or refuses it where it happens:

- **Duplicate** instances the prefab again, carrying the overrides over, instead
  of copying the root's components into a childless entity.
- **Undo of a delete** restores the marker and the uids with the rest of the
  subtree, so the instance comes back whole with its overrides intact.
- **Re-parenting into or out of an instance** is refused with a toast in
  `EditorActions::reparentKeepingWorld`, which is where every interactive move
  goes. The root itself still moves anywhere: its `Hierarchy` is the scene's.
- **Add Component** on an instance entity works and warns, because saving the
  instance back over its file is how a component is added to a prefab. The
  inspector carries the rule above the button and toasts it on the add.
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
