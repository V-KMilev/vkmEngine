#pragma once

namespace Engine {

struct EditorContext;
class SceneIOController;

/**
 * @brief Top-centre simulation HUD (Play/Pause, Step, Stop) with play mode.
 *
 * A small floating icon bar in the viewport (same look as the bottom-left
 * tool box) that drives play mode + the engine's SimulationClock:
 *  - Edit mode (no snapshot): Play captures a scene snapshot, then runs the
 *    SimulationClock so physics/animation/scripts tick. Step enters a paused
 *    play session and advances one fixed tick.
 *  - Play mode (snapshot held): Play/Pause toggles the clock; Step advances
 *    one fixed tick while paused; Stop restores the snapshot and returns to
 *    Edit mode - undoing every transform/spawn the simulation made.
 *
 * The snapshot + restore live on SceneIOController (a restore is just an
 * in-memory reload), so the bar drives play mode through it.
 */
class ViewportPlaybar {
    public:
        void draw(EditorContext& ec, SceneIOController& sceneIO);

        /**
         * @brief True while the mouse is over the bar (so the viewport does not
         * also treat the click as a pick / camera input).
         */
        bool isHovered() const { return m_hovered; }

    private:
        bool m_hovered = false;
};

} // namespace Engine
