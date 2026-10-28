#pragma once

#include <cstdint>
#include <string>

#include "ui/asset_picker.h"

#include "session/play_snapshot.h"

namespace Vkm::Engine {

class Scene;
struct FrameContext;
struct EditorState;
class BehaviorSystem;
class CameraControllerSystem;
class MaterialPreviewSession;

/**
 * @brief Owns editor scene replacement: file I/O and the play-mode snapshot.
 *
 * captureSnapshot() on Play serializes the authored scene to memory, and
 * restoreSnapshot() on Stop swaps it back through the post-swap housekeeping
 * load() runs (afterSceneReplace), which is why the snapshot lives here.
 */
class SceneIOController {
    public:
        SceneIOController(
            CameraControllerSystem& cameraController,
            MaterialPreviewSession& materialPreviews,
            BehaviorSystem&         behaviors
        );
        ~SceneIOController();

        SceneIOController(const SceneIOController& other) = delete;
        SceneIOController& operator=(const SceneIOController& other) = delete;

        SceneIOController(SceneIOController && other) = delete;
        SceneIOController& operator=(SceneIOController && other) = delete;

        /**
         * @brief Save to the current path, or pop the Save-As prompt if none yet.
         *
         * Clears EditorState::sceneDirty on success. Refused while a play
         * session is live, for the reason writeScene() gives.
         *
         * @param ctx Frame context supplying the scene and resources to write.
         * @param state Editor state whose dirty flag, recents and toasts are updated.
         */
        void save(FrameContext& ctx, EditorState& state);

        /**
         * @brief Replace the scene with a fresh default one (buildDefaultScene).
         *
         * Callers guard unsaved changes first.
         *
         * @param ctx Frame context owning the scene being replaced.
         * @param state Editor state whose scene-scoped parts are reset.
         */
        void newScene(FrameContext& ctx, EditorState& state);

        /**
         * @brief Empty the scene so something else can take its place.
         *
         * The housekeeping a swap needs that Scene::clear() does not do: live
         * behaviors get onDestroy while their code is still loaded, the undo
         * stack is dropped because entity ids do not carry across scenes, the
         * material previews keyed by the outgoing assets are released, and the
         * saved-scene path is forgotten so a later Save cannot write into
         * whatever was open before.
         *
         * Leaves an empty scene; the caller decides what fills it. Exposed
         * because opening a project is also a scene swap.
         *
         * @param ctx Frame context owning the scene being replaced.
         * @param state Editor state whose scene-scoped parts are reset.
         */
        void beginSceneReplace(FrameContext& ctx, EditorState& state);
        /**
         * @brief Queue the Save-As prompt to open on the next drawDialogs().
         *
         * Deferred so the modal is opened from the menu-bar scope, which is what
         * keeps it alive past the menu closing.
         */
        void requestSaveAs();
        /**
         * @brief Queue the Load-Scene picker to open on the next drawDialogs().
         */
        void requestLoad();
        /**
         * @brief Load a scene path directly.
         *
         * The current path moves only if the scene does: a read that fails
         * leaves the outgoing scene live, and the name has to stay with it.
         *
         * @param ctx Frame context owning the scene being replaced.
         * @param state Editor state whose scene-scoped parts are reset.
         * @param path Absolute path of the scene file to open.
         */
        void loadPath(FrameContext& ctx, EditorState& state, const std::string& path);

        /**
         * @brief Render any pending Save-As / Load modals.
         *
         * Must be called once per frame from the menu-bar scope so the modals
         * survive the menu closing.
         *
         * @param ctx Frame context supplying the scene and resources to save/load.
         * @param state Editor state read for paths/flags and updated on a completed pick.
         */
        void drawDialogs(FrameContext& ctx, EditorState& state);

        /**
         * @brief Snapshot the live scene and assets in memory, for Stop to restore.
         *
         * Call when entering play. Bakes every loaded asset into the cooked
         * library first: the snapshot is the scene file format, which names its
         * assets, and a name is only restorable when the library holds a record
         * for it.
         *
         * May block while an import that has not landed finishes, since the cook
         * waits on outstanding async loads.
         *
         * @param ctx Frame context supplying the scene and resources to snapshot.
         * @param state Editor state whose dirty flag is remembered for Stop,
         *              whose undo history is parked until it, and which
         *              receives a toast if the cook or the serialization failed.
         * @return Whether a play session may begin. False only when the scene
         *         could not be serialized at all. A partial cook still answers
         *         true, and the toast names what Stop may lose.
         */
        bool captureSnapshot(FrameContext& ctx, EditorState& state);

