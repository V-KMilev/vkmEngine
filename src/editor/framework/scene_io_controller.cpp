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
    // Only what the outgoing scene never named can be an import left behind;
    // everything else went with the scene that owned it, which is not news. And
    // only what the new graph does not already hold is gone: two scenes sharing
    // a sound would otherwise be reported as a loss that never happened.
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
    // A play session owns the scene, so there is nothing here worth writing:
    // the world holds whatever the simulation has done to a scratch copy, the
    // authored scene is the snapshot, and Stop is about to swap the two. A
    // write would put the played scene under the authored one's name and then
    // clear the dirty flag - which Stop restores to its pre-Play value, leaving
    // the editor claiming the file matches a scene it was never given. Refused
    // rather than quietly redirected at the snapshot: the author asked to save
    // what they are looking at, and what they are looking at is not theirs to
    // keep.
    if (refusedDuringPlay(state)) return false;

    // Bake every referenced asset into the cooked library + manifest, then write
    // the scene as name-only references to those cooked assets. A partial cook
    // does not stop the save - the scene itself is still worth writing, and the
    // toast says which half went wrong.
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
    if (!hasSnapshot()) return false;
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
    m_currentScenePath = path;
    load(ctx, state);
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

void SceneIOController::load(FrameContext& ctx, EditorState& state) {
    if (m_currentScenePath.empty()) {
        m_currentScenePath = (ProjectPaths::scenes() / "scene.json").string();
    }

    // Cache the selection's Name (if any) BEFORE attempting the load -
    // matching by name post-load is more robust than matching by slot
    // index alone, which can silently select a different entity that
    // happens to land in the same slot after the new scene populates it.
    const std::string priorSelectionName = cacheSelectionName(ctx, state);

    // If a play session is somehow active, tear its behaviors down (onDestroy)
    // before the swap discards them.
    BehaviorSystem::endSession(ctx.scene);

    // WHAT AN OPEN DOES TO THE SESSION'S IMPORTS, stated because the answer is
    // not the one Stop gives and the difference has to be a decision rather
    // than an omission. Stop promises to put the session back exactly as Play
    // found it, so captureSnapshot records the whole asset list and
    // restoreSnapshot feeds it back. An open makes no such promise: it is the
    // editor's clean break, and it already drops the undo stack, the selection,
    // the material previews and the camera binding on the way through. An asset
    // nothing in the outgoing scene pointed at belongs to that session the same
    // way those do, and carrying it forward would grow the graph by a whole
    // scene's worth of assets per open and cook every one of them into the
    // project library at the next save.
    //
    // So they go - and are counted first, because the one thing that made this
    // a defect rather than a rule was that nothing said it. The source files
    // are still on disk and importing them again is one dialog; not knowing
    // they left is what costs an afternoon.
    const std::set<AssetKey> beforeAll =
        assetKeysOf(AssetSerializer::saveAllAssets(ctx.resources));
    const std::set<AssetKey> beforeNamed =
        assetKeysOf(AssetSerializer::saveAssetsForScene(ctx.scene, ctx.resources));

    if (!SceneSerializer::load(ctx.scene, ctx.resources, m_currentScenePath)) {
        LOG_ERROR("SceneIOController::load: failed to load %s - editor state preserved",
            m_currentScenePath.c_str());
        state.pushToast(EditorState::ToastKind::Error,
            "Load failed: " + m_currentScenePath);
        return;
    }

    reportDroppedImports(ctx, state, beforeAll, beforeNamed);

    // Opening a scene replaces the world a running session was taken from, so
    // it ends that session for the same reason New Scene does. Done after the
    // load rather than before it, because a load that fails leaves the outgoing
    // scene live and its session is still the session it belongs to.
    endPlaySession(ctx);

    // Entity ids do not carry across scenes: the file just loaded fills the
    // slots with its own entities, so a pending undo would assign a component
    // value onto whatever unrelated entity now sits at the slot it recorded.
    state.commands.clear();
    afterSceneReplace(ctx, state, priorSelectionName, m_currentScenePath);

    state.sceneDirty = false;
    pushRecentPath(state.recentScenes, m_currentScenePath);
}

