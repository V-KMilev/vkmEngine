#pragma once

#include <cstdint>
#include <string>

#include "framework/asset_picker.h"

namespace Vkm::Engine {

struct FrameContext;
struct EditorState;
class CameraControllerSystem;
class MaterialPreviewSession;

/**
 * @brief Owns editor scene replacement: file I/O and the play-mode snapshot.
 *
 * Holds the current scene path, performs Save / Save-As / Load, renders the
 * Save-As and Load-picker modals, and runs post-load editor housekeeping
 * (camera rebind, temporal-history invalidate) directly inside load().
 *
 * It also owns the in-memory play-mode snapshot: captureSnapshot() on Play
 * serializes the authored scene to memory, and restoreSnapshot() on Stop
 * swaps it back. Both go through the same post-swap housekeeping as load(),
 * since a restore is just an in-memory reload - that shared path is why the
 * snapshot lives here rather than in the playbar.
 *
 * The snapshot is two documents, not one, because a session holds more than a
 * scene file describes: the scene, and the list of every asset loaded at the
 * time. Restoring only the first would put the world back and leave the Asset
 * Browser emptied of whatever nothing in the world pointed at.
 */
class SceneIOController {
    public:
        SceneIOController(
            CameraControllerSystem& cameraController,
            MaterialPreviewSession& materialPreviews
        );
        ~SceneIOController();

        SceneIOController(const SceneIOController& other) = delete;
        SceneIOController& operator=(const SceneIOController& other) = delete;

        SceneIOController(SceneIOController && other) = delete;
        SceneIOController& operator=(SceneIOController && other) = delete;

        /**
         * @brief Save to the current path, or pop the Save-As prompt if none yet.
         * Clears EditorState::sceneDirty on success.
         *
         * Refused while a play session is live, for the reason writeScene()
         * gives - the scene in the world during a session is not the one the
         * author wrote.
         */
        void save(FrameContext& ctx, EditorState& state);

        /**
         * @brief Replace the scene with a fresh minimal one (a camera + a sun).
         *
         * Clears entities, resets the Environment and the current path, and
         * runs the same post-swap housekeeping as a load (undo stack, preview
         * cache, selection). Callers guard unsaved changes first.
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
         * because opening a project is also a scene swap, and doing this by
         * hand there is how the two drift apart.
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
         * @brief Load a scene path directly (used by the recent-scenes menu). Goes
         * through the same housekeeping as a Load-modal pick.
         */
        void loadPath(FrameContext& ctx, EditorState& state, const std::string& path);

        /**
         * @brief Open @p path through the unsaved-changes guard.
         *
         * Prompts (Save / Don't Save / Cancel) when the current scene is
         * dirty, otherwise loads immediately. Every open flow - the picker
         * and Open Recent - routes through this; loading a scene used to
         * silently discard unsaved work.
         */
        void requestOpenPath(FrameContext& ctx, EditorState& state, const std::string& path);

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
         * @brief Snapshot the live scene + assets in memory so Stop can restore
         * the authored state. Call when entering play.
         *
         * Bakes every loaded asset into the cooked library first, for the reason
         * writeScene() does: the snapshot is the scene file format, which names
         * its assets and nothing more, and a name is only restorable when the
         * library holds a record for it. An asset imported during this session
         * and never baked would come back as an empty slot.
         *
         * The session's whole asset list is recorded beside the scene, because
         * the scene names only what it uses: an import nobody has assigned yet
         * is in the Asset Browser and in no component, and restoring the scene
         * alone would drop it. The bake above is what makes that list
         * restorable too.
         *
         * May block while an import that has not landed yet finishes, since the
         * cook waits on outstanding async loads - pressing Play seconds after
         * Import Model waits for that model. A half-loaded scene is not one worth
         * playing, but the pause is visible and is worth expecting.
         *
         * @param ctx Frame context supplying the scene and resources to snapshot.
         * @param state Editor state whose dirty flag and history revision are
         *              remembered for Stop - the first to put back, the second
         *              to tell a session that edited from one that only ran -
         *              and which receives a toast if the cook or the
         *              serialization failed.
         */
        void captureSnapshot(FrameContext& ctx, EditorState& state);

        /**
         * @brief Swap the captured snapshot back in (same housekeeping as load())
         * and clear it. No-op if no snapshot was captured.
         *
         * A failed restore keeps the snapshot rather than dropping it, so the
         * played scene is left alone instead of being half-replaced.
         *
         * Every panel stays live during a session, so the world it throws away
         * may hold entities and edits the author made inside one. Those are
         * said out loud, because the editor called them authored work while
         * they were being made: it pushed an undo step and raised the dirty
         * flag for each, and withdrawing both without a word is what made the
         * loss invisible.
         *
         * @param ctx Frame context supplying the scene and resources to restore into.
         * @param state Editor state whose selection, dirty flag and toasts are
         *              updated to match the restored scene.
         */
        void restoreSnapshot(FrameContext& ctx, EditorState& state);

        /**
         * @brief End the play session: stop the clock and restore the authored
         * scene. No-op outside a session.
         *
         * What Stop is, in one place. The transport's button is one caller and
         * the unsaved-changes guard is the other - a save cannot run inside a
         * session, so answering Save on quit has to end one first - and two
         * spellings of Stop would drift the moment either grew a step.
         *
         * @param ctx Frame context owning the clock and the scene.
         * @param state Editor state the restore updates.
         */
        void stopPlaySession(FrameContext& ctx, EditorState& state);

        /**
         * @brief Whether a play-mode snapshot is currently held.
         *
         * @return true once captureSnapshot() has stored a snapshot (i.e. while
         *         in play mode), false after restoreSnapshot() clears it.
         */
        bool hasSnapshot() const { return !m_playSnapshot.empty(); }

