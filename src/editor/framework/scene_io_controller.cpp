#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/scene_io_controller.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <imgui.h>

#include "logger.h"

#include "core/system.h"
#include "ecs/component/camera.h"
#include "ecs/component/name.h"
#include "ecs/scene.h"
#include "framework/editor_state.h"
#include "io/scene_serializer.h"
#include "io/project_paths.h"
#include "cook/asset_cooker.h"
#include "system/camera/camera_controller.h"
#include "system/script/behavior_system.h"
#include "system/event/event_system.h"
#include "system/render/render_backend.h"
#include "system/render/render_system.h"

namespace Engine {

SceneIOController::SceneIOController(
    EventSystem& events,
    CameraController& cameraController,
    RenderSystem& renderSystem
)
    : m_events(events)
    , m_cameraController(cameraController)
    , m_renderSystem(renderSystem)
{
    // Post-load editor housekeeping (camera rebind, temporal-history
    // invalidate) lives in load() directly - no self-subscribe needed.
    // Other listeners (outside this class) still receive
    // SceneSerializer::SceneLoadedEvent when load() emits it below.
}

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
    m_openLoadPopup = true;
}

bool SceneIOController::isSaveDialogActive() const {
    // Queued (about to open this frame) or already open from a prior frame.
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
    if (RenderBackend* backend = m_renderSystem.backend()) {
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

    // Tell external subscribers (anything that cares about scene swaps) the
    // scene was replaced. The effects above used to ride this event back to
    // us via a self-subscribe; we do them directly now that we have ctx.
    m_events.emit(SceneSerializer::SceneLoadedEvent{eventPath});
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

void SceneIOController::drawDialogs(FrameContext& ctx, EditorState& state) {
    if (m_openSaveAsPopup) {
        ImGui::OpenPopup("Save Scene As");
        m_openSaveAsPopup = false;
    }
    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("Saved into %s (.json appended automatically)",
                            ProjectPaths::scenes().string().c_str());
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("##SaveAsName", m_saveAsBuffer, sizeof(m_saveAsBuffer));

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
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                "Overwrites existing %s", finalName.c_str());
        }

        ImGui::BeginDisabled(empty);
        if (ImGui::Button(collides ? "Overwrite" : "Save", ImVec2(120, 0))) {
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
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    const std::filesystem::path scenesDir = ProjectPaths::scenes();
    if (m_openLoadPopup) {
        // Refresh the cached listing once per open. The popup may stay up for
        // many frames; re-listing the directory each one is wasted work.
        m_loadCandidates.clear();
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(scenesDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                m_loadCandidates.push_back(entry.path().string());
            }
        }
        std::sort(m_loadCandidates.begin(), m_loadCandidates.end());

        ImGui::OpenPopup("Load Scene");
        m_openLoadPopup = false;
    }
    if (ImGui::BeginPopupModal("Load Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("%s", scenesDir.string().c_str());

        if (m_loadCandidates.empty()) {
            ImGui::TextDisabled("(no .json files in scenes/)");
        } else {
            ImGui::BeginChild("##SceneList", ImVec2(360, 200), true);
            for (const auto& p : m_loadCandidates) {
                const std::string filename = std::filesystem::path(p).filename().string();
                const bool isCurrent = (p == m_currentScenePath);
                if (ImGui::Selectable(filename.c_str(), isCurrent, ImGuiSelectableFlags_AllowDoubleClick)) {
                    m_currentScenePath = p;
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        load(ctx, state);
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::EndChild();
        }

        const bool canLoad = !m_currentScenePath.empty() && !m_loadCandidates.empty();
        ImGui::BeginDisabled(!canLoad);
        if (ImGui::Button("Load", ImVec2(120, 0))) {
            load(ctx, state);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace Engine