        /**
         * @brief Swap the captured snapshot back in, through load()'s housekeeping, and release it.
         *
         * No-op if no snapshot was captured.
         *
         * A failed restore keeps the snapshot rather than dropping it, so the
         * played scene is left alone instead of being half-replaced.
         *
         * Every panel stays live during a session, so the world it throws away
         * may hold entities and edits the author made inside one. Those are
         * said out loud, because the editor called them authored work while
         * they were being made: it pushed an undo step and raised the dirty
         * flag for each.
         *
         * @param ctx Frame context supplying the scene and resources to restore into.
         * @param state Editor state whose selection, dirty flag and toasts are
         *              updated to match the restored scene.
         */
        void restoreSnapshot(FrameContext& ctx, EditorState& state);

        /**
         * @brief End the play session: stop the clock and restore the authored scene.
         *
         * No-op outside a session.
         *
         * @param ctx Frame context owning the clock and the scene.
         * @param state Editor state the restore updates.
         */
        void stopPlaySession(FrameContext& ctx, EditorState& state);

        /**
         * @brief Whether a play session is live.
         *
         * True from Play until Stop. Ask this, not the clock, before anything
         * that must not run against the simulation's copy of the scene: the
         * clock is paused in Edit mode as well.
         *
         * @return true while the authored scene is held aside and the world on
         *         screen belongs to the simulation.
         */
        bool isPlaying() const { return m_play.held(); }

        /**
         * @brief Show the editor's view in the running session, or the game's again.
         *
         * Ejected, the viewport renders through the editor's viewpoint, flies,
         * picks and edits as in Edit mode, and the game runs on hearing none of
         * the input - it is told the host holds both devices. A cursor the game
         * had grabbed is freed, and given back as it was on the way back in.
         * No-op outside a session; a session's end ends the ejection with it.
         *
         * @param ctx     Frame context owning the window whose cursor moves.
         * @param ejected true to show the editor's view.
         */
        void setEjected(FrameContext& ctx, bool ejected);

        /**
         * @brief Whether the running session's viewport shows the editor's view.
         *
         * @return false outside a session.
         */
        bool isEjected() const { return m_play.ejected(); }

        /**
         * @brief Free the cursor again if the game grabbed it while ejected; see
         *        PlaySnapshot::holdCursorFree.
         *
         * @param ctx Frame context owning the window.
         */
        void holdEjectedCursorFree(FrameContext& ctx);

        /**
         * @brief Take @p path as the file the scene already in the world came from.
         *
         * For a scene that arrives without passing through this controller: a
         * project opens by asking bootProjectWorld for its world, and the editor
         * then has a scene on screen that it did not read.
         *
         * Adopting is not loading: the world is already there and nothing is
         * read, replaced or reselected here. An empty @p path is the honest
         * answer for a world with no file - one a module built, or the default
         * scene standing in for a load that failed - and leaves the
         * controller with no save path, which is what keeps a stand-in from
         * overwriting the file it stood in for.
         *
         * The editor's viewpoint is restored for it, as for a scene it opened.
         *
         * @param scene The scene already in the world, framed when it has no
         *              saved viewpoint.
         * @param state Editor state whose recent-scenes list gains @p path.
         * @param path Absolute path of the scene file, or empty for none.
         */
        void adoptPath(const Scene& scene, EditorState& state, const std::string& path);

        /**
         * @brief Keep the viewpoint the open scene is seen from, under its path.
         *
         * Into EditorState::sceneViews, which editor_settings.json persists, so
         * reopening the scene - this session or a later one - looks from where
         * it was left. Call it as a scene is left and before the settings are
         * written; a scene with no file has nothing to be kept under.
         *
         * @param state Editor state whose sceneViews takes the entry.
         */
        void rememberView(EditorState& state) const;

        bool hasPath() const { return !m_currentScenePath.empty(); }
        const std::string& path() const { return m_currentScenePath; }

