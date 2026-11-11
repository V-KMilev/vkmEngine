#pragma once

#include <vector>

#include "ecs/entity.h"
#include "selection.h"
#include "session/play_snapshot.h"

namespace Vkm::Engine {

struct EditorState;
struct FrameContext;

/**
 * @brief The entities the editor marks in a world, by their ids.
 */
struct EditorMarks {
    Selection             selection;
    std::vector<EntityId> hidden;  ///< EditorState::hiddenInView.
    std::vector<EntityId> locked;  ///< EditorState::lockedInView.
};

/**
 * @brief The play session: from Play until Stop, the world as Play found it, the editor's view
 *        in the running world, and the Edit mode put back when the session ends.
 *
 * Stop puts the snapshot back through SceneSession::stopPlaySession, which replaces the world as
 * an open does; an open ends the session too (end()).
 */
class PlaySession {
    public:
        PlaySession() = default;
        ~PlaySession() = default;

        PlaySession(const PlaySession& other) = delete;
        PlaySession& operator=(const PlaySession& other) = delete;

        PlaySession(PlaySession && other) = delete;
        PlaySession& operator=(PlaySession && other) = delete;

    public:
        /**
         * @brief Take Play's snapshot: end a Render Settings tier preview, then capture().
         *
         * The clock is the caller's to start.
         *
         * @param ctx Frame context supplying the scene, resources and render settings.
         * @param state Editor state holding the preview; see capture().
         * @return What capture() answered.
         */
        bool start(FrameContext& ctx, EditorState& state);

        /**
         * @brief Snapshot the scene for Stop, cooking every loaded asset and writing the terrains first.
         *
         * The snapshot names its assets, which restore only from the cooked library; a terrain comes
         * back from its file. May block on an import still loading. Play goes through start().
         *
         * @param ctx Supplies the scene and resources to snapshot.
         * @param state Its dirty flag and its marks are kept for Stop and its undo history parked until
         *              then; takes a toast for what failed.
         * @param action The gesture taking it, as its toasts name it.
         * @return False when a terrain could not be written or the scene serialized; a partial cook
         *         answers true, with a toast naming what the restore may lose.
         */
        bool capture(FrameContext& ctx, EditorState& state, const char* action);

        /**
         * @brief Drop the play snapshot and put the clock back in Edit mode.
         *
         * What a path that replaces the world must do, or Stop would restore the dead world over the
         * new one, under the new scene's name.
         *
         * @param ctx Frame context supplying the clock the session ran on.
         */
        void end(FrameContext& ctx);

        /**
         * @brief Show the editor's view in the running session, or the game's again.
         *
         * Ejected, the viewport works as in Edit mode, the game hears no input and a cursor it grabbed
         * is freed until the way back in. Nothing outside a session.
         *
         * @param ctx     Frame context owning the window whose cursor moves.
         * @param ejected true to show the editor's view.
         */
        void setEjected(FrameContext& ctx, bool ejected);

        /**
         * @brief Free the cursor again if the game grabbed it while ejected (PlaySnapshot::holdCursorFree).
         *
         * @param ctx Frame context owning the window.
         */
        void holdEjectedCursorFree(FrameContext& ctx);

        /**
         * @brief Whether a play session is live, from Play until Stop.
         *
         * Ask this, not the clock, which is paused in Edit mode too.
         *
         * @return true while the authored scene is held aside and the world on screen is the simulation's.
         */
        bool isPlaying() const { return m_snapshot.held(); }

        /**
         * @brief Whether the running session's viewport shows the editor's view.
         *
         * @return false outside a session.
         */
        bool isEjected() const { return m_snapshot.ejected(); }

        /// The world as Play found it, which Stop puts back (SceneSession::stopPlaySession).
        const PlaySnapshot& snapshot() const { return m_snapshot; }

        /**
         * @brief What the editor marked in the world as Play found it.
         *
         * Stop puts it back when the game read a world of its own in play: that world's ids are
         * numbered afresh, so what was marked in it would name others.
         */
        const EditorMarks& marksAtPlay() const { return m_marks; }

    private:
        PlaySnapshot m_snapshot;  ///< Held from Play until the session ends.
        EditorMarks  m_marks;     ///< Taken with the snapshot.
};

} // namespace Vkm::Engine
