#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ecs/prefab_instances.h"
#include "vkm_platform/window/window_manager.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;

/**
 * @brief The world as Play found it, held until Stop puts it back.
 *
 * Every asset is recorded, keeping unassigned imports, and so are each instance's slots and every
 * slot's generation, so each entity comes back under the id the undo history holds it by, and every
 * reference to an entity already deleted, which the document holds as none: an undo that brings that
 * entity back points them at it again.
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
         * @brief Put the assets back in place, then the scene, into @p scene.
         *
         * In place, so the undo history's handles keep their meaning; @ref restoredInPlace says
         * whether the entities came back under their ids.
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
         * set. Nothing outside a session.
         *
         * @param ejected true to show the editor's view.
         * @param window  Owns the cursor.
         */
        void setEjected(bool ejected, Platform::WindowManager& window);

        /**
         * @brief Free the cursor again if the game grabbed it while ejected, keeping what it set.
         *
         * Call once a frame after the game's systems, never while the editor's view holds it for a look.
         *
         * @param window Owns the cursor.
         */
        void holdCursorFree(Platform::WindowManager& window);

        /**
         * @brief Whether the running session shows the editor's view.
         *
         * @return false outside a session.
         */
        bool ejected() const { return m_ejected; }

        /**
         * @brief Whether every entity a prefab instance built stands in the slot it held at capture.
         *
         * False when a prefab changed on disk: a kept history would address others.
         *
         * @param scene World restoreInto() rebuilt.
         * @return true when the restore put every slot back.
         */
        bool restoredInPlace(const Scene& scene) const {
            return Prefab::instanceSlotsOf(scene) == m_instanceSlots;
        }

        /// EditorState::sceneDirty at capture.
        bool dirtyAtCapture() const { return m_dirty; }

        /**
         * @brief Scene::epoch() of the world capture() took.
         *
         * @return Not the world's epoch once the game has read a world of its own in play, whose
         *         ids are numbered afresh.
         */
        uint64_t epochAtCapture() const { return m_epoch; }

    private:
        /// Writes one component's references back as capture() found them.
        using PutBack = std::function<void(Scene&)>;

    private:
        std::string           m_scene;                                      ///< Empty means no session.
        std::string           m_assets;                                     ///< Whole asset list.
        Prefab::InstanceSlots m_instanceSlots;                              ///< Where each instance's
                                                                            ///< entities stood.
        std::vector<uint32_t> m_generations;                                ///< Scene::slotGenerations
                                                                            ///< at capture.
        std::vector<PutBack>  m_deadReferences;                             ///< Each component naming an
                                                                            ///< entity dead at capture.
        uint64_t              m_epoch      = 0;                             ///< See epochAtCapture().
        bool                  m_dirty      = false;
        bool                  m_ejected    = false;                         ///< See setEjected.
        Platform::CursorMode  m_gameCursor = Platform::CursorMode::Normal;  ///< The game's, handed back.
};

} // namespace Vkm::Engine
