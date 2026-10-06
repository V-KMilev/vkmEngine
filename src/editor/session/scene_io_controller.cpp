#define VKM_LOG_CATEGORY "EDITOR"

#include "session/scene_io_controller.h"

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
#include "editor_state.h"
#include "session/material_preview_session.h"
#include "io/asset/asset_serializer.h"
#include "io/scene/scene_serializer.h"
#include "io/project_paths.h"
#include "cook/asset_cooker.h"
#include "platform/input/input_map.h"
#include "platform/window/window_manager.h"
#include "resource/resource_manager.h"
#include "resource/generate/default_scene.h"
#include "input/camera_controller_system.h"
#include "system/script/behavior_system.h"
#include "ui/editor_style.h"
#include "ui/editor_dialogs.h"

namespace Vkm::Engine {

namespace {

// One asset's serializable identity: the section it was written under and the
// name it is found by. The section is carried because names are only unique
// within one - a mesh and a texture may share a name.
using AssetKey = std::pair<std::string, std::string>;

// Reads whatever sections the block happens to have rather than a list of them,
// so a new asset kind is counted here the day AssetSerializer starts writing it.
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

// What the graph held before a swap and the subset the outgoing scene named,
// read before it so the strays it leaves can be named; see
// docs/reference/editor.md, "What an open does to the session's imports".
struct HeldImports {
    std::set<AssetKey> all;
    std::set<AssetKey> named;
};

HeldImports heldImports(FrameContext& ctx) {
    return HeldImports{
        assetKeysOf(AssetSerializer::saveAllAssets(ctx.resources)),
        assetKeysOf(AssetSerializer::saveAssetsForScene(ctx.scene, ctx.resources))
    };
}

// Say which of the session's imports the swap just left behind, reading what
// the graph holds now against what it held before.
void reportDroppedImports(FrameContext& ctx, EditorState& state, const HeldImports& before) {
    // Only what the outgoing scene never named can be a left-behind import, and
    // only what the new graph does not already hold is gone - two scenes sharing
    // a sound is not a loss.
    const std::set<AssetKey> nowHeld =
        assetKeysOf(AssetSerializer::saveAllAssets(ctx.resources));

    std::vector<std::string> dropped;
    for (const AssetKey& key : before.all) {
        if (before.named.count(key) || nowHeld.count(key)) continue;
        dropped.push_back(key.first + " '" + key.second + "'");
    }
    if (dropped.empty()) return;

    for (const std::string& what : dropped) {
        LOG_INFO("Left an unused import behind: %s", what.c_str());
    }
    state.pushToast(
        ToastKind::Info,
        std::to_string(dropped.size()) + " unused import(s) stayed with the previous scene"
    );
}

// The active entity's name, for a swap that fills the slots with other
// entities: an open matches the selection by name, since a slot match would
// silently select a different one.
std::string selectedName(const Scene& scene, const EditorState& state) {
    // tryGet is total, so it also answers for a selection that has gone away.
    const Name* name = scene.tryGet<Name>(state.selectedEntity);
    return name ? name->value : std::string{};
}

void selectByName(const Scene& scene, EditorState& state, const std::string& name) {
    if (name.empty()) return;
    scene.forEach<Name>([&](EntityId id, const Name& n) {
        if (!state.selectedEntity && name == n.value) state.selectEntity(id);
    });
}

// The whole selection by slot, the active entity last, for a swap that puts
// every entity back where it was: names need not be unique, and a Stop matching
// by name would lose the multi-selection and pick the first of two twins.
std::vector<uint32_t> selectedSlots(const EditorState& state) {
    std::vector<uint32_t> slots;
    for (const EntityId id : state.selection) {
        if (id != state.selectedEntity) slots.push_back(id.slot());
    }
    if (state.selectedEntity) slots.push_back(state.selectedEntity.slot());
    return slots;
}

void selectSlots(const Scene& scene, EditorState& state, const std::vector<uint32_t>& slots) {
    for (const uint32_t slot : slots) {
        if (scene.isAliveAtIndex(slot)) state.addToSelection(scene.entityAt(slot));
    }
}

} // namespace

SceneIOController::SceneIOController(
    CameraControllerSystem& cameraController,
    MaterialPreviewSession& materialPreviews,
    BehaviorSystem&         behaviors
)
    : m_cameraController(cameraController)
    , m_materialPreviews(materialPreviews)
    , m_behaviors(behaviors)
{}

SceneIOController::~SceneIOController() = default;

bool SceneIOController::writeScene(FrameContext& ctx, EditorState& state, const std::string& path) {
    if (refusedDuringPlay(state)) return false;

    // A partial cook does not stop the save: the scene itself is still worth
    // writing, and the toast below says which half went wrong. The binaries bake
    // after the save returns; the scene needs only the records.
    const bool cooked = AssetCooker::cookAllAssets(ctx.resources, AssetCooker::Bake::InBackground);

    const std::string shown = std::filesystem::path(path).filename().string();
    if (!SceneSerializer::save(ctx.scene, ctx.resources, path)) {
        LOG_ERROR("SceneIOController: failed to write %s - scene remains dirty", path.c_str());
        state.pushToast(ToastKind::Error, "Save failed: " + shown);
        return false;
    }
    state.sceneDirty = false;
    pushRecentPath(state.recentScenes, path);
    if (!cooked) {
        state.pushToast(ToastKind::Error, "Saved " + shown + ", but some assets did not cook");
        return true;
    }
    state.pushToast(ToastKind::Info, "Saved " + shown);
    return true;
}

bool SceneIOController::refusedDuringPlay(EditorState& state) const {
    if (!isPlaying()) return false;
    state.pushToast(
        ToastKind::Warning,
        "Stop the play session before saving - the running scene is not the authored one"
    );
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
    rememberView(state);
    std::string previous = m_currentScenePath;
    m_currentScenePath = path;
    if (!load(ctx, state)) m_currentScenePath = std::move(previous);
}

void SceneIOController::adoptPath(const Scene& scene, EditorState& state, const std::string& path) {
    m_currentScenePath = path;
    restoreView(scene, state);
    // Guarded because a world with no file must not put an empty name there.
    if (!path.empty()) pushRecentPath(state.recentScenes, path);
}

void SceneIOController::rememberView(EditorState& state) const {
    if (m_currentScenePath.empty()) return;
    state.sceneViews[ProjectPaths::toProjectRelative(m_currentScenePath)] = m_cameraController.viewpoint();
}

void SceneIOController::restoreView(const Scene& scene, const EditorState& state) {
    const auto saved = state.sceneViews.find(ProjectPaths::toProjectRelative(m_currentScenePath));
    if (!m_currentScenePath.empty() && saved != state.sceneViews.end()) {
        m_cameraController.setViewpoint(saved->second);
    } else {
        m_cameraController.startFrom(scene);
    }
}

void SceneIOController::setEjected(FrameContext& ctx, bool ejected) {
    m_play.setEjected(ejected, ctx.window);
}

void SceneIOController::holdEjectedCursorFree(FrameContext& ctx) {
    m_play.holdCursorFree(ctx.window);
}

void SceneIOController::requestSaveAs() {
    m_openSaveAsPopup = true;
}

void SceneIOController::requestLoad() {
    AssetPicker::Options options;
    options.title      = "Load Scene";
    options.root       = ProjectPaths::scenes();
    options.extensions = {".json"};
    options.relativeTo.clear();  // loadPath() wants an absolute path
    m_loadPicker.open(options);
}

bool SceneIOController::isSaveDialogActive() const {
    // The intent flag stays set for the dialog's whole lifetime (the
    // dialog scaffold clears it on any dismissal), but keep the popup check
    // for the single frame between CloseCurrentPopup and the next Begin.
    return m_openSaveAsPopup || ImGui::IsPopupOpen("Save Scene As");
}

bool SceneIOController::load(FrameContext& ctx, EditorState& state) {
    if (m_currentScenePath.empty()) {
        m_currentScenePath = (ProjectPaths::scenes() / "scene.json").string();
    }

    const std::string priorSelection = selectedName(ctx.scene, state);

    // If a play session is somehow active, tear its behaviors down (onDestroy)
    // before the swap discards them.
    m_behaviors.endSession(ctx.scene);

    const HeldImports before = heldImports(ctx);
    if (!SceneSerializer::load(ctx.scene, ctx.resources, m_currentScenePath)) {
        LOG_ERROR(
            "SceneIOController::load: failed to load %s - editor state preserved",
            m_currentScenePath.c_str()
        );
        state.pushToast(ToastKind::Error, "Load failed: " + m_currentScenePath);
        return false;
    }

    reportDroppedImports(ctx, state, before);

    // After the load, not before: a load that fails leaves the outgoing scene
    // live, and its session is still the session it belongs to. Entity ids do
    // not carry across scenes, so the history goes with it.
    afterSceneReplace(ctx, state, History::Drop, m_currentScenePath);
    selectByName(ctx.scene, state, priorSelection);
    restoreView(ctx.scene, state);

    state.sceneDirty = false;
    pushRecentPath(state.recentScenes, m_currentScenePath);
    return true;
}

void SceneIOController::endPlaySession(FrameContext& ctx) {
    // A session that has ended is Edit mode - paused, back at 1x whatever a
    // script scaled it to, and with the cursor a game may have grabbed handed
    // back to the editor.
    m_play.release();
    ctx.clock.setPaused(true);
    ctx.clock.setTimeScale(1.0f);
    ctx.window.setCursorMode(CursorMode::Normal);
    // The next session needs the command slots this one's actions hold.
    ctx.input.reset();
}

void SceneIOController::beginSceneReplace(FrameContext& ctx, EditorState& state) {
    m_behaviors.endSession(ctx.scene);

    const HeldImports before = heldImports(ctx);
    ctx.scene.clear();

    // The asset graph goes with the scene it belonged to; see
    // docs/reference/editor.md, "What an open does to the session's imports".
    ResourceManager replaced;
    ctx.resources.swap(replaced);

    reportDroppedImports(ctx, state, before);

    m_currentScenePath.clear();

    // Same rule as a load, one step further: the scene is empty now, so every
    // entity the history addresses is gone and nothing it holds can be applied.
    const std::string noEventPath;
    afterSceneReplace(ctx, state, History::Drop, noEventPath);
}

void SceneIOController::newScene(FrameContext& ctx, EditorState& state) {
    rememberView(state);
    beginSceneReplace(ctx, state);

    // The same seed the engine boots with when a project names no scene. Called
    // directly rather than through EditorActions: a brand-new scene carries no
    // undo entries and no selection side effects.
    buildDefaultScene(ctx.scene, ctx.resources);
    restoreView(ctx.scene, state);

    state.sceneDirty = false;
    state.pushToast(ToastKind::Info, "New scene");
}

void SceneIOController::afterSceneReplace(
    FrameContext& ctx,
    EditorState& state,
    History history,
    const std::string& eventPath
) {
    endPlaySession(ctx);
    if (history == History::Drop) state.commands.clear();

    // The previews and the pinned material are keyed by handles into the
    // outgoing graph: an open replaced it, and a Stop dropped whatever the
    // session made.
    m_materialPreviews.clear();
    state.materialEditorTarget = {};
    state.materialPinnedAt     = {};

    state.deselect();

    // A preview names an entity of the outgoing world.
    m_cameraController.lookThrough({});

    // Which camera a session renders through is findActiveCamera's choice.
    int activeCount = 0;
    ctx.scene.forEach<Camera, Transform>([&](EntityId, const Camera& c, const Transform&) {
        if (c.active) ++activeCount;
    });
    if (activeCount > 1) {
        LOG_WARNING(
            "SceneIOController: %d cameras marked active in %s - a session renders "
            "through the one in the lowest slot",
            activeCount,
            eventPath.c_str()
        );
    }
}

bool SceneIOController::captureSnapshot(FrameContext& ctx, EditorState& state) {
    // The snapshot is the scene file format, so it names assets the library has
    // to be able to hand back - an unrecorded import would come back empty. A
    // recorded one whose bake has not landed comes back from its recipe.
    const bool cooked = AssetCooker::cookAllAssets(ctx.resources, AssetCooker::Bake::InBackground);

    if (!m_play.capture(ctx.scene, ctx.resources, state.sceneDirty)) {
        LOG_ERROR("SceneIOController::captureSnapshot: failed to serialize scene");
        state.pushToast(ToastKind::Error, "Play: could not snapshot the scene, so Play is refused");
        return false;
    }
    // The session's steps address the copy Stop discards; see
    // docs/reference/editor.md, "A play session owns the scene".
    state.commands.park();
    ctx.input.reset();

    // A partial cook does not stop Play, by the same rule the save follows: the
    // session is still worth entering, and the toast names what Stop may lose.
    if (!cooked) {
        state.pushToast(ToastKind::Error, "Some assets did not cook; Stop may not restore them");
    }
    return true;
}

void SceneIOController::restoreSnapshot(FrameContext& ctx, EditorState& state) {
    if (!m_play.held()) return;

    // Read before the swap moves either: the session's own history and the
    // dirty flag are how its authored work is told from what it merely simulated.
    const bool discardingEdits = state.commands.canUndo() || state.commands.canRedo()
        || (state.sceneDirty && !m_play.dirtyAtCapture());

    const std::vector<uint32_t> priorSelection = selectedSlots(state);

    // Play stop: fire onDestroy on the played scene's running behaviors while
    // their context is still valid, before the swap restores the snapshot.
    m_behaviors.endSession(ctx.scene);

    // Assets then scene, both from the snapshot; see docs/reference/editor.md,
    // "What an open does to the session's imports".
    if (!m_play.restoreInto(ctx.scene, ctx.resources)) {
        LOG_ERROR("SceneIOController::restoreSnapshot: failed to restore play snapshot");
        state.pushToast(ToastKind::Error, "Stop: could not restore scene snapshot");
        // The snapshot is kept, so Stop can be pressed again. The asset graph
        // is already back, though: it has to be restored before the scene that
        // names it, and only a scene the editor wrote and cannot read gets here.
        return;
    }

    // Both are read out first because afterSceneReplace releases the snapshot.
    const bool dirtyAtCapture = m_play.dirtyAtCapture();
    const bool inPlace        = m_play.restoredInPlace(ctx.scene);

    // The authored history comes back when every entity is back in its slot,
    // and so does the selection; see docs/reference/editor.md, "What an open
    // does to the session's imports".
    if (inPlace) state.commands.unpark();
    afterSceneReplace(ctx, state, inPlace ? History::Keep : History::Drop, m_currentScenePath);
    if (inPlace) selectSlots(ctx.scene, state, priorSelection);
    state.sceneDirty = dirtyAtCapture;

    if (discardingEdits) {
        LOG_WARNING("SceneIOController: Stop discarded the edits made during the session");
        state.pushToast(ToastKind::Warning, "Stop discarded the edits made during play");
    } else if (!inPlace) {
        LOG_WARNING("SceneIOController: a prefab instance came back in other slots; undo history dropped");
        state.pushToast(ToastKind::Warning, "Undo history cleared: a prefab changed during play");
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
        ImGui::TextDisabled(
            "Saved into %s (.json appended automatically)",
            ProjectPaths::scenes().string().c_str()
        );
        ImGui::SetNextItemWidth(EditorStyle::px(360.0f));
        // Fed to dialogButtons as fieldCommitted: ImGui swallows the plain
        // Enter while the field is active.
        const bool enterCommit = ImGui::InputText(
            "##SaveAsName",
            m_saveAsBuffer,
            sizeof(m_saveAsBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue
        );

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
            std::filesystem::create_directories(ProjectPaths::scenes(), mkdirEc);
            // Set before the write: a failed Save-As leaves the new path
            // current, so Ctrl+S retries where the user asked to go.
            m_currentScenePath = finalPath;
            writeScene(ctx, state, m_currentScenePath);
        }
        endDialog();
    }

    // A pick is asked for rather than loaded: opening throws the live scene away,
    // so it goes through the unsaved-changes guard.
    std::string picked;
    if (m_loadPicker.draw(picked)) {
        state.requestSceneAction(EditorState::SceneAction::Open, picked);
    }
}

} // namespace Vkm::Engine
