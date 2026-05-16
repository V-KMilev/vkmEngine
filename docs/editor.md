# Editor

The editor provides an ImGui-based interface for scene inspection and manipulation.

## Key Files

- `src/editor/editor_system.h` -- EditorSystem (System subclass)
- `src/editor/camera_controller.h` -- CameraController (System subclass)
- `src/editor/transform_gizmo.h` -- Transform gizmo for viewport manipulation
- `src/editor/editor_common.h` -- Shared editor state and types
- `src/editor/panels/` -- UI panels

## EditorSystem

Inherits `System`. Manages the editor UI layout and all panels. Owns the ImGui rendering context and processes editor input.

### Panel Layout

```
+-------------------------------------------+
|              [Play/Stop]                  |
|                Viewport (3D scene)        |
|  [Gizmo]                    [Nav Gizmo]  |
|  [Toolbar]                                |
+--------+-------------------+--------------+
| Hierarchy |   Inspector   |              |
|  Panel    |    Panel      |  Bottom Panel|
+--------+-------------------+--------------+
```

The in-viewport toolbar (bottom-left) is a Unity/Unreal-style tool box
drawn with vector icons (no icon font dependency): Select, Move, Rotate,
Scale, then Local/World space, Snap, then Duplicate, Focus, Delete. The
active tool is accent-highlighted; selection actions disable when nothing
is selected; every button has a tooltip with its keybind. It replaces the
old gizmo-mode text overlay.

The transport bar (top-centre) is the engine-style global playback
control: **Play/Pause** toggles all Animation components, **Stop** pauses
and rewinds them. It replaces the old "Pause/Resume All Animations" menu
items and Statistics buttons.

### Panels

| Panel | File | Description |
|-------|------|-------------|
| Hierarchy | `panels/hierarchy_panel.cpp` | Entity tree; drag a node onto another to reparent (cycle-safe); context-menu Unparent |
| Inspector | `panels/inspector_panel.cpp` | Component editor; animation easing/keyframes; Camera "Set as Main"; Hierarchy Unparent |
| Bottom | `panels/bottom_panel.cpp` | Per-scene working surface: grouped master-detail browser |
| Preferences | `panels/preferences_panel.cpp` | Floating editor/app settings window (Edit > Preferences, Ctrl+,) |
| Viewport Overlay | `panels/viewport_overlay.cpp` | FPS counter, entity count overlay |
| Gizmo Overlay | `panels/gizmo_overlay.cpp` | Transform gizmo controls |
| Viewport Toolbar | `panels/viewport_toolbar.cpp` | In-viewport icon tool box: tool/space/snap + selection actions |
| Playback Bar | `panels/playback_bar.cpp` | Top-centre Play/Pause/Stop transport for all animations |

The editor separates **per-scene working data** from **editor/app
preferences**:

- **Bottom panel** -- a grouped master-detail browser of the scene
  surface. Left nav groups: `WORLD` (Environment, Rendering), `TOOLS`
  (Animation), `INFO` (Statistics).
- **Preferences window** -- a floating, closeable window opened from
  `Edit > Preferences` (Ctrl+,). Grouped nav: `VIEWPORT` (Camera fly-cam,
  Gizmo snap defaults), `APPLICATION` (Display), `INPUT` (Keybinds).
  Camera/Display/Gizmo/Keybinds used to live in the bottom panel; they
  moved here because they are user/app config, not scene data. The Gizmo
  section is snap-only -- the active tool and Local/World space live on
  the viewport toolbar, not in a settings list.

### Animation editor (Bottom panel > Animation)

Operates on the selected entity. When it has **no** `Animation`, the whole
editor is shown **disabled** (a preview of the UI) with a single centered
**Add Animation Component** button -- no explanatory prose. New animations
default to a 5 s `length` so the timeline is immediately usable.

Controls (icon buttons, shared `editor_icons.{h,cpp}`):

- Playback: Play/Pause, Stop (rewind), global **Set Key** (add/replace a
  keyframe on all 3 tracks at the current time), Loop, Speed.
- **Length** (`Animation::length`, serialized): explicit animation
  duration in seconds; `0` = auto from the last keyframe. The timeline
  spans `max(last keyframe, length)`, so you can set a length and place
  keys anywhere along it (looping uses this duration too).
- A scrubbable timeline: ruler, per-track keyframe dots (P/R/S), playhead.
  Drag empty timeline to scrub; **drag a keyframe dot to retime it**
  (hover highlights it; cursor switches to the resize arrow).
- `Time` is a typed `InputFloat` so you place the playhead exactly.
- Per track (Position/Rotation/Scale): `+` add/replace key from the live
  transform, trash to clear, easing dropdown, and a keyframe **table**
  with **editable Value** (XYZ for pos/scale, Euler degrees for rotation),
  editable **Time** (commit-on-deactivate), and per-row delete.

Scrubbing or editing while paused live-previews the pose in the viewport.
Re-keying at an existing time **replaces** that keyframe
(`AnimationTrack::setKeyframe`) instead of stacking a zero-length segment.
Editing/keyframe APIs (`setKeyframe`, `removeKeyframe`, `setKeyframeTime`,
`setKeyframeValue`, `keyframeCount`) and the shared easing dropdown are
also used by the Inspector. Shared colors/metrics live in
`editor_style.h`; vector icons in `editor_icons.h`.

The Inspector's Animation section is now a compact summary (play/stop,
loop, speed, time slider, track/key counts) that points here for full
keyframe editing.

## CameraController

FPS-style fly camera. Inherits `System`, updates camera transform from input each frame.

### Controls

| Input | Action |
|-------|--------|
| Right mouse button (hold) | Enable look mode (cursor disabled) |
| Mouse movement (in look mode) | Rotate camera (yaw/pitch) |
| W/A/S/D | Move forward/left/back/right |
| Q/E | Move down/up |
| Shift (hold) | Speed boost (3x) |
| Scroll wheel | Zoom (adjust FoV) |

### Configuration

```cpp
struct CameraControllerSettings {
    float zoomSensitivity   = 0.02f;
    float lookSensitivity   = 0.002f;
    float moveSpeed         = 10.0f;
    float speedBoost        = 3.0f;
    float scrollMultiplier  = 2.0f;
    float minPitch          = -90.0f;
    float maxPitch          = 90.0f;
};
```

Keybindings are configurable via the keybindings system.

## Transform Gizmo

Viewport-space manipulation handles for translate, rotate, and scale. Supports
axis-constrained operations. A fourth mode, **Select**, draws no handles
(pick-only) so clicks always select rather than drag.

Default tool keybinds (active only when the camera is *not* in fly mode):
`Q` Select, `W` Move, `E` Rotate, `R` Scale, `X` toggle Local/World. All are
rebindable from the Bottom panel's Keybinds section.

## Entity Selection

- Click in viewport to pick entities (ray-AABB intersection)
- Selection highlighted in hierarchy panel
- Inspector shows components of selected entity
- Focus on entity with dedicated keybind
