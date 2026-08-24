#pragma once

namespace Vkm::Engine {

struct EditorContext;
class SceneIOController;

/**
 * @brief Top-centre simulation HUD (Play/Pause, Step, Stop) with play mode.
 *
 * A small floating icon bar in the viewport (same look as the bottom-left
 * tool box) that drives play mode + the engine's Clock:
 *  - Edit mode (no snapshot): Play captures a scene snapshot, then runs the
 *    Clock so physics/animation/scripts tick. Step enters a paused
 *    play session and advances one fixed tick.
 *  - Play mode (snapshot held): Play/Pause toggles the clock; Step advances
 *    one fixed tick while paused; Stop restores the snapshot and returns to
 *    Edit mode - undoing every transform/spawn the simulation made.
 *
 * Pause, Resume and Step also hold and release the voices the mixer is playing,
 * which the Clock cannot do for them: audio runs off the frame rather than off
 * simulation time, so that a shipped game's pause menu keeps its music. Here
 * the world was frozen to be looked at, so the bar reaches the device itself.
 * A step is a pause with one tick in the middle, so it ends the way a pause
 * does - what the tick set going is heard for that tick and then held.
 *
 * The snapshot + restore live on SceneIOController (a restore is just an
 * in-memory reload), so the bar drives play mode through it.
 */
class PlaybackBar {
    public:
        void draw(EditorContext& ec, SceneIOController& sceneIO);

        /**
         * @brief True while the mouse is over the bar (so the viewport does not
         * also treat the click as a pick / camera input).
         */
        bool isHovered() const { return m_hovered; }

    private:
        bool m_hovered = false;

        /**
         * @brief Whether a stepped tick has been queued and not yet held.
         *
         * Set when Step queues the tick, read on the next draw - by which time
         * the Clock has fed the step, the systems have run against it and any
         * voice it started exists. There is no earlier moment: the bar draws in
         * the UI stage, after the frame it is asking for has already happened.
         */
        bool m_stepPending = false;
};

} // namespace Vkm::Engine
