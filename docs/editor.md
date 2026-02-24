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
|           Viewport (3D scene)             |
|                                           |
|  [Gizmo]                    [Nav Gizmo]  |
|                                           |
+--------+-------------------+--------------+
| Hierarchy |   Inspector   |              |
|  Panel    |    Panel      |  Bottom Panel|
+--------+-------------------+--------------+
```

### Panels

| Panel | File | Description |
|-------|------|-------------|
| Hierarchy | `panels/hierarchy_panel.cpp` | Entity tree view with parent-child relationships |
| Inspector | `panels/inspector_panel.cpp` | Component editor for selected entity |
| Bottom | `panels/bottom_panel.cpp` | Statistics, render info, system metrics |
| Viewport Overlay | `panels/viewport_overlay.cpp` | FPS counter, entity count overlay |
| Gizmo Overlay | `panels/gizmo_overlay.cpp` | Transform gizmo controls |

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

Viewport-space manipulation handles for translate, rotate, and scale. Supports axis-constrained operations.

## Entity Selection

- Click in viewport to pick entities (ray-AABB intersection)
- Selection highlighted in hierarchy panel
- Inspector shows components of selected entity
- Focus on entity with dedicated keybind
