#pragma once

namespace Vkm::Engine {

struct EditorContext;
class SceneIOController;

/**
 * @brief Top-centre simulation HUD (Play/Pause, Step, Stop, Eject) with play mode.
 *
 * Play snapshots the scene and runs the Clock; Step enters a paused session and advances
 * one tick; Stop restores the snapshot; Eject toggles the editor's view of the game.
 *
 * Pause, Resume and Step also hold and release mixer voices, since audio runs off the
 * frame, not simulation time. A step ends like a pause: its tick is heard, then held.
 */
class PlaybackBar {
    public:
        PlaybackBar() = default;
        ~PlaybackBar() = default;

        PlaybackBar(const PlaybackBar& other) = delete;
        PlaybackBar& operator=(const PlaybackBar& other) = delete;

        PlaybackBar(PlaybackBar && other) = delete;
        PlaybackBar& operator=(PlaybackBar && other) = delete;

    public:
        /**
         * @brief Draw the bar, and the play-mode frame and caption while a session runs.
         *
         * Centred on the viewport, within the span the other top-row overlays leave free.
         *
         * @param ec      The frame's editor context.
         * @param sceneIO The session the buttons drive.
         * @param left    The free span's left end, in viewport coordinates.
         * @param right   Its right end.
         */
        void draw(EditorContext& ec, SceneIOController& sceneIO, float left, float right);

        /**
         * @brief Answer the transport keybinds: Play / Stop, Pause / Resume, Eject / Return.
         *
         * Runs even when the bar is hidden or the game has the keyboard - they are the way
         * out of a session that grabbed the cursor; the caller asks
         * InputOwnership::editorHasSessionKeys first. None repeats while held. A game bound
         * to the same keys sees them first; a pause withdraws only the edges latched for the
         * next tick.
         *
         * @param ec For the keybinds, the clock and the input.
         * @param sceneIO The session the keys drive.
         */
        void processKeys(EditorContext& ec, SceneIOController& sceneIO);

        /**
         * @brief Whether the mouse was over the bar as last drawn.
         *
         * @return True over it, so the viewport does not also take the click.
         */
        bool isHovered() const { return m_hovered; }

    private:
        /**
         * @brief The first button: start a running session, or pause or resume the current one.
         *
         * @param ec For the clock and the audio device.
         * @param sceneIO Snapshots a start.
         */
        void playOrPause(EditorContext& ec, SceneIOController& sceneIO);

    private:
        bool m_hovered = false;

        /**
         * @brief Whether a stepped tick has been queued and not yet held.
         *
         * Read on the next draw, the first moment the tick has run and its voices exist.
         */
        bool m_stepPending = false;
};

} // namespace Vkm::Engine
