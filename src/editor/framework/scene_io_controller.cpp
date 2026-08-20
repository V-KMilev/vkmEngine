#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/scene_io_controller.h"

#include <filesystem>
#include <string>
#include <system_error>

#include <imgui.h>

#include "logger.h"

#include "core/clock.h"
#include "core/system.h"
#include "ecs/component/camera.h"
#include "ecs/component/transform.h"
#include "ecs/component/name.h"
#include "ecs/scene.h"
#include "framework/editor_state.h"
#include "framework/material_preview_session.h"
#include "io/scene/scene_serializer.h"
#include "io/project_paths.h"
#include "cook/asset_cooker.h"
#include "generator/default_scene.h"
#include "generator/light_generators.h"
#include "system/camera/camera_controller_system.h"
#include "system/script/behavior_system.h"
#include "ui/editor_style.h"
#include "ui/editor_dialogs.h"

namespace Vkm::Engine {

SceneIOController::SceneIOController(
    CameraControllerSystem& cameraController,
    MaterialPreviewSession& materialPreviews
)
    : m_cameraController(cameraController)
    , m_materialPreviews(materialPreviews)
{}

SceneIOController::~SceneIOController() = default;

bool SceneIOController::writeScene(FrameContext& ctx, EditorState& state, const std::string& path) {
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

void SceneIOController::save(FrameContext& ctx, EditorState& state) {
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

    if (!SceneSerializer::load(ctx.scene, ctx.resources, m_currentScenePath)) {
        LOG_ERROR("SceneIOController::load: failed to load %s - editor state preserved",
            m_currentScenePath.c_str());
        state.pushToast(EditorState::ToastKind::Error,
            "Load failed: " + m_currentScenePath);
        return;
    }

    afterSceneReplace(ctx, state, priorSelectionName, m_currentScenePath);

    state.sceneDirty = false;
    pushRecentPath(state.recentScenes, m_currentScenePath);
}

void SceneIOController::beginSceneReplace(FrameContext& ctx, EditorState& state) {
    // Tear down any live behaviors before their entities vanish - and while the
    // module holding their code is still loaded.
    BehaviorSystem::endSession(ctx.scene);

    ctx.scene.clear();
    m_currentScenePath.clear();

    // The play snapshot is a copy of the scene going away. Left behind, the
    // transport still reads as playing and Stop would restore the outgoing
    // scene over whatever replaced it. Pausing goes with it: dropping the
    // snapshot ends the session, and a session that has ended is Edit mode.
    m_playSnapshot.clear();
    ctx.clock.setPaused(true);

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
    // Entity IDs don't carry across scenes - any pending undo would
    // operate on slots that now hold unrelated entities.
    state.commands.clear();

    // Same for the Hierarchy panel's cached root list. It otherwise only
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
    m_playSnapshot = SceneSerializer::saveToString(ctx.scene, ctx.resources);
    if (m_playSnapshot.empty()) {
        LOG_ERROR("SceneIOController::captureSnapshot: failed to serialize scene");
        state.pushToast(EditorState::ToastKind::Error,
            "Play: could not snapshot scene (Stop will not restore)");
        return;
    }
    // Remember the dirty flag so Stop leaves it exactly as the user left it -
    // simulation mutates the ECS directly (not via editor commands), so it
    // never dirties the scene on its own.
    m_playSnapshotDirty = state.sceneDirty;
}

void SceneIOController::restoreSnapshot(FrameContext& ctx, EditorState& state) {
    if (m_playSnapshot.empty()) return;

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

    m_playSnapshot.clear();
    afterSceneReplace(ctx, state, priorSelectionName, m_currentScenePath);
    state.sceneDirty = m_playSnapshotDirty;
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