        /**
         * @brief Take @p path as the file the scene already in the world came from.
         *
         * The one case a scene arrives without passing through this controller:
         * both hosts open a project by asking bootProjectScene for its world,
         * and the editor then has a scene on screen that it did not read. Left
         * unadopted it is a scene with no file - Save asks for a name, the
         * default one offered is not the name it has, and accepting it writes
         * the session somewhere the project's entryScene never points, leaving
         * the file the author was editing exactly as it was with nothing said.
         * The title bar calling it untitled is the same gap, said out loud.
         *
         * Adopting is not loading: the world is already there and nothing is
         * read, replaced or reselected here. An empty @p path is the honest
         * answer for the two worlds with no file - a module-built one and the
         * default scene standing in for a load that failed - and leaves the
         * controller with no save path, which is what keeps a stand-in from
         * overwriting the file it stood in for.
         *
         * @param state Editor state whose recent-scenes list gains @p path.
         * @param path Absolute path of the scene file, or empty for none.
         */
        void adoptPath(EditorState& state, const std::string& path);

        bool hasPath() const { return !m_currentScenePath.empty(); }
        const std::string& path() const { return m_currentScenePath; }

        /**
         * @brief True while a Save-As prompt is either queued for opening or currently visible.
         *
         * Used by the save-on-quit flow to detect whether the user cancelled
         * mid-Save (EditorState::afterSaveAction is cleared on cancel; left
         * set on success).
         */
        bool isSaveDialogActive() const;

    private:
        /**
         * @brief Write the live scene to @p path and record that it was written.
         *
         * The whole write rule in one place: cook the referenced assets first
         * (the scene stores name-only references to cooked ones, so a write
         * that skips this names assets the library does not have), then
         * serialize, and only on success clear the dirty flag and promote the
         * path in the recents list. Save and Save-As differ in how they choose
         * the path, not in how a scene is written.
         *
         * On failure sceneDirty is deliberately left set - the deferred-quit
         * flow waits on it dropping, so a failed save must not look like a
         * successful one.
         *
         * Refused outright while a play session is live: the scene in the world
         * is then the simulation's, Stop is about to replace it with the
         * snapshot, and writing it over the file would store a scene nobody
         * authored - under a cleared dirty flag Stop then restores, so the
         * editor would go on reporting the file as current.
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
         * @brief Load m_currentScenePath: stashes/restores selection, then runs
         * afterSceneReplace() housekeeping (camera rebind done inline).
         *
         * An open is the editor's clean break, and the session's imports break
         * with it. The swap replaces the asset graph, so an asset nothing in
         * the outgoing scene named - a sound imported and not yet assigned to a
         * source - has no name in the new document to be recreated from and
         * goes. That is the opposite of what Stop does, deliberately: Stop
         * promises to put one session back, while this leaves a world for
         * another one. What it does owe the author is the fact, so the ones
         * that went are counted into a toast and named in the log.
         */
        void load(FrameContext& ctx, EditorState& state);
        /**
         * @brief Drop the play snapshot and put the clock back in Edit mode.
         *
         * What every path that replaces the world has to do, in one place
         * because getting it wrong is invisible: a snapshot outliving the scene
         * it was taken from leaves the transport reading as playing, and Stop
         * then restores that dead world over whatever replaced it - under the
         * new scene's name, which is the file the next save writes.
         *
         * @param ctx Frame context supplying the clock the session ran on.
         */
        void endPlaySession(FrameContext& ctx);
        /**
         * @brief Name of the current selection (empty if none), captured BEFORE a
         * scene swap so afterSceneReplace can re-select it by name afterwards.
         */
        static std::string cacheSelectionName(FrameContext& ctx, EditorState& state);
        /**
         * @brief Editor housekeeping shared by load() and restoreSnapshot() after the
         * scene + resources have been swapped: clear undo, drop stale GPU
         * previews, re-select @p priorSelectionName, and rebind the active camera.
         */
        void afterSceneReplace(
            FrameContext& ctx,
            EditorState& state,
            const std::string& priorSelectionName,
            const std::string& eventPath
        );

        CameraControllerSystem& m_cameraController;
        MaterialPreviewSession& m_materialPreviews;

        std::string m_currentScenePath;  ///< Empty until the user saves/loads once.

        /**
         * @brief In-memory play-mode snapshot of the scene. Non-empty only between
         * captureSnapshot() (Play) and restoreSnapshot() (Stop).
         */
        std::string m_playSnapshot;

        /**
         * @brief The session's whole asset list at capture, as a serialized block.
         *
         * The scene above names only the assets the scene uses, which is what a
         * scene file is; this is the rest. Held as text beside it so this header
         * stays free of the JSON type, and because the two are one snapshot in
         * two documents rather than a document and a cache.
         */
        std::string m_playAssets;
        /**
         * @brief EditorState::sceneDirty at capture time, restored on Stop so a play
         * session leaves the dirty flag exactly as the user left it.
         */
        bool        m_playSnapshotDirty = false;
        /**
         * @brief CommandStack::revision() at capture time.
         *
         * The same number on Stop means the session left the history exactly as
         * Play found it, which is what lets the restore keep it: the scene it
         * puts back is the one those steps were made against, at the same ids.
         */
        unsigned long long m_playSnapshotHistory = 0;
        bool        m_openSaveAsPopup = false;
        char        m_saveAsBuffer[256] = "scene.json";

        /**
         * @brief Shared cached file picker for the Load-Scene flow: rooted at the
         * scenes dir, filtered to .json. requestLoad() configures + opens it;
         * drawDialogs() drives it and feeds a pick into loadPath().
         */
        AssetPicker m_loadPicker;
};

} // namespace Vkm::Engine
