#pragma once

namespace Engine {

struct EditorContext;

/**
 * @brief Top-centre simulation HUD (Play/Pause, Step, Stop).
 *
 * A small floating icon bar in the viewport (same look as the bottom-left
 * tool box) that drives the engine's global simulation state:
 *  - Play/Pause toggles the engine's SimulationClock - physics and animation
 *    freeze/resume together (the editor opens paused, in Edit mode).
 *  - Step queues one fixed tick of sim time while paused.
 *  - Stop re-pauses and rewinds every Animation to t=0.
 */
class ViewportPlaybar {
    public:
        void draw(EditorContext& ec);

        /// True while the mouse is over the bar (so the viewport does not
        /// also treat the click as a pick / camera input).
        bool isHovered() const { return m_hovered; }

    private:
        bool m_hovered = false;
};

} // namespace Engine
