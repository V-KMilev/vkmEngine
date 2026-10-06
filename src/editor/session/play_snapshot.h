#pragma once

#include <string>

#include "io/scene/prefab.h"
#include "platform/window/window_manager.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;

/**
 * @brief The world as it stood when Play was pressed, held until Stop puts it back.
 *
 * The scene file names only the assets it uses, so the whole asset list is
 * recorded beside it, keeping unassigned imports. Prefab-built slots, which the
 * scene stores as a reference, are recorded too: the undo history addresses
 * entities by slot. The ejection ends with the session in release().
 */
class PlaySnapshot {
    public:
        PlaySnapshot() = default;
        ~PlaySnapshot() = default;

        PlaySnapshot(const PlaySnapshot& other) = delete;
        PlaySnapshot& operator=(const PlaySnapshot& other) = delete;

        PlaySnapshot(PlaySnapshot && other) = delete;
        PlaySnapshot& operator=(PlaySnapshot && other) = delete;

    public:
        /// True while a session is running.
        bool held() const { return !m_scene.empty(); }

        /**
         * @brief Serialize @p scene and the session's whole asset list.
         *
         * @param scene     World to record.
         * @param resources Recorded whole rather than by use.
         * @param dirty     EditorState::sceneDirty now, so Stop can put it back.
         * @return False when the scene could not be serialized; nothing is held.
         */
        bool capture(const Scene& scene, ResourceManager& resources, bool dirty);

        /**
         * @brief Put the assets back, then the scene, into @p scene.
         *
         * Assets in place, so the undo history's handles keep their meaning.
         * Entities return to their slots while each prefab still builds the same
         * entities; @ref restoredInPlace says whether it did.
         *
         * @param scene     Live world, rebuilt.
         * @param resources Reloaded from the recorded list.
         * @return False when the scene did not load; the snapshot is kept.
         */
        bool restoreInto(Scene& scene, ResourceManager& resources) const;

        /// End the session, and the ejection with it.
        void release();

        /**
         * @brief Show the editor's view of the running session, or the game's again.
         *
         * Ejecting frees a grabbed cursor; returning hands back what the game last
         * set. No-op outside a session.
         *
         * @param ejected true to show the editor's view.
         * @param window  Owns the cursor.
         */
        void setEjected(bool ejected, WindowManager& window);

        /**
         * @brief Free the cursor again if the game grabbed it while ejected.
         *
         * What the game set is kept for return. Call once a frame, after the
         * game's systems, never while the editor's view holds the cursor for a look.
         *
         * @param window Owns the cursor.
         */
        void holdCursorFree(WindowManager& window);

        /**
         * @brief Whether the running session shows the editor's view.
         *
         * @return false outside a session.
         */
        bool ejected() const { return m_ejected; }

        /**
         * @brief Whether every entity a prefab instance built stands in the slot
         *        it held at capture.
         *
         * False when a prefab changed on disk or would not open; a history kept
         * across the restore would then address other entities.
         *
         * @param scene World restoreInto() rebuilt.
         * @return true when the restore put every slot back.
         */
        bool restoredInPlace(const Scene& scene) const {
            return Prefab::instanceSlotsOf(scene) == m_instanceSlots;
        }

        /// EditorState::sceneDirty at capture.
        bool dirtyAtCapture() const { return m_dirty; }

    private:
        std::string           m_scene;          ///< Empty means no session.
        std::string           m_assets;         ///< Whole asset list.
        Prefab::InstanceSlots m_instanceSlots;  ///< Where each instance's entities stood.
        bool                  m_dirty = false;
        bool                  m_ejected = false;                ///< See setEjected.
        /// Handed back on return.
        CursorMode            m_gameCursor = CursorMode::Normal;
};

} // namespace Vkm::Engine
