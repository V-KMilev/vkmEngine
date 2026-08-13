#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/scene_io_controller.h"
#include "ecs/environment.h"
#include "generator/light_generators.h"

#include "ui/editor_style.h"
#include "ui/editor_dialogs.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

#include <imgui.h>

#include "logger.h"

#include "core/system.h"
#include "ecs/component/camera.h"
#include "ecs/component/transform.h"
#include "ecs/component/name.h"
#include "ecs/scene.h"
#include "framework/editor_state.h"
#include "io/scene/scene_serializer.h"
#include "io/project_paths.h"
#include "cook/asset_cooker.h"
#include "system/camera/camera_controller_system.h"
#include "system/script/behavior_system.h"
#include "system/render/editor_render_hooks.h"
#include "system/render/render_system.h"

namespace Engine {

SceneIOController::SceneIOController(
    CameraControllerSystem& cameraController,
    RenderSystem& renderSystem
)
    : m_cameraController(cameraController)
    , m_renderSystem(renderSystem)
{}

SceneIOController::~SceneIOController() = default;

void SceneIOController::save(FrameContext& ctx, EditorState& state) {
    if (m_currentScenePath.empty()) {
        requestSaveAs();
        return;
    }
    // Bake every referenced asset into the cooked library + manifest, then write
    // the scene as name-only references to those cooked assets.
    AssetCooker::cookAllAssets(ctx.resources);
    if (!SceneSerializer::save(ctx.scene, ctx.resources, m_currentScenePath)) {
        LOG_ERROR("SceneIOController::save: failed to write %s - scene remains dirty",
            m_currentScenePath.c_str());
        state.pushToast(EditorState::ToastKind::Error,
            "Save failed: " + m_currentScenePath);
        return;
    }
    state.sceneDirty = false;
    pushRecent(state, m_currentScenePath);
    state.pushToast(EditorState::ToastKind::Info, "Saved");
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
    pushRecent(state, m_currentScenePath);
}

void SceneIOController::newScene(FrameContext& ctx, EditorState& state) {
    // Tear down any live behaviors before their entities vanish.
    BehaviorSystem::endSession(ctx.scene);

    ctx.scene.clear();
    ctx.scene.environment() = Environment{};
    m_currentScenePath.clear();

    afterSceneReplace(ctx, state, /*priorSelectionName*/ {}, /*eventPath*/ {});

    // Seed the minimal viable scene - an eye and a key light - directly (not
    // via EditorActions) so no undo entries or selection side effects exist
    // in a brand-new scene.
    {
        auto cam = ctx.scene.createEntity();
        Transform camTf;
        camTf.position = {0.0f, 2.0f, 6.0f};
        ctx.scene.add(cam, camTf);
        ctx.scene.add(cam, Camera{});
        Name camName{};
        snprintf(camName.value, sizeof(camName.value), "Camera");
        ctx.scene.add(cam, camName);

        auto sun = ctx.scene.createEntity();
        Transform sunTf;
        sunTf.position = {0.0f, 8.0f, 0.0f};
        ctx.scene.add(sun, sunTf);
        ctx.scene.add(sun, generateDirectionalLight());
        Name sunName{};
        snprintf(sunName.value, sizeof(sunName.value), "Sun");
        ctx.scene.add(sun, sunName);
    }

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

    // The swap replaced the ResourceManager wholesale, so preview targets
    // keyed by the old asset handles are stale. Drop them; the Material
    // Editor / Asset Browser re-bake lazily on their next draw.
    if (EditorRenderHooks* backend = editorRenderHooks(m_renderSystem.backend())) {
        backend->releaseAllPreviews();
    }

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

    // Re-bind the camera controller to the new scene's active Camera. If
    // multiple cameras claim active (authoring oversight), pick the first
    // by iteration order and warn - silently picking one of several would
    // make the choice look intentional.
    Entity rebound{};
    int activeCount = 0;
    ctx.scene.forEach<Camera>([&](EntityId id, const Camera& c) {
        if (!c.active) return;
        ++activeCount;
        if (!rebound.getID()) rebound = Entity{id};
    });
    if (activeCount > 1) {
        LOG_WARNING("SceneIOController: %d cameras marked active in %s - using the first",
            activeCount, eventPath.c_str());
    }
    m_cameraController.setCameraEntity(rebound);
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

void SceneIOController::pushRecent(EditorState& state, const std::string& path) {
    auto& mru = state.recentScenes;
    mru.erase(std::remove(mru.begin(), mru.end(), path), mru.end());
    mru.insert(mru.begin(), path);
    if (mru.size() > EditorState::MAX_RECENT_SCENES) mru.resize(EditorState::MAX_RECENT_SCENES);
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
        // Enter in the field commits (EnterReturnsTrue), matching the
        // scaffold's Enter-confirms contract.
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

        DialogResult r = dialogButtons(m_openSaveAsPopup,
                                       collides ? "Overwrite" : "Save", !empty);
        if (enterCommit && !empty && r == DialogResult::None) {
            r = DialogResult::Confirm;
            m_openSaveAsPopup = false;
            ImGui::CloseCurrentPopup();
        }
        if (r == DialogResult::Confirm) {
            // First-time save: the scenes/ directory may not exist yet.
            std::error_code mkdirEc;
            std::filesystem::create_directories(
                ProjectPaths::scenes(), mkdirEc);
            m_currentScenePath = finalPath;
            AssetCooker::cookAllAssets(ctx.resources);
            if (SceneSerializer::save(ctx.scene, ctx.resources, m_currentScenePath)) {
                state.sceneDirty = false;
                pushRecent(state, m_currentScenePath);
                state.pushToast(EditorState::ToastKind::Info, "Saved " + finalName);
            } else {
                LOG_ERROR("SceneIOController: Save As failed for %s", m_currentScenePath.c_str());
                state.pushToast(EditorState::ToastKind::Error,
                    "Save failed: " + finalName);
            }
        }
        endDialog();
    }

    // The Load flow rides the shared AssetPicker (rooted at scenes/, .json
    // filter): requestLoad() configured + opened it; here we just drive it and
    // route a pick through the same loadPath() the recent-scenes menu uses.
    std::string picked;
    if (m_loadPicker.draw(picked)) {
        requestOpenPath(ctx, state, picked);
    }
}

} // namespace Engine