        /**
         * @brief True while a Save-As prompt is either queued for opening or currently visible.
         *
         * Tells a save still being waited on from one the author backed out of:
         * a scene still dirty with no prompt up means the Save-As was cancelled,
         * and the action waiting on that save is dropped with it.
         *
         * @return true while the prompt is queued or up.
         */
        bool isSaveDialogActive() const;

    private:
        /// Whether a swap takes the undo history with it; see afterSceneReplace.
        enum class History : uint8_t { Drop, Keep };

    private:
        /**
         * @brief Write the live scene to @p path and record that it was written.
         *
         * The whole write rule in one place: cook every loaded asset first
         * (the scene stores name-only references to cooked ones, so a write
         * that skips this names assets the library does not have), then
         * serialize, and only on success clear the dirty flag and promote the
         * path in the recents list.
         *
         * On failure sceneDirty is left set - the unsaved-changes guard waits on
         * it dropping, so a failed save must not look like a successful one.
         *
         * Refused outright while a play session is live: the scene in the world
         * is then the simulation's, Stop is about to replace it with the
         * snapshot, and writing it over the file would store a scene nobody
         * authored.
         *
         * @param ctx Frame context supplying the scene and resources to write.
         * @param state Editor state whose dirty flag, recents and toasts are updated.
         * @param path Absolute path of the scene file to write.
         * @return Whether the scene reached disk.
         */
        bool writeScene(FrameContext& ctx, EditorState& state, const std::string& path);
        /**
         * @brief Whether a live play session forbids the save being asked for,
         * saying so through a toast when it does.
         *
         * @param state Editor state receiving the toast.
         * @return true when a session is live and the caller must not write.
         */
        bool refusedDuringPlay(EditorState& state) const;
        /**
         * @brief Load m_currentScenePath through afterSceneReplace(), keeping the active entity
         * selected by name.
         *
         * The swap replaces the asset graph, so an asset nothing in the
         * outgoing scene named - a sound imported and not yet assigned to a
         * source - has no name in the new document to be recreated from and
         * goes. The ones that went are counted into a toast and named in the
         * log.
         *
         * @param ctx Frame context owning the scene being replaced.
         * @param state Editor state whose scene-scoped parts are reset.
         * @return true when the scene on screen is the one @ref path names;
         *         false when the read failed and the outgoing scene is still
         *         live, which is the caller's cue to put the path back too.
         */
        bool load(FrameContext& ctx, EditorState& state);
        /**
         * @brief Drop the play snapshot and put the clock back in Edit mode.
         *
         * What a path that replaces the world has to do: a snapshot
         * outliving the scene it was taken from leaves the transport reading as
         * playing, and Stop then restores that dead world over whatever
         * replaced it - under the new scene's name, which is the file the next
         * save writes.
         *
         * @param ctx Frame context supplying the clock the session ran on.
         */
        void endPlaySession(FrameContext& ctx);

        /**
         * @brief Editor housekeeping for a scene swap, once the scene and
         * resources have been replaced.
         *
         * Ends the play session, drops the history when told to, drops the GPU
         * previews and the pinned material keyed by the outgoing graph, clears
         * the selection, and ends a preview through a camera of the outgoing
         * world. What the selection becomes is the caller's. The editor's
         * viewpoint is left where it is; restoreView puts back a scene's own.
         *
         * Keep is for a restore that puts the very scene it snapshotted back at
         * the same slots; a swap that replaces every entity drops the history.
         *
         * @param ctx Frame context owning the swapped scene and resources.
         * @param state Editor state whose selection and cached panel state is reset.
         * @param history Whether the undo history goes with the outgoing world.
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

    private:
        CameraControllerSystem& m_cameraController;
        MaterialPreviewSession& m_materialPreviews;
        /// Ends the running behaviors' session before a scene is replaced
        BehaviorSystem&         m_behaviors;

        std::string m_currentScenePath;  ///< The file Save writes; empty while the scene has none.

        /// The world as Play found it, and whether it is ejected, held until Stop. See PlaySnapshot.
        PlaySnapshot m_play;

        bool        m_openSaveAsPopup = false;
        char        m_saveAsBuffer[256] = "scene.json";

        /**
         * @brief Shared cached file picker for the Load-Scene flow.
         */
        AssetPicker m_loadPicker;
};

} // namespace Vkm::Engine
