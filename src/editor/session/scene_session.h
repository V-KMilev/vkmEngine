#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "ui/file_picker.h"

namespace Vkm::Engine {

class Scene;
struct FrameContext;
struct EditorState;
class BehaviorSystem;
class CameraControllerSystem;
class LoadingSystem;
class PlaySession;
class PreviewCache;

/**
 * @brief Owns editor scene replacement: file I/O, and Stop putting Play's snapshot back.
 *
 * Stop swaps the snapshot back through the housekeeping a load runs (afterSceneReplace), which is
 * why it is here; the session it ends is PlaySession's.
 */
class SceneSession {
    public:
        SceneSession(
            CameraControllerSystem& cameraController,
            PreviewCache& previews,
            BehaviorSystem&         behaviors,
            const LoadingSystem&    loading,
            PlaySession&            play
        );
        ~SceneSession();

        SceneSession(const SceneSession& other) = delete;
        SceneSession& operator=(const SceneSession& other) = delete;

        SceneSession(SceneSession && other) = delete;
        SceneSession& operator=(SceneSession && other) = delete;

        /**
         * @brief Save to the current path, or ask for one; refused during a play session.
         *
         * Clears EditorState::sceneDirty on success.
         *
         * @param ctx Supplies the scene and resources to write.
         * @param state Takes the dirty flag, the recents and the toasts.
         */
        void save(FrameContext& ctx, EditorState& state);

        /**
         * @brief Replace the scene with buildDefaultScene's; the caller guards unsaved changes first.
         *
         * @param ctx Frame context owning the scene being replaced.
         * @param state Editor state whose scene-scoped parts are reset.
         */
        void newScene(FrameContext& ctx, EditorState& state);

        /**
         * @brief Empty the scene for a swap, doing what Scene::clear() does not; the caller fills it.
         *
         * Behaviors get onDestroy while their code is loaded; the undo stack goes, as entity ids do
         * not carry across scenes; the outgoing previews are released; and the save path is forgotten,
         * so a later Save cannot write into what was open before.
         *
         * @param ctx Frame context owning the scene being replaced.
         * @param state Editor state whose scene-scoped parts are reset.
         */
        void beginSceneReplace(FrameContext& ctx, EditorState& state);
        /**
         * @brief Open the Save-As prompt at the next drawDialogs(), from the scope that keeps it past the
         *        menu; refused, with a toast, in a play session.
         *
         * @param state Takes the refusal's toast.
         */
        void requestSaveAs(EditorState& state);
        /// Open the Load-Scene picker at the next drawDialogs().
        void requestLoad();
        /**
         * @brief Load @p path; a failed read keeps the outgoing scene live, and its path with it.
         *
         * @param ctx Frame context owning the scene being replaced.
         * @param state Editor state whose scene-scoped parts are reset.
         * @param path Absolute path of the scene file to open.
         */
        void loadPath(FrameContext& ctx, EditorState& state, const std::string& path);

        /**
         * @brief Draw the pending Save-As and Load modals; call every frame, as a Save may wait on a name.
         *
         * @param ctx Supplies the scene and resources to save or load.
         * @param state Read for paths and flags, and updated on a completed pick.
         */
        void drawDialogs(FrameContext& ctx, EditorState& state);

        /**
         * @brief Keep a copy of a dirty scene where a crash cannot take it, outside a session, and
         *        recover it when its prompt asked (Ask::RecoverScene).
         *
         * Every SceneRecovery::INTERVAL, in the user's folder (Roots::userRoot), never the project's.
         * Called before the ImGui frame, as a recovery opens a world.
         *
         * @param ctx   Supplies the scene, its assets and the frame's time.
         * @param state Says whether the scene is dirty.
         */
        void serviceRecovery(FrameContext& ctx, EditorState& state);

        /// Remove the open scene's copy: its changes were saved or given up.
        void discardRecovery();

        /**
         * @brief Swap the captured snapshot back in, through load()'s housekeeping, and release it.
         *
         * Nothing without a snapshot. A failed restore keeps the snapshot, leaving the played scene
         * whole. Entities and edits made during the session are lost, and a toast says so.
         *
         * @param ctx Supplies the scene and resources to restore into.
         * @param state Its selection, dirty flag and toasts are updated to match the restored scene.
         */
        void restoreSnapshot(FrameContext& ctx, EditorState& state);

        /**
         * @brief Stop the clock and restore the authored scene; nothing outside a session.
         *
         * @param ctx Frame context owning the clock and the scene.
         * @param state Editor state the restore updates.
         */
        void stopPlaySession(FrameContext& ctx, EditorState& state);

        /**
         * @brief Take @p path as the file the scene already in the world came from, reading nothing.
         *
         * For a world this controller did not read, as bootProjectWorld's. An empty @p path, for a
         * world with no file, leaves no save path, so a stand-in for a failed load cannot overwrite it.
         *
         * @param scene The scene already in the world, framed when it has no saved viewpoint.
         * @param state Editor state whose recent-scenes list gains @p path.
         * @param path Absolute path of the scene file, or empty for none.
         */
        void adoptPath(const Scene& scene, EditorState& state, const std::string& path);

        /**
         * @brief Keep the viewpoint the open scene is seen from, under its path, in EditorState::sceneViews.
         *
         * Call it as a scene is left and before the settings are written.
         *
         * @param state Editor state whose sceneViews takes the entry.
         */
        void rememberView(EditorState& state) const;

