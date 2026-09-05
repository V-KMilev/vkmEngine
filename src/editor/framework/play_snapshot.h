#pragma once

#include <string>

namespace Vkm::Engine {

class Scene;
class ResourceManager;

/**
 * @brief The world as it stood when Play was pressed, held until Stop puts it back.
 *
 * Four values meaningful only together, and only between one capture and one
 * restore. That is a state, and naming it is what lets a reader ask whether a
 * session is running - @ref held - rather than infer it from a string's length.
 *
 * Two documents, not one. The scene file names only the assets the scene uses,
 * which is what a scene file is; an asset imported during the session and not
 * yet assigned to anything is in no component and would be lost. So the
 * session's whole asset list is recorded beside the scene and restored first,
 * because the scene resolves assets by name and the names have to be there.
 *
 * This holds the snapshot and nothing else. Deciding *when* to take one, what to
 * do about a partial cook, and what to tell the author afterwards is the
 * controller's - a snapshot has no opinion about the session it belongs to.
 */
class PlaySnapshot {
    public:
        /// True while a snapshot is held, which is what "a session is running" means.
        bool held() const { return !m_scene.empty(); }

        /**
         * @brief Serialize @p scene and the session's whole asset list.
         *
         * @param scene     The world to record.
         * @param resources The session's assets, recorded whole rather than by use.
         * @param dirty     EditorState::sceneDirty now, so Stop can put it back.
         * @param history   CommandStack::revision() now; the same number at Stop
         *                  means the session authored nothing.
         * @return False when the scene could not be serialized, in which case
         *         nothing is held and Stop will have nothing to restore.
         */
        bool capture(const Scene& scene, ResourceManager& resources,
                     bool dirty, unsigned long long history);

        /**
         * @brief Put the assets back, then the scene, into @p scene.
         *
         * Assets first and in place, so the handles the undo history holds go on
         * naming what they named.
         *
         * @return False when the scene did not load, in which case the snapshot
         *         is kept so the live world is left untouched.
         */
        bool restoreInto(Scene& scene, ResourceManager& resources) const;

        /// Drop it. A session ends exactly once, and this is what says so.
        void release();

        /// EditorState::sceneDirty as it stood at capture.
        bool dirtyAtCapture() const { return m_dirty; }

        /**
         * @brief Whether the undo history moved since capture.
         *
         * The one thing that tells authored work from a world that merely
         * simulated: a session that pushed a command changed something a person
         * meant, and the scene those commands addressed is about to be replaced.
         */
        bool historyMoved(unsigned long long now) const { return now != m_history; }

    private:
        std::string        m_scene;    ///< The scene document; empty means no session.
        std::string        m_assets;   ///< The session's whole asset list, beside it.
        bool               m_dirty   = false;
        unsigned long long m_history = 0;
};

} // namespace Vkm::Engine
