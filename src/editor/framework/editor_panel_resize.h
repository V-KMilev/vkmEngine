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
        /**
         * @brief Apply hover cursors and drag deltas; mutates state's panel sizes.
         *
         * @param state        Editor state - read for panel visibility, mutated
         *                     for panel sizes on drag.
         * @param areaStart    Top-left of the panel row in screen space.
         * @param mainH        Height of the panel row (workspace minus toolbar
         *                     and bottom panel).
         * @param workW        Root work-area width.
         * @param blockNew     An ImGui item or the gizmo is active; do not
         *                     start a new resize this frame (ongoing continue).
         */
        void process(EditorState& state, ImVec2 areaStart, float mainH, float workW, bool blockNew);

        /**
         * @brief Drop any in-progress drag flags. Call when the editor hides
         * (F5) so a held drag doesn't ghost-resume on re-show.
         */
        void resetDragState() { m_resizingLeft = m_resizingRight = m_resizingBottom = false; }

    private:
        bool m_resizingLeft   = false;
        bool m_resizingRight  = false;
        bool m_resizingBottom = false;
};

} // namespace Engine