void SceneIOController::endPlaySession(FrameContext& ctx) {
    // The play snapshot is a copy of the scene going away. Left behind, the
    // transport still reads as playing and Stop would restore the outgoing
    // scene over whatever replaced it - measured, opening a scene mid-session
    // and pressing Stop put the previous world back under the new scene's name,
    // with the title bar still naming the file Save would then overwrite. The
    // asset list goes with the snapshot, being the other half of it. The clock
    // goes too: dropping the snapshot ends the session, and a session that has
    // ended is Edit mode - paused, and back at 1x whatever a script scaled it
    // to.
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

    // The asset graph goes with the scene it belonged to. An open already does
    // this - the serializer swaps a staging manager in - and the reasoning
    // there ("carrying the strays forward would grow the graph by a scene's
    // worth of assets per open and cook every one into the project library")
    // was never applied to the sibling that throws a scene away outright.
    // Left in place, buildDefaultScene's cube and default material collided
    // with the outgoing scene's and took a " (2)" suffix from ensureUniqueName
    // - and names being the serializable identity, that suffix became the new
    // scene's frozen identity, in the file and in the cooked manifest.
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

    // The same seed the engine boots with when a project names no scene: one
    // definition, so New Scene and a fresh start cannot drift apart. Called
    // directly rather than through EditorActions so a brand-new scene carries
    // no undo entries or selection side effects.
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
    // The undo history is deliberately NOT dropped here. Two of the three swaps
    // that come through replace every entity and must drop it; the third puts
    // back the very scene it took the snapshot of, ids and all. Each caller
    // says which it is, where the reason is true.

    // The Hierarchy panel's cached root list. It otherwise only
    // rebuilds when the entity count moves, so reloading the same scene (or
    // Stop after a play session that spawned nothing) would keep drawing the
    // outgoing scene's ids.
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

    // Re-bind the camera controller to the new scene's active Camera, by the
    // same rule the renderer uses. If multiple cameras claim active (authoring
    // oversight), that rule picks the first by iteration order - warn, because
    // silently picking one of several would make the choice look intentional.
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
    // The snapshot is the scene file format, and that format promises that every
    // asset name it writes is one the library can hand back. An asset imported
    // this session and never baked has no manifest entry, so on Stop its name
    // resolves to nothing while the ResourceManager still holding it is thrown
    // away by the restoring swap - the import vanishes from the component and
    // from the Asset Browser both. writeScene bakes before it writes for exactly
    // this reason; the snapshot writes the same document and needs the same bake.
    const bool cooked = AssetCooker::cookAllAssets(ctx.resources);

    m_playSnapshot = SceneSerializer::saveToString(ctx.scene, ctx.resources);
    if (m_playSnapshot.empty()) {
        LOG_ERROR("SceneIOController::captureSnapshot: failed to serialize scene");
        state.pushToast(EditorState::ToastKind::Error,
            "Play: could not snapshot scene (Stop will not restore)");
        m_playAssets.clear();
        return;
    }
    // The scene document names the assets the scene uses and nothing else, which
    // is right for a file and not enough for a restore. A sound imported and not
    // yet assigned to a source is in the Asset Browser and in every picker, and
    // no component points at it - so the scene never mentions it and the
    // restoring swap throws it away with the manager that held it. Recording the
    // session's whole list here is what makes Stop put back what Play found.
    m_playAssets = AssetSerializer::saveAllAssets(ctx.resources).dump();
    // A partial cook does not stop Play, by the same rule the save follows: the
    // session is still worth entering, and the toast names what Stop may lose.
    if (!cooked) {
        state.pushToast(EditorState::ToastKind::Error,
            "Some assets did not cook; Stop may not restore them");
    }
    // Remember the dirty flag so Stop leaves it exactly as the user left it -
    // simulation mutates the ECS directly (not via editor commands), so it
    // never dirties the scene on its own. The author editing during the session
    // does, though, and the same flag cannot tell the two apart: the history
    // revision beside it is what Stop reads the author's share off.
    m_playSnapshotDirty   = state.sceneDirty;
    m_playSnapshotHistory = state.commands.revision();
}

