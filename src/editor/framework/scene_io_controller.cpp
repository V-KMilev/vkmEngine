#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/scene_io_controller.h"

#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include "logger.h"

#include "core/clock.h"
#include "core/system.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/camera.h"
#include "ecs/scene.h"
#include "framework/editor_state.h"
#include "framework/material_preview_session.h"
#include "io/asset/asset_serializer.h"
#include "io/scene/scene_serializer.h"
#include "io/project_paths.h"
#include "cook/asset_cooker.h"
#include "resource/resource_manager.h"
#include "resource/asset/font_asset.h"
#include "generator/default_scene.h"
#include "generator/light_generators.h"
#include "system/camera/camera_controller_system.h"
#include "system/script/behavior_system.h"
#include "ui/editor_style.h"
#include "ui/editor_dialogs.h"

namespace Vkm::Engine {

namespace {

// One asset's serializable identity: the section it was written under and the
// name it is found by. The section is carried because names are only unique
// within one - a mesh and a texture may share a name - and this is a set the
// whole assets block goes into at once.
using AssetKey = std::pair<std::string, std::string>;

// Every identity an assets block holds. Reads whatever sections the block
// happens to have rather than a list of them, so a new asset kind is counted
// here the day AssetSerializer starts writing it.
std::set<AssetKey> assetKeysOf(const nlohmann::json& block) {
    std::set<AssetKey> keys;
    if (!block.is_object()) return keys;
    for (auto section = block.begin(); section != block.end(); ++section) {
        if (!section.value().is_array()) continue;
        for (const nlohmann::json& entry : section.value()) {
            const std::string name = entry.value("name", std::string{});
            if (!name.empty()) keys.emplace(section.key(), name);
        }
    }
    return keys;
}

// Say which of the session's imports the open just left behind. Takes the two
// sets read before the swap - everything the graph held, and the subset the
// outgoing scene named - and reads the third off the graph the load produced.
void reportDroppedImports(
    FrameContext& ctx,
    EditorState& state,
    const std::set<AssetKey>& beforeAll,
    const std::set<AssetKey>& beforeNamed
) {
    // Only what the outgoing scene never named can be a left-behind import, and
    // only what the new graph does not already hold is gone - two scenes sharing
    // a sound is not a loss.
    const std::set<AssetKey> nowHeld =
        assetKeysOf(AssetSerializer::saveAllAssets(ctx.resources));

    std::vector<std::string> dropped;
    for (const AssetKey& key : beforeAll) {
        if (beforeNamed.count(key) || nowHeld.count(key)) continue;
        dropped.push_back(key.first + " '" + key.second + "'");
    }
    if (dropped.empty()) return;

    // The log names them so they can be imported again from the message alone;
    // the toast carries the count, because a list is not what a toast is for.
    for (const std::string& what : dropped) {
        LOG_INFO("Left an unused import behind: %s", what.c_str());
    }
    state.pushToast(EditorState::ToastKind::Info,
        std::to_string(dropped.size()) + " unused import(s) stayed with the previous scene");
}

} // namespace

SceneIOController::SceneIOController(
    CameraControllerSystem& cameraController,
    MaterialPreviewSession& materialPreviews
)
    : m_cameraController(cameraController)
    , m_materialPreviews(materialPreviews)
{}

SceneIOController::~SceneIOController() = default;

bool SceneIOController::writeScene(FrameContext& ctx, EditorState& state, const std::string& path) {
    if (refusedDuringPlay(state)) return false;

    // A partial cook does not stop the save: the scene itself is still worth
    // writing, and the toast below says which half went wrong.
    const bool cooked = AssetCooker::cookAllAssets(ctx.resources);

    const std::string shown = std::filesystem::path(path).filename().string();
    if (!SceneSerializer::save(ctx.scene, ctx.resources, path)) {
        LOG_ERROR("SceneIOController: failed to write %s - scene remains dirty",
            path.c_str());
        state.pushToast(EditorState::ToastKind::Error, "Save failed: " + shown);
        return false;
    }
    state.sceneDirty = false;
    pushRecentPath(state.recentScenes, path);
    if (!cooked) {
        state.pushToast(EditorState::ToastKind::Error,
            "Saved " + shown + ", but some assets did not cook");
        return true;
    }
    state.pushToast(EditorState::ToastKind::Info, "Saved " + shown);
    return true;
}

bool SceneIOController::refusedDuringPlay(EditorState& state) const {
    if (!isPlaying()) return false;
    state.pushToast(EditorState::ToastKind::Warning,
        "Stop the play session before saving - the running scene is not the authored one");
    return true;
}

void SceneIOController::save(FrameContext& ctx, EditorState& state) {
    // Answered here as well as in writeScene so a session does not get as far
    // as a Save-As prompt for a name it would then refuse to write.
    if (refusedDuringPlay(state)) return;
    if (m_currentScenePath.empty()) {
        requestSaveAs();
        return;
    }
    writeScene(ctx, state, m_currentScenePath);
}

void SceneIOController::loadPath(FrameContext& ctx, EditorState& state, const std::string& path) {
    // The name follows the scene, and only if the scene moves: left pointing at
    // a scene that never opened, the title names it, and the next Ctrl+S writes
    // the world still on screen over the one file the author wanted kept.
    std::string previous = m_currentScenePath;
    m_currentScenePath = path;
    if (!load(ctx, state)) m_currentScenePath = std::move(previous);
}

void SceneIOController::adoptPath(EditorState& state, const std::string& path) {
    m_currentScenePath = path;
    // A scene that opened is a scene you have opened, so it belongs in the
    // list that takes you back to it - the same entry load() makes. Guarded
    // because the two worlds with no file must not put an empty name there.
    if (!path.empty()) pushRecentPath(state.recentScenes, path);
}

void SceneIOController::requestSaveAs() {
    m_openSaveAsPopup = true;
}

void SceneIOController::requestLoad() {
    m_loadPicker.options.popupId    = "Load Scene";
    m_loadPicker.options.title      = "Load Scene";
    m_loadPicker.options.root       = ProjectPaths::scenes();
    m_loadPicker.options.recursive  = false;
    m_loadPicker.options.kind       = AssetPicker::Kind::Files;
    m_loadPicker.options.extensions = {".json"};
    m_loadPicker.options.relativeTo.clear();  // loadPath() wants an absolute path
    m_loadPicker.options.hint.clear();
    m_loadPicker.open();
}

bool SceneIOController::isSaveDialogActive() const {
    // The intent flag now stays set for the dialog's whole lifetime (the
    // dialog scaffold clears it on any dismissal), but keep the popup check
    // for the single frame between CloseCurrentPopup and the next Begin.
    return m_openSaveAsPopup || ImGui::IsPopupOpen("Save Scene As");
}

bool SceneIOController::load(FrameContext& ctx, EditorState& state) {
    if (m_currentScenePath.empty()) {
        m_currentScenePath = (ProjectPaths::scenes() / "scene.json").string();
    }

    // By name, not by slot: the new scene fills the same slots with its own
    // entities, so a slot match silently selects a different one.
    const std::string priorSelectionName = cacheSelectionName(ctx, state);

    // If a play session is somehow active, tear its behaviors down (onDestroy)
    // before the swap discards them.
    BehaviorSystem::endSession(ctx.scene);

    // Counted before the swap, so the strays it leaves can be named; see
    // docs/reference/editor.md, "What an open does to the session's imports".
    const std::set<AssetKey> beforeAll =
        assetKeysOf(AssetSerializer::saveAllAssets(ctx.resources));
    const std::set<AssetKey> beforeNamed =
        assetKeysOf(AssetSerializer::saveAssetsForScene(ctx.scene, ctx.resources));

    if (!SceneSerializer::load(ctx.scene, ctx.resources, m_currentScenePath)) {
        LOG_ERROR("SceneIOController::load: failed to load %s - editor state preserved",
            m_currentScenePath.c_str());
        state.pushToast(EditorState::ToastKind::Error,
            "Load failed: " + m_currentScenePath);
        return false;
    }

    reportDroppedImports(ctx, state, beforeAll, beforeNamed);

    // After the load, not before: a load that fails leaves the outgoing scene
    // live, and its session is still the session it belongs to.
    endPlaySession(ctx);

    // Entity ids do not carry across scenes: the file just loaded fills the
    // slots with its own entities, so a pending undo would assign a component
    // value onto whatever unrelated entity now sits at the slot it recorded.
    state.commands.clear();
    afterSceneReplace(ctx, state, priorSelectionName, m_currentScenePath);

    state.sceneDirty = false;
    pushRecentPath(state.recentScenes, m_currentScenePath);
    return true;
}

void SceneIOController::endPlaySession(FrameContext& ctx) {
    // The asset list is the snapshot's other half, and a session that has ended
    // is Edit mode - paused, and back at 1x whatever a script scaled it to.
    m_playSnapshot.clear();
    m_playAssets.clear();
    m_playSnapshotDirty = false;
    ctx.clock.setPaused(true);
    ctx.clock.setTimeScale(1.0f);
}

void SceneIOController::beginSceneReplace(FrameContext& ctx, EditorState& state) {
    // Tear down any live behaviors before their entities vanish - and while the
    // module holding their code is still loaded.
    BehaviorSystem::endSession(ctx.scene);

    // Counted before the graph goes, by the same rule an open counts them.
    const std::set<AssetKey> beforeAll =
        assetKeysOf(AssetSerializer::saveAllAssets(ctx.resources));
    const std::set<AssetKey> beforeNamed =
        assetKeysOf(AssetSerializer::saveAssetsForScene(ctx.scene, ctx.resources));

    ctx.scene.clear();

    // The asset graph goes with the scene it belonged to; see
    // docs/reference/editor.md, "What an open does to the session's imports".
    ResourceManager replaced;
    ctx.resources.swap(replaced);
    // Fonts are engine-owned, baked once at startup and never written to a
    // scene, so they are kept across this swap exactly as the loader keeps them
    // across its own - without it every UIText loses the font it names.
    ctx.resources.swapSlot<FontAsset>(replaced);

    reportDroppedImports(ctx, state, beforeAll, beforeNamed);

    m_currentScenePath.clear();

    endPlaySession(ctx);

    // Same rule as a load, one step further: the scene is empty now, so every
    // entity the history addresses is gone and nothing it holds can be applied.
    state.commands.clear();
    afterSceneReplace(ctx, state, /*priorSelectionName*/ {}, /*eventPath*/ {});
}

void SceneIOController::newScene(FrameContext& ctx, EditorState& state) {
    beginSceneReplace(ctx, state);

    // The same seed the engine boots with when a project names no scene, so the
    // two cannot drift. Called directly rather than through EditorActions: a
    // brand-new scene carries no undo entries and no selection side effects.
    buildDefaultScene(ctx.scene, ctx.resources);

    state.sceneDirty = false;
    state.pushToast(EditorState::ToastKind::Info, "New scene");
}

std::string SceneIOController::cacheSelectionName(FrameContext& ctx, EditorState& state) {
    if (state.selectedEntity && ctx.scene.isAlive(state.selectedEntity)
            && ctx.scene.has<Name>(state.selectedEntity)) {
        return ctx.scene.get<Name>(state.selectedEntity).value;
    }
    return {};
}

void SceneIOController::afterSceneReplace(
    FrameContext& ctx,
    EditorState& state,
    const std::string& priorSelectionName,
    const std::string& eventPath
) {
    // The Hierarchy's cached root list otherwise rebuilds only when the entity
    // count moves, so reloading the same scene - or Stop after a session that
    // spawned nothing - would keep drawing the outgoing scene's ids.
    state.hierarchyDirty = true;

    // The swap replaced the ResourceManager wholesale, so preview targets
    // keyed by the old asset handles are stale. Drop them; the Material
    // Editor / Asset Browser re-bake lazily on their next draw.
    m_materialPreviews.clear();

    // Same reason: the pinned material is a handle into the manager that just
    // went away. The Material Editor falls back to the selection until the
    // user pins another one.
    state.materialEditorTarget = {};

    // Selection survives only when an entity with the same Name exists in
    // the new scene. Anonymous selections (no Name) are dropped rather than
    // potentially landing on the wrong entity.
    state.deselect();
    if (!priorSelectionName.empty()) {
        ctx.scene.forEach<Name>([&](EntityId id, const Name& n) {
            if (!state.selectedEntity && priorSelectionName == n.value) {
                state.selectEntity(id);
            }
        });
    }

    // The new scene's active Camera, by the same rule the renderer uses: first
    // by iteration order. Several claiming active is an authoring slip, and the
    // warning keeps a silent pick from looking intentional.
    int activeCount = 0;
    ctx.scene.forEach<Camera, Transform>([&](EntityId, const Camera& c, const Transform&) {
        if (c.active) ++activeCount;
    });
    if (activeCount > 1) {
        LOG_WARNING("SceneIOController: %d cameras marked active in %s - using the first",
            activeCount, eventPath.c_str());
    }
    m_cameraController.setCameraEntity(findActiveCamera(ctx.scene));
}

void SceneIOController::captureSnapshot(FrameContext& ctx, EditorState& state) {
    // The snapshot is the scene file format, so it names assets the library has
    // to be able to hand back - an unbaked import would come back empty.
    const bool cooked = AssetCooker::cookAllAssets(ctx.resources);

    m_playSnapshot = SceneSerializer::saveToString(ctx.scene, ctx.resources);
    if (m_playSnapshot.empty()) {
        LOG_ERROR("SceneIOController::captureSnapshot: failed to serialize scene");
        state.pushToast(EditorState::ToastKind::Error,
            "Play: could not snapshot scene (Stop will not restore)");
        m_playAssets.clear();
        return;
    }
    // The scene document names only what the scene uses, so the session's whole
    // list is recorded beside it - an unassigned import is in no component.
    m_playAssets = AssetSerializer::saveAllAssets(ctx.resources).dump();
    // A partial cook does not stop Play, by the same rule the save follows: the
    // session is still worth entering, and the toast names what Stop may lose.
    if (!cooked) {
        state.pushToast(EditorState::ToastKind::Error,
            "Some assets did not cook; Stop may not restore them");
    }
    // Simulation writes the ECS directly, so a session never dirties the scene
    // on its own; the author editing inside one does, and the history revision
    // beside the flag is what Stop reads that share off.
    m_playSnapshotDirty   = state.sceneDirty;
    m_playSnapshotHistory = state.commands.revision();
}

void SceneIOController::restoreSnapshot(FrameContext& ctx, EditorState& state) {
    if (m_playSnapshot.empty()) return;

    // Read before the swap moves either: the undo revision and the dirty flag
    // are how the session's authored work is told from what it merely simulated.
    const bool historyMoved    = state.commands.revision() != m_playSnapshotHistory;
    const bool discardingEdits = historyMoved
                              || (state.sceneDirty && !m_playSnapshotDirty);

    const std::string priorSelectionName = cacheSelectionName(ctx, state);

    // Play stop: fire onDestroy on the played scene's running behaviors while
    // their context is still valid, before the swap restores the snapshot.
    BehaviorSystem::endSession(ctx.scene);

    // The assets first and in place, so the handles the undo history holds go on
    // naming what they named; see docs/reference/editor.md, "What an open does
    // to the session's imports". Ahead of the load, which resolves names.
    if (nlohmann::json assets = nlohmann::json::parse(m_playAssets, nullptr, false);
        !assets.is_discarded()) {
        AssetSerializer::loadAssets(assets, ctx.resources, AssetSerializer::LoadMode::Reload);
    }

    if (!SceneSerializer::loadFromString(m_playSnapshot, ctx.scene, ctx.resources)) {
        LOG_ERROR("SceneIOController::restoreSnapshot: failed to restore play snapshot");
        state.pushToast(EditorState::ToastKind::Error,
            "Stop: could not restore scene snapshot");
        return;  // Keep the snapshot so the live (played) scene is untouched.
    }

    // The session is over, so it ends the one way every path ends one: the
    // snapshot, its asset list and the clock go together. The dirty flag is
    // read out first because that is what endPlaySession resets.
    const bool dirtyAtCapture = m_playSnapshotDirty;
    endPlaySession(ctx);

    // Kept when the session never touched it, dropped when it did; see
    // docs/reference/editor.md, "What an open does to the session's imports".
    if (historyMoved) state.commands.clear();
    afterSceneReplace(ctx, state, priorSelectionName, m_currentScenePath);
    state.sceneDirty = dirtyAtCapture;

    if (discardingEdits) {
        // A warning, not a note: this is the one thing Stop does that cannot be
        // taken back - the history addressed a scene that no longer exists, so
        // it goes with it and there is nothing left to undo.
        LOG_WARNING("SceneIOController: Stop discarded the edits made during the session");
        state.pushToast(EditorState::ToastKind::Warning,
            "Stop discarded the edits made during play");
    }
}

void SceneIOController::stopPlaySession(FrameContext& ctx, EditorState& state) {
    // Pausing first is what makes a failed restore safe: restoreSnapshot keeps
    // the snapshot when the swap fails, so the session lives on, and it should
    // live on frozen rather than running a scene the editor could not put back.
    ctx.clock.setPaused(true);
    restoreSnapshot(ctx, state);
}

void SceneIOController::drawDialogs(FrameContext& ctx, EditorState& state) {
    if (beginDialog("Save Scene As", m_openSaveAsPopup)) {
        ImGui::TextDisabled("Saved into %s (.json appended automatically)",
                            ProjectPaths::scenes().string().c_str());
        ImGui::SetNextItemWidth(EditorStyle::px(360.0f));
        // Fed to dialogButtons as fieldCommitted: ImGui swallows the plain
        // Enter while the field is active.
        const bool enterCommit = ImGui::InputText("##SaveAsName", m_saveAsBuffer,
            sizeof(m_saveAsBuffer), ImGuiInputTextFlags_EnterReturnsTrue);

        // Trim whitespace and append .json if the user did not: the Load picker
        // filters by extension, so a scene saved without one is saved where the
        // editor will never list it again.
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
        const std::string finalPath = (ProjectPaths::scenes() / finalName).string();
        const bool empty = finalName.empty();
        const bool collides = !empty && std::filesystem::exists(finalPath);
        if (!empty && name != finalName) {
            ImGui::TextDisabled("Will save as: %s", finalName.c_str());
        }
        if (collides) {
            ImGui::TextColored(EditorStyle::WARNING,
                "Overwrites existing %s", finalName.c_str());
        }

        const DialogResult r = dialogButtons(m_openSaveAsPopup,
                                             collides ? "Overwrite" : "Save",
                                             !empty, enterCommit);
        if (r == DialogResult::Confirm) {
            // First-time save: the scenes/ directory may not exist yet.
            std::error_code mkdirEc;
            std::filesystem::create_directories(
                ProjectPaths::scenes(), mkdirEc);
            // Set before the write: a failed Save-As leaves the new path
            // current, so Ctrl+S retries where the user asked to go.
            m_currentScenePath = finalPath;
            writeScene(ctx, state, m_currentScenePath);
        }
        endDialog();
    }

    // requestLoad() configured and opened the shared picker. A pick is asked for
    // rather than loaded: opening throws the live scene away, so it goes through
    // the same guard the recent-scenes menu uses.
    std::string picked;
    if (m_loadPicker.draw(picked)) {
        state.requestSceneAction(EditorState::SceneAction::Open, picked);
    }
}

} // namespace Vkm::Engine
