#define VKM_LOG_CATEGORY "EDITOR"

#include "session/scene_session.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include "vkm_log/logger.h"

#include "core/clock.h"
#include "core/frame_context.h"
#include "ecs/component/scene/name.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/scene.h"
#include "session/play_session.h"
#include "session/scene_recovery.h"
#include "session/asset/asset_writes.h"
#include "session/asset/terrain_files.h"
#include "editor_state.h"
#include "preview/preview_cache.h"
#include "io/scene/scene_assets.h"
#include "io/scene/scene_serializer.h"
#include "io/project/project.h"
#include "io/project/roots.h"
#include "resource/resource_manager.h"
#include "io/scene/default_scene.h"
#include "view/camera_controller_system.h"
#include "system/loading/loading_system.h"
#include "io/project/render_settings.h"
#include "system/script/behavior_system.h"
#include "ui/style/editor_style.h"
#include "ui/editor_dialogs.h"

namespace Vkm::Engine {

namespace {

// An asset's identity in a saved document: its section and name (SceneAssets::namesIn).
using AssetKey = std::pair<std::string, std::string>;

std::set<AssetKey> assetKeysOf(const nlohmann::json& block) {
    const std::vector<AssetKey> names = SceneAssets::namesIn(block);
    return std::set<AssetKey>(names.begin(), names.end());
}

// The graph's assets before a swap, and those the outgoing scene or the loading screen names; see
// docs/reference/editor.md, "What an open does to the session's imports".
struct HeldImports {
    std::set<AssetKey> all;
    std::set<AssetKey> named;
};

HeldImports heldImports(FrameContext& ctx, const std::vector<AssetKey>& loadingScreen) {
    HeldImports held{
        assetKeysOf(SceneAssets::saveAllAssets(ctx.resources)),
        assetKeysOf(SceneAssets::saveAssetsForScene(ctx.scene, ctx.resources))
    };
    held.named.insert(loadingScreen.begin(), loadingScreen.end());
    return held;
}

void reportDroppedImports(FrameContext& ctx, EditorState& state, const HeldImports& before) {
    // A stray is what neither the old scene nor the loading screen named and the new graph lacks:
    // two scenes sharing a sound lose nothing.
    const std::set<AssetKey> nowHeld =
        assetKeysOf(SceneAssets::saveAllAssets(ctx.resources));

    std::vector<std::string> dropped;
    for (const AssetKey& key : before.all) {
        if (before.named.count(key) || nowHeld.count(key)) continue;
        dropped.push_back(key.first + " '" + key.second + "'");
    }
    if (dropped.empty()) return;

    for (const std::string& what : dropped) {
        LOG_INFO("Left an unused import behind: %s", what.c_str());
    }
    state.toasts.push(
        ToastKind::Info,
        std::to_string(dropped.size()) + " unused import(s) stayed with the previous scene"
    );
}

// The active entity's name: an open reselects by name, as its slots hold other entities.
std::string selectedName(const Scene& scene, const EditorState& state) {
    // tryGet is total, so it also answers for a selection that has gone away.
    const Name* name = scene.tryGet<Name>(state.selection.active());
    return name ? name->value : std::string{};
}

void selectByName(const Scene& scene, EditorState& state, const std::string& name) {
    if (name.empty()) return;
    scene.forEach<Name>([&](EntityId id, const Name& n) {
        if (!state.selection.active() && name == n.value) state.selectEntity(id);
    });
}

// Drop from @p entities what the world Stop put back does not hold: a slot free at Play hands out
// again the ids play took in it.
void keepHeld(const Scene& scene, std::vector<EntityId>& entities) {
    const auto gone = [&](EntityId id) { return !scene.isAlive(id); };
    entities.erase(std::remove_if(entities.begin(), entities.end(), gone), entities.end());
}

} // namespace

SceneSession::SceneSession(
    CameraControllerSystem& cameraController,
    PreviewCache& previews,
    BehaviorSystem&         behaviors,
    const LoadingSystem&    loading,
    PlaySession&            play
)
    : m_cameraController(cameraController)
    , m_previews(previews)
    , m_behaviors(behaviors)
    , m_loading(loading)
    , m_play(play)
{}

SceneSession::~SceneSession() = default;

bool SceneSession::writeScene(FrameContext& ctx, EditorState& state, const std::string& path) {
    if (refusedDuringPlay(state)) return false;

    // A partial cook does not stop the save; the toast below says so.
    const AssetWrites writes = writeAssets(ctx.resources);

    const std::string shown = std::filesystem::path(path).filename().string();
    if (!SceneSerializer::save(ctx.scene, ctx.resources, path)) {
        LOG_ERROR("SceneSession: failed to write %s - scene remains dirty", path.c_str());
        state.toasts.push(ToastKind::Error, "Save failed: " + shown);
        return false;
    }
    // A terrain's sculpting lives in its file, not the scene's: one not written is still unsaved.
    if (!writes.terrains) {
        state.toasts.push(ToastKind::Error, "Saved " + shown + ", but a terrain could not be written");
        return false;
    }
    pushRecentPath(state.recentScenes, path);
    state.sceneDirty = false;
    // Saved, so a copy kept for a crash is behind the file, as is one of a scene never saved.
    SceneRecovery::discard(recoveryCopyOf(path));
    SceneRecovery::discard(recoveryCopyOf({}));
    m_recoveryTimer = 0.0f;
    m_recoveryHeld  = false;
    if (!writes.cooked) {
        state.toasts.push(ToastKind::Error, "Saved " + shown + ", but some assets did not cook");
        return true;
    }
    state.toasts.push(ToastKind::Info, "Saved " + shown);
    return true;
}

void SceneSession::startGameIn(EditorState& state, const std::string& path) {
    const std::string scene = Roots::toProjectRelative(path);
    if (!setEntrySceneIfNone(Roots::projectRoot(), scene)) return;
    if (state.project.entryScene == state.projectSaved.entryScene) state.project.entryScene = scene;
    state.projectSaved.entryScene = scene;
    state.toasts.push(ToastKind::Info, "The game now starts in " + scene);
}

std::filesystem::path SceneSession::recoveryCopyOf(const std::string& scenePath) const {
    const std::filesystem::path dir = Roots::userRoot() / "recovery";
    return SceneRecovery::pathFor(dir, Roots::projectRoot(), scenePath);
}

void SceneSession::discardRecovery() {
    SceneRecovery::discard(recoveryCopy());
    m_recoveryHeld = false;
}

void SceneSession::offerRecovery() {
    m_recoveryTimer     = 0.0f;
    m_openRecoveryPopup = SceneRecovery::hasNewer(recoveryCopy(), m_currentScenePath);
    m_recoveryHeld      = m_openRecoveryPopup;
}

void SceneSession::serviceRecovery(FrameContext& ctx, EditorState& state) {
    if (state.requests.take(Ask::RecoverScene)) recover(ctx, state);
    if (m_play.isPlaying() || !state.sceneDirty || m_recoveryHeld) {
        m_recoveryTimer = 0.0f;
        return;
    }
    m_recoveryTimer += ctx.clock.getDeltaTime();
    if (m_recoveryTimer < SceneRecovery::INTERVAL) return;
    m_recoveryTimer = 0.0f;

    // Written as a save writes it, so a recovery is a load, but nothing the project holds is
    // rewritten: the author has not saved.
    const std::filesystem::path copy = recoveryCopy();
    std::error_code ec;
    std::filesystem::create_directories(copy.parent_path(), ec);
    const AssetWrites kept = keepAssets(ctx.resources, SceneRecovery::terrainsOf(copy));
    if (!kept.terrains) LOG_WARNING("Could not keep the unsaved terrains beside %s", copy.string().c_str());
    if (!kept.cooked) LOG_WARNING("Could not record the new imports %s names", copy.string().c_str());
    if (!SceneSerializer::save(ctx.scene, ctx.resources, copy.string())) {
        LOG_WARNING("Could not keep a copy of the unsaved changes at %s", copy.string().c_str());
    }
}

void SceneSession::recover(FrameContext& ctx, EditorState& state) {
    const std::filesystem::path copy = recoveryCopy();
    const bool read = openWorld(ctx, state, [&] {
        if (!SceneSerializer::load(ctx.scene, ctx.resources, copy.string())) return false;
        readTerrainCopies(ctx.resources, SceneRecovery::terrainsOf(copy));
        return true;
    });
    if (!read) {
        state.toasts.push(ToastKind::Error, "The kept copy could not be read; it is left where it is");
        return;
    }
    restoreView(ctx.scene, state);
    // Saving is still the author's to do; the next copy may replace this one.
    state.sceneDirty = true;
    m_recoveryHeld   = false;
    state.toasts.push(ToastKind::Info, "Recovered the unsaved changes - save to keep them");
}

bool SceneSession::refusedDuringPlay(EditorState& state) const {
    if (!m_play.isPlaying()) return false;
    state.toasts.push(
        ToastKind::Warning,
        "Stop the play session before saving - the running scene is not the authored one"
    );
    return true;
}

void SceneSession::save(FrameContext& ctx, EditorState& state) {
    if (m_currentScenePath.empty()) {
        requestSaveAs(state);
        return;
    }
    writeScene(ctx, state, m_currentScenePath);
}

void SceneSession::loadPath(FrameContext& ctx, EditorState& state, const std::string& path) {
    if (path.empty()) return;
    rememberView(state);
    std::string previous = m_currentScenePath;
    m_currentScenePath = path;
    if (!load(ctx, state)) {
        m_currentScenePath = std::move(previous);
        return;
    }
    // The scene left was saved or given up at the unsaved-changes prompt.
    if (previous != path) SceneRecovery::discard(recoveryCopyOf(previous));
    offerRecovery();
}

void SceneSession::adoptPath(const Scene& scene, EditorState& state, const std::string& path) {
    m_currentScenePath = path;
    restoreView(scene, state);
    offerRecovery();
    // Guarded because a world with no file must not put an empty name there.
    if (!path.empty()) pushRecentPath(state.recentScenes, path);
}

void SceneSession::rememberView(EditorState& state) const {
    if (m_currentScenePath.empty()) return;
    state.sceneViews[Roots::toProjectRelative(m_currentScenePath)] = m_cameraController.viewpoint();
}

void SceneSession::restoreView(const Scene& scene, const EditorState& state) {
    const auto saved = state.sceneViews.find(Roots::toProjectRelative(m_currentScenePath));
    if (!m_currentScenePath.empty() && saved != state.sceneViews.end()) {
        m_cameraController.setViewpoint(saved->second);
    } else {
        m_cameraController.startFrom(scene);
    }
}

void SceneSession::requestSaveAs(EditorState& state) {
    // Here, so a session gets no prompt for a name writeScene would refuse.
    if (refusedDuringPlay(state)) return;
    m_openSaveAsPopup = true;
}

void SceneSession::requestLoad() {
    FilePicker::Options options;
    options.title      = "Load Scene";
    options.root       = Roots::scenes();
    options.extensions = {".json"};
    options.relativeTo.clear();  // loadPath() wants an absolute path
    m_loadPicker.open(options);
}

bool SceneSession::isSaveDialogActive() const {
    // Set for the dialog's whole life, cleared on any dismissal. No popup query: this is asked between
    // frames, where ImGui has no window to name a popup by.
    return m_openSaveAsPopup;
}

bool SceneSession::load(FrameContext& ctx, EditorState& state) {
    const std::string priorSelection = selectedName(ctx.scene, state);

    const auto read = [&] { return SceneSerializer::load(ctx.scene, ctx.resources, m_currentScenePath); };
    if (!openWorld(ctx, state, read)) {
        LOG_ERROR(
            "SceneSession::load: failed to load %s - editor state preserved",
            m_currentScenePath.c_str()
        );
        state.toasts.push(ToastKind::Error, "Load failed: " + m_currentScenePath);
        return false;
    }
    selectByName(ctx.scene, state, priorSelection);
    restoreView(ctx.scene, state);

    state.sceneDirty = false;
    pushRecentPath(state.recentScenes, m_currentScenePath);
    return true;
}

void SceneSession::beginSceneReplace(FrameContext& ctx, EditorState& state) {
    // Leaving it was saved or given up at the unsaved-changes prompt.
    discardRecovery();

    openWorld(ctx, state, [&] {
        ctx.scene.clear();
        // The asset graph goes with the scene it belonged to; see
        // docs/reference/editor.md, "What an open does to the session's imports".
        ResourceManager replaced;
        ctx.resources.swap(replaced);
        m_currentScenePath.clear();
        return true;
    });
}

bool SceneSession::openWorld(FrameContext& ctx, EditorState& state, const std::function<bool()>& fill) {
    // A session's behaviors get onDestroy while their world and code are whole.
    m_behaviors.endSession(ctx.scene);
    const HeldImports before = heldImports(ctx, m_loading.screenNames());
    // An opened world comes in behind the loading screen; one that fails to fill takes that back.
    ctx.chrome.coverNextWorld = true;
    if (!fill()) {
        ctx.chrome.takeCoverNextWorld();
        // A play session cannot go on with its behaviors ended: Stop puts the authored world back.
        if (m_play.isPlaying()) stopPlaySession(ctx, state);
        return false;
    }
    reportDroppedImports(ctx, state, before);
    // Entity ids do not carry across worlds, so the history goes with it.
    afterSceneReplace(ctx, state, History::Drop, m_currentScenePath);
    return true;
}

void SceneSession::newScene(FrameContext& ctx, EditorState& state) {
    rememberView(state);
    beginSceneReplace(ctx, state);

    // Not through EditorActions: a new scene carries no undo entries and no selection.
    buildDefaultScene(ctx.scene, ctx.resources);
    restoreView(ctx.scene, state);

    state.sceneDirty = false;
    state.toasts.push(ToastKind::Info, "New scene");
}

void SceneSession::afterSceneReplace(
    FrameContext& ctx,
    EditorState& state,
    History history,
    const std::string& eventPath
) {
    m_play.end(ctx);
    // A copied component kept over a Stop carries what play tuned back to the authored
    // scene; another scene's entities and assets are not its own.
    if (history == History::Drop) {
        state.forgetWorldState();
        state.hiddenInView.clear();
        state.lockedInView.clear();
    }

    // The previews are keyed by handles into the outgoing graph.
    m_previews.clear();

    state.selection.clear();

    // A preview names an entity of the outgoing world.
    m_cameraController.lookThrough({});

    // Which camera a session renders through is findActiveCamera's choice.
    int activeCount = 0;
    ctx.scene.forEach<Camera, Transform>([&](EntityId, const Camera& c, const Transform&) {
        if (c.active) ++activeCount;
    });
    if (activeCount > 1) {
        LOG_WARNING(
            "SceneSession: %d cameras marked active in %s - a session renders "
            "through the one in the lowest slot",
            activeCount,
            eventPath.c_str()
        );
    }
}

void SceneSession::restoreSnapshot(FrameContext& ctx, EditorState& state) {
    const PlaySnapshot& snapshot = m_play.snapshot();
    if (!snapshot.held()) return;

    // Read before the swap moves either: the session's own history and the
    // dirty flag are how its authored work is told from what it merely simulated.
    const bool discardingEdits = state.commands.canUndo() || state.commands.canRedo()
        || (state.sceneDirty && !snapshot.dirtyAtCapture());

    const uint64_t played = snapshot.epochAtCapture();
    EditorMarks marks{state.selection, state.hiddenInView, state.lockedInView};
    // A world the game read in play numbered its ids afresh, so what was marked in it would name
    // others here: the marks come back as Play found them.
    if (ctx.scene.epoch() != played) marks = m_play.marksAtPlay();

    // onDestroy runs while the played scene still gives its behaviors their context.
    m_behaviors.endSession(ctx.scene);

    // Assets then scene, both from the snapshot; see docs/reference/editor.md,
    // "What an open does to the session's imports".
    if (!snapshot.restoreInto(ctx.scene, ctx.resources)) {
        LOG_ERROR("SceneSession::restoreSnapshot: failed to restore play snapshot");
        state.toasts.push(ToastKind::Error, "The scene could not be put back from its snapshot");
        // The snapshot is kept, so Stop can be pressed again; the asset graph, restored first, stays.
        return;
    }

    // The terrains as Play wrote them: what the session dug or brushed goes too.
    readTerrainFiles(ctx.resources);

    // Both are read out first because afterSceneReplace releases the snapshot.
    const bool dirtyAtCapture = snapshot.dirtyAtCapture();
    const bool inPlace        = snapshot.restoredInPlace(ctx.scene);

    // The history, the selection, the eye and the lock come back when every entity is back under its
    // id; see docs/reference/editor.md, "What an open does to the session's imports".
    if (inPlace) state.commands.unpark(ctx.scene);
    afterSceneReplace(ctx, state, inPlace ? History::Keep : History::Drop, m_currentScenePath);
    if (inPlace) {
        state.selection    = marks.selection;
        state.hiddenInView = marks.hidden;
        state.lockedInView = marks.locked;
        state.selection.prune(ctx.scene);
        keepHeld(ctx.scene, state.hiddenInView);
        keepHeld(ctx.scene, state.lockedInView);
        state.carryClipboard(played, ctx.scene);
    }
    state.sceneDirty = dirtyAtCapture;

    if (discardingEdits) {
        LOG_WARNING("SceneSession: the edits made during the session were discarded");
        state.toasts.push(ToastKind::Warning, "The edits made during play were discarded");
    } else if (!inPlace) {
        LOG_WARNING("SceneSession: a prefab instance came back in other slots; undo history dropped");
        state.toasts.push(ToastKind::Warning, "Undo history cleared: a prefab instance came back elsewhere");
    }
}

void SceneSession::stopPlaySession(FrameContext& ctx, EditorState& state) {
    // Paused first, so a session a failed restore keeps lives on frozen.
    ctx.clock.setPaused(true);
    restoreSnapshot(ctx, state);
}

void SceneSession::drawDialogs(FrameContext& ctx, EditorState& state) {
    if (beginDialog("Save Scene As", m_openSaveAsPopup)) {
        ImGui::TextDisabled("Saved into %s (.json appended automatically)", Roots::scenes().string().c_str());
        // The name is what the dialog is for: typing replaces the one offered.
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(EditorStyle::px(360.0f));
        // Fed to dialogButtons as fieldCommitted: ImGui swallows the plain
        // Enter while the field is active.
        const bool enterCommit = ImGui::InputText(
            "##SaveAsName",
            m_saveAsBuffer,
            sizeof(m_saveAsBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll
        );

        // .json is appended when missing: the Load picker filters by extension and would never list it.
        std::string name = m_saveAsBuffer;
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
        size_t lead = 0;
        while (lead < name.size() && (name[lead] == ' ' || name[lead] == '\t')) ++lead;
        if (lead > 0) name.erase(0, lead);
        std::string finalName = name;
        if (!finalName.empty()) {
            const std::filesystem::path p(finalName);
            if (p.extension() != ".json") finalName += ".json";
        }
        const std::string finalPath = (Roots::scenes() / finalName).string();
        const bool empty = finalName.empty();
        std::error_code existsEc;
        const bool collides = !empty && std::filesystem::exists(finalPath, existsEc);
        if (!empty && name != finalName) {
            ImGui::TextDisabled("Will save as: %s", finalName.c_str());
        }
        if (collides) {
            ImGui::TextColored(EditorStyle::WARNING, "Overwrites existing %s", finalName.c_str());
        }

        const DialogResult r = dialogButtons(
            m_openSaveAsPopup,
            collides ? "Overwrite" : "Save",
            !empty,
            enterCommit
        );
        if (r == DialogResult::Confirm) {
            // First-time save: the scenes/ directory may not exist yet.
            std::error_code mkdirEc;
            std::filesystem::create_directories(Roots::scenes(), mkdirEc);
            // Set before the write: a failed Save-As leaves the new path
            // current, so Ctrl+S retries where the user asked to go.
            const std::string from = m_currentScenePath;
            m_currentScenePath = finalPath;
            // Saved elsewhere, the scene left behind has no unsaved changes of its own.
            if (writeScene(ctx, state, m_currentScenePath)) {
                SceneRecovery::discard(recoveryCopyOf(from));
                startGameIn(state, m_currentScenePath);
            }
        }
        endDialog();
    }

    if (beginDialog("Recover Unsaved Changes", m_openRecoveryPopup)) {
        const std::string shown = m_currentScenePath.empty()
            ? std::string("the untitled scene")
            : std::filesystem::path(m_currentScenePath).filename().string();
        ImGui::Text("The editor closed without saving %s.", shown.c_str());
        ImGui::TextDisabled("A copy of its unsaved changes was kept.");
        ImGui::TextDisabled("Cancel keeps it, and keeps no newer one, until the scene is saved or left.");
        switch (dialogButtons(m_openRecoveryPopup, "Recover", "Discard")) {
            case DialogResult::Confirm: state.requests.ask(Ask::RecoverScene); break;
            case DialogResult::Alt:     discardRecovery(); break;
            case DialogResult::Cancel:
            case DialogResult::None:    break;
        }
        endDialog();
    }

    // A pick is asked for rather than loaded: opening throws the live scene away,
    // so it goes through the unsaved-changes guard.
    std::string picked;
    if (m_loadPicker.draw(picked)) {
        state.sceneActions.request(SceneActions::Kind::Open, picked);
    }
}

} // namespace Vkm::Engine