        bool hasPath() const { return !m_currentScenePath.empty(); }
        const std::string& path() const { return m_currentScenePath; }

        /**
         * @brief Whether a Save-As prompt is queued or up.
         *
         * A scene still dirty with none up means the Save-As was canceled: the action waiting drops.
         *
         * @return true while the prompt is queued or up.
         */
        bool isSaveDialogActive() const;

    private:
        /**
         * @brief Whether a swap takes the undo history with it; see afterSceneReplace.
         */
        enum class History : uint8_t { Drop, Keep };

    private:
        /**
         * @brief Write the live scene to @p path, cooking every loaded asset first, and record it.
         *
         * Only success clears the dirty flag and promotes the path in the recents, so a failed save
         * never looks like one that worked. Refused during a play session: the world is the simulation's.
         *
         * @param ctx Frame context supplying the scene and resources to write.
         * @param state Editor state whose dirty flag, recents and toasts are updated.
         * @param path Absolute path of the scene file to write.
         * @return Whether the scene and its terrains reached disk; if not, it stays dirty.
         */
        bool writeScene(FrameContext& ctx, EditorState& state, const std::string& path);

        /**
         * @brief Make @p path the start scene if the project names none, so `vkm run` plays what was saved.
         *
         * @param state Editor state whose project takes it; an edit pending in Project Settings stays.
         * @param path  Absolute path of the scene just saved.
         */
        void startGameIn(EditorState& state, const std::string& path);
        /**
         * @brief Whether a live play session forbids the save asked for, saying so in a toast when it does.
         *
         * @param state Editor state receiving the toast.
         * @return true when a session is live and the caller must not write.
         */
        bool refusedDuringPlay(EditorState& state) const;
        /**
         * @brief Load m_currentScenePath through afterSceneReplace(), reselecting the active entity by name.
         *
         * An import no document names, such as a sound on no source yet, goes with the swap; a toast
         * counts them and the log names them.
         *
         * @param ctx Frame context owning the scene being replaced.
         * @param state Editor state whose scene-scoped parts are reset.
         * @return true when the scene on screen is the one @ref path names; false when the read failed
         *         and the outgoing scene is live, the caller's cue to put the path back too.
         */
        bool load(FrameContext& ctx, EditorState& state);

        /**
         * @brief Replace the world with what @p fill makes, in the one order an open takes.
         *
         * Behaviors end while their world is whole, the imports @p fill dropped are reported, and what
         * was keyed by the old world goes (afterSceneReplace). A failed fill leaves the outgoing world
         * and the editor's state as they were, but for a play session: its behaviors ended, it stops.
         *
         * @param ctx   Frame context owning the scene being replaced.
         * @param state Editor state whose scene-scoped parts are reset.
         * @param fill  Makes the new world; false when it could not.
         * @return What @p fill answered.
         */
        bool openWorld(FrameContext& ctx, EditorState& state, const std::function<bool()>& fill);

        /**
         * @brief Editor housekeeping for a scene swap, once the scene and resources are replaced.
         *
         * Ends the play session and drops the outgoing previews, the selection and a preview through
         * an outgoing camera. The viewpoint stays; restoreView puts back a scene's own.
         *
         * @param ctx Frame context owning the swapped scene and resources.
         * @param state Editor state whose selection and cached panel state is reset.
         * @param history Keep only for a restore that puts the scene it snapshotted back at the same slots.
         * @param eventPath Scene path named in the several-active-cameras warning.
         */
        void afterSceneReplace(
            FrameContext& ctx,
            EditorState& state,
            History history,
            const std::string& eventPath
        );

        /**
         * @brief Put the editor's viewpoint where the scene now open was last seen from.
         *
         * Its saved entry when it has one; framed from its active camera, or a
         * default, when it is new to this editor or has no file.
         *
         * @param scene The scene now open.
         * @param state Editor state holding the saved viewpoints.
         */
        void restoreView(const Scene& scene, const EditorState& state);

        /// Where the open scene's copy of unsaved changes is kept; see SceneRecovery.
        std::filesystem::path recoveryCopy() const { return recoveryCopyOf(m_currentScenePath); }
        std::filesystem::path recoveryCopyOf(const std::string& scenePath) const;

        /// Ask whether to take back a copy left newer than the scene just opened.
        void offerRecovery();

        /**
         * @brief Put the copy's changes in place of the scene just opened, unsaved.
         *
         * @param ctx   Supplies the scene and assets replaced.
         * @param state Takes the dirty flag and a toast.
         */
        void recover(FrameContext& ctx, EditorState& state);

    private:
        CameraControllerSystem& m_cameraController;
        PreviewCache&           m_previews;
        BehaviorSystem&         m_behaviors;  ///< Ends the behaviors' session before a scene is replaced.
        const LoadingSystem&    m_loading;    ///< Its loading screen's assets are no import an open drops.

        std::string m_currentScenePath;  ///< The file Save writes; empty while the scene has none.

        PlaySession& m_play;  ///< Refuses a save while live; ended by an open, put back by Stop.

        float m_recoveryTimer     = 0.0f;   ///< Seconds dirty since the last copy.
        bool  m_openRecoveryPopup = false;
        bool  m_recoveryHeld      = false;  ///< A crash's copy kept: no copy overwrites it until discarded.

        bool        m_openSaveAsPopup   = false;
        char        m_saveAsBuffer[256] = "scene.json";

        FilePicker m_loadPicker;  ///< Load-Scene's file picker.
};

} // namespace Vkm::Engine
