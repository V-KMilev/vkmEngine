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
         * @brief State the reason the viewport is showing nothing when the
         *        scene has no camera to render from.
         *
         * The editor has no camera of its own: it flies whichever entity holds
         * an active Camera, so deleting or unticking that one entity empties
         * the viewport and freezes navigation. VisibilitySystem computes the
         * fact and phrases it, but only into the log file, which the editor has
         * no view of.
         *
         * @param ec The frame's editor context; the viewport rect is read off it.
         */
        void drawNoCameraNotice(EditorContext& ec);

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
