#pragma once

#include <imgui.h>

namespace Engine {

struct EditorContext;

/**
 * @brief Floating in-viewport tool box (bottom-left), Unity/Unreal style.
 *
 * Compact button strip for switching the active manipulation tool
 * (Select / Move / Rotate / Scale), toggling Local/World space and snap,
 * and acting on the current selection (duplicate / focus / delete).
 * Drawn inside the viewport child window, on top of the 3D scene.
 */
class ViewportToolbar {
    public:
        void draw(EditorContext& ec);

        /**
         * @brief True while the mouse is over the toolbar (so the viewport does not
         * also treat the click as a pick / camera input).
         */
        bool isHovered() const { return m_hovered; }

    private:
        bool m_hovered = false;
};

} // namespace Engine
