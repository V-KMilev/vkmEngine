#pragma once

#include <imgui.h>

namespace Engine {

struct EditorState;

/**
 * @brief Border-drag resize for the docked panels.
 *
 * Extracted from EditorSystem (god-file decomposition). Owns the per-edge
 * drag state and turns mouse hover/drag on the Hierarchy / Inspector /
 * Bottom borders into edits of EditorState's panel sizes. Borderless: it
 * needs no extra layout space, it just probes the mouse against the known
 * panel-edge positions the workspace computed this frame.
 */
class EditorPanelResize {
    public:
        /// Geometry of the current frame's panel area (screen space).
        struct Layout {
            ImVec2 areaStart;            ///< Top-left of the panel row.
            float  mainH  = 0.0f;        ///< Height of the panel row.
            float  workW  = 0.0f;        ///< Root work-area width.
            float  leftW  = 0.0f;        ///< Current left panel width (0 = hidden).
            float  rightW = 0.0f;        ///< Current right panel width (0 = hidden).
            bool   showLeft   = false;
            bool   showRight  = false;
            bool   showBottom = false;
        };

        /// Apply hover cursors and drag deltas; mutates state's panel sizes.
        /// @param blockNew an ImGui item or the gizmo is active; do not
        ///                 start a new resize this frame (ongoing ones continue).
        void process(EditorState& state, const Layout& layout, bool blockNew);

    private:
        bool m_resizingLeft   = false;
        bool m_resizingRight  = false;
        bool m_resizingBottom = false;
};

} // namespace Engine
