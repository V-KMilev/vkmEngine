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
|             Bottom panel (Animation / Errors tabs)            |
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
| Bottom              | `panels/bottom_panel.cpp`             | Two tabs: Animation (keyframe editor) and Errors (recoverable engine failures) |
| Render Settings     | `panels/render_settings_panel.cpp`    | Render quality tuning: `RenderSettings` (debug view / grid / MSAA, texture filtering, GTAO, bloom, shadows, probes) plus the `VisibilitySystem` culling thresholds; opened from Window > Render Settings |
| Material Editor     | `panels/material_editor_panel.cpp`          | Per-material PBR inspector with live preview (renders the real pipeline)    |
| Asset Browser       | `panels/asset_browser_panel.cpp`            | Thumbnail grid of materials / meshes / textures; pickable into the inspector|
| Preferences         | `panels/preferences_panel.cpp`        | Floating editor/app settings window (Edit > Preferences, Ctrl+,)            |
| Viewport Overlay    | `overlays/viewport_overlay.cpp`       | The axis navigation gizmo, top-right of the viewport (click an axis to snap the camera) |
| Gizmo Overlay       | `overlays/gizmo_overlay.cpp`          | Transform gizmo drawing + light/camera gizmos                               |
| Viewport Toolbar    | `overlays/viewport_toolbar.cpp`       | In-viewport icon tool box: tool/space/snap + selection actions              |
| Playback Bar        | `overlays/playback_bar.cpp`           | Top-centre Play/Pause/Stop transport for all Animation components           |

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
  **Animation** (the keyframe editor below) and **Errors**. The Errors
  tab is where recoverable engine failures surface - a script hook that
  throws does not kill the frame, it lands here - listing
  `EngineErrorLog` entries newest first with a Clear button.
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

Controls (icon buttons, shared `editor_icons.{h,cpp}`):

- Playback: Play/Pause, Stop (rewind), global **Set Key** (add/replace
  a keyframe on all three tracks at the current time), Loop, Speed.
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
  card: loop and speed round-trip with the scene so they push an edit, while
  play, stop and the scrubber do not. Scrubbing works while paused because the
  pose system composes every frame. A clip cooked against a different rig is
  called out in red on the card, where the pairing is being made, rather than
  only in the log. Blend state is deliberately absent: a crossfade is started
  from code through `Animator::crossFadeTo` and is never serialized.
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

## Undo / redo

Every editor mutation goes through a `Command` that captures the
"before" state and applies the "after" state. The stack is bounded
(default 200 entries) and is cleared on scene load (entity IDs and
component topology are not comparable across a swap).

Available commands (in `framework/editor_commands.h`):

- `TransformChangeCommand`: position / rotation / scale on an entity.
  Coalesces consecutive edits on the same entity, so a gizmo drag or a
  stream of inspector micro-edits collapses to one undo step.
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
- `RenameAssetCommand<HandleType>`: undoable asset rename (routes through
  `ResourceManager::rename` so the name index stays consistent).

Templated commands are emitted out of line via `extern template` in the
header and instantiated once in `editor_commands.cpp` so each
translation unit doesn't recompile the bodies. Both blocks expand from
the single `VKM_EDITOR_COMMAND_COMPONENTS` list, so they cannot drift.

`CommandStack::push` calls `Command::tryMerge` against the top of the
undo stack first; that is where transform drag coalescing happens.

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
7. Boot its scene through `bootProjectScene`, the same rule both binaries use.

A path that names a file rather than a directory still works - `findProjectRoot`
walks up to the owning `project.json`, so dropping in a scene opens its project.

Command-line `vkm_editor <project>` does the same thing at startup, before any
path is composed. See [system/io.md](system/io.md#projects-and-the-two-roots).

## Scene I/O

`SceneIOController` owns the New / Open / Save / Save-As flow:

- Drives the file-picker modals.
- Hands off to `SceneSerializer::save` / `load` (engine-side; see
  [IO and serialization](system/io.md)).
- After a successful load it clears the command stack and rebinds the
  camera if the loaded scene defined one.
- Maintains a recent-scenes list cached when the Open dialog is opened
  (so re-opening doesn't re-scan disk every frame).

## Material preview / Asset browser

Both the Material Editor and the Asset Browser show live PBR previews.
These are rendered by the backend's dedicated preview path
(`RenderBackend::renderPreview`, backed by `GLPreview`) - **not** the full
frame pipeline. It is a minimal forward + composite render of the material on
a preview mesh into a small offscreen target, kept separate from the main
19-pass path. Results are cached per asset (keyed by handle + version) with a
small per-frame bake budget, so the Asset Browser grid amortizes thumbnail
generation across frames while the Material Editor's live view re-renders each
frame.

## CameraControllerSystem

FPS-style fly camera; a `System` on `SystemStage::Input`. Updates the
active camera's transform from input each frame.

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

Light and camera entities show their own gizmos in `gizmo_overlay.cpp`
(directional rays, cone projections, frustum lines, area-light edges).

The `View` menu adds three overlays that are off by default because they draw
for every matching entity rather than the selection: **Show Colliders** (the
boxes the solver collides against), **Show Bounds** (the world AABB of every
visible entity) and **Show Skeletons** (each posed rig's bones, straight out of
`FrameContext::poses` - segments joint to joint, plus an axis triad per bone on
the selected rig, which is what makes a bone's *orientation* visible and not
just its position).

## Entity selection and shortcuts

- Click in the viewport to pick entities (ray-AABB against the visible
  set's cached world AABBs).
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
- **A successful save drops the undo history.** The subtree's entities are
  rebuilt from the file on the next load, so nothing already on the stack
  describes the scene any more - the same reason a scene load clears it. A
  refused save leaves the history alone.
