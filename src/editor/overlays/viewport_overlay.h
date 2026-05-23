#pragma once

#include <imgui.h>

namespace Engine {

struct EditorContext;

/**
 * @brief In-viewport navigation widget (axis snap gizmo).
 *
 * Drawn inside the viewport child window on top of the 3D scene. Six axis
 * endpoints (+/- X/Y/Z) are clickable to snap the camera view to that axis,
 * orbiting either the current selection or the origin.
 */
class ViewportOverlay {
    public:
        void drawNavigationGizmo(EditorContext& ec);
};

} // namespace Engine
