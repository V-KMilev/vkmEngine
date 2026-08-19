#pragma once

#include <imgui.h>

namespace Vkm::Engine {

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

        /**
         * @brief True while the mouse is over one of the axis endpoints (so the
         * viewport does not also treat the click as a pick).
         *
         * @return true when the last drawNavigationGizmo() hit an endpoint.
         */
        bool isHovered() const { return m_hovered; }

    private:
        bool m_hovered = false;  ///< An axis endpoint was under the cursor this frame.
};

} // namespace Vkm::Engine