void SceneIOController::restoreSnapshot(FrameContext& ctx, EditorState& state) {
    if (m_playSnapshot.empty()) return;

    // What the session did beyond simulating. Every panel stays live in play
    // mode, so the world about to be thrown away can hold entities the author
    // created and fields they typed - and the editor called each of those
    // authored work as it happened, by pushing an undo step and raising the
    // dirty marker. Read here, before the swap moves either, and said below
    // because withdrawing both without a word is what made the loss
    // invisible.
    const bool historyMoved    = state.commands.revision() != m_playSnapshotHistory;
    const bool discardingEdits = historyMoved
                              || (state.sceneDirty && !m_playSnapshotDirty);

    const std::string priorSelectionName = cacheSelectionName(ctx, state);

    // Play stop: fire onDestroy on the played scene's running behaviors while
    // their context is still valid, before the swap restores the snapshot.
    BehaviorSystem::endSession(ctx.scene);

    if (!SceneSerializer::loadFromString(m_playSnapshot, ctx.scene, ctx.resources)) {
        LOG_ERROR("SceneIOController::restoreSnapshot: failed to restore play snapshot");
        state.pushToast(EditorState::ToastKind::Error,
            "Stop: could not restore scene snapshot");
        return;  // Keep the snapshot so the live (played) scene is untouched.
    }

    // The load brought back the assets the scene names; this brings back the
    // ones it does not. loadAssets skips every name already present, so it
    // recreates exactly what the swap dropped, from the library entries the
    // cook at capture guaranteed each of them. It runs before the housekeeping
    // below so the panels' first draw after Stop sees the whole graph.
    if (nlohmann::json assets = nlohmann::json::parse(m_playAssets, nullptr, false);
        !assets.is_discarded()) {
        AssetSerializer::loadAssets(assets, ctx.resources);
    }

    // The session is over, so it ends the one way every path ends one: the
    // snapshot, its asset list and the clock go together. The dirty flag is
    // read out first because that is what endPlaySession resets.
    const bool dirtyAtCapture = m_playSnapshotDirty;
    endPlaySession(ctx);

    // The history survives a Stop that the session never touched, and that is
    // the whole difference between this swap and an open. A load fills the
    // slots with another file's entities; this one writes back the document
    // captured from these entities, at the ids they had - SceneSerializer keys
    // every record on the slot index and rebuilds through createEntityAt - so
    // every step still names what it named before Play. Edit, Play to check,
    // Stop, undo the bad edit is the loop that was silently missing its last
    // step.
    //
    // A session that DID move the history is the case an open's reasoning
    // really covers: those steps address entities of the played world, which is
    // the thing being thrown away, and the ones underneath them can no longer
    // be reached past steps that would apply to nothing. It goes, and says so.
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

void SceneIOController::requestOpenPath(FrameContext& ctx, EditorState& state, const std::string& path) {
    if (state.sceneDirty) {
        state.confirmAction    = EditorState::PendingSceneAction::Open;
        state.pendingScenePath = path;
        return;
    }
    loadPath(ctx, state, path);
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

        // Canonicalise the filename: trim whitespace, append .json if the
        // user didn't, and check for an existing file under scenes/. Without
        // these the Load picker (which filters by .json) would silently fail
        // to list a saved-without-extension file.
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

    // requestLoad() configured and opened the shared picker; a pick routes
    // through the same loadPath() the recent-scenes menu uses.
    std::string picked;
    if (m_loadPicker.draw(picked)) {
        requestOpenPath(ctx, state, picked);
    }
}

} // namespace Vkm::Engine
