#pragma once

namespace Vkm::Engine {

struct EditorContext;
class SceneSession;

/**
 * @brief Top-center simulation HUD: Play/Pause, Step, Stop and Eject, with play mode.
 *
 * Play snapshots the scene and runs the Clock; Step enters a paused session and advances one tick;
 * Stop restores the snapshot; Eject toggles the editor's view of the game.
 *
 * Pause, Resume and Step also hold and release mixer voices: audio runs off the frame, not the clock.
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
         * Centered on the viewport, within the span the other top-row overlays leave free.
         *
         * @param ec      The frame's editor context.
         * @param left    The free span's left end, in viewport coordinates.
         * @param right   Its right end.
         * @param inBand  Whether the bar has a band of its own above the game's view: the caption goes
         *                beside it rather than below, over the game.
         */
        void draw(EditorContext& ec, float left, float right, bool inBand);

        /**
         * @brief Answer the transport keybinds: Play / Stop, Pause / Resume, Eject / Return.
         *
         * Runs even when the bar is hidden or the game has the keyboard, as the way out of a session
         * that grabbed the cursor; the caller asks InputOwnership::editorHasSessionKeys first.
         *
         * @param ec For the keybinds, the clock and the input.
         */
        void processKeys(EditorContext& ec);

        /**
         * @brief Whether the mouse was over the bar as last drawn.
         *
         * @return True over it, so the viewport does not also take the click.
         */
        bool isHovered() const { return m_hovered; }

        /**
         * @brief Start the running session over, as a game's restartScene() asks: Stop, then Play.
         *
         * A Stop that could not put the scene back leaves the session paused, and nothing is played.
         *
         * @param ec For the clock and the audio device.
         */
        void restart(EditorContext& ec);

    private:
        /**
         * @brief The first button: start a running session, or pause or resume the current one.
         *
         * @param ec For the clock and the audio device.
         */
        void playOrPause(EditorContext& ec);

    private:
        bool m_hovered = false;

        bool m_stepPending = false;  ///< A stepped tick is queued and its voices not yet held; read on
                                     ///< the next draw, the first moment the tick has run.
};

} // namespace Vkm::Engine
