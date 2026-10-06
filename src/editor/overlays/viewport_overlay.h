#pragma once

#include <imgui.h>

namespace Vkm::Engine {

struct EditorContext;

/**
 * @brief In-viewport axis gizmo: click an endpoint to snap the view to that axis.
 *
 * The view orbits the selection or the origin.
 */
class ViewportOverlay {
    public:
        ViewportOverlay() = default;
        ~ViewportOverlay() = default;

        ViewportOverlay(const ViewportOverlay& other) = delete;
        ViewportOverlay& operator=(const ViewportOverlay& other) = delete;

        ViewportOverlay(ViewportOverlay && other) = delete;
        ViewportOverlay& operator=(ViewportOverlay && other) = delete;

    public:
        /**
         * @brief Draw the axes in the viewport's bottom-right corner and answer a click on an endpoint.
         *
         * @param ec    Supplies the viewport rect.
         * @param shown Whether the axes are drawn at all.
         */
        void drawNavigationGizmo(EditorContext& ec, bool shown);

        /**
         * @brief Say why the viewport is empty when a session's game has no active Camera.
         *
         * @param ec Supplies the viewport rect.
         */
        void drawNoCameraNotice(EditorContext& ec);

        /**
         * @brief Whether the mouse is over an axis endpoint, so the viewport does not also pick.
         *
         * @return True when the last drawNavigationGizmo() hit an endpoint.
         */
        bool isHovered() const { return m_hovered; }

        /**
         * @brief How far in from their corner's edges the axes reach, inset included.
         *
         * @return Screen pixels on each axis.
         */
        static float reach();

    private:
        bool m_hovered = false;  ///< An endpoint was under the cursor this frame.
};

} // namespace Vkm::Engine
