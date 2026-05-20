#include "framework/scene_io_controller.h"
#include "framework/editor_state.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <string>
#include <vector>

#include "core/engine.h"
#include "core/system.h"
#include "ecs/scene.h"
#include "ecs/component/camera.h"
#include "io/scene_serializer.h"
#include "system/camera/camera_controller.h"
#include "system/event/event_system.h"

namespace Engine {

SceneIOController::SceneIOController(EventSystem* events, CameraController* cameraController)
    : m_events(events)
    , m_cameraController(cameraController)
{
    // Editor-side refresh after a scene reload.
    if (m_events) {
        m_sceneLoadedListenerId = m_events->subscribe<SceneSerializer::SceneLoadedEvent>(
            [this](const SceneSerializer::SceneLoadedEvent&) {
                if (!m_cameraController) return;
                // Re-bind the camera controller to the loaded scene's active
                // Camera; the prior handle's slot may now hold another entity.
                auto& engine = Engine::get();
                Entity rebound{};
                engine.getScene().forEach<Camera>([&](EntityId id, const Camera& c) {
                    if (c.active && !rebound.getID()) rebound = Entity{id};
                });
                m_cameraController->setCameraEntity(rebound);
            }
        );
    }
}

SceneIOController::~SceneIOController() {
    if (m_events && m_sceneLoadedListenerId != 0) {
        m_events->unsubscribe<SceneSerializer::SceneLoadedEvent>(m_sceneLoadedListenerId);
    }
}

void SceneIOController::save(FrameContext& ctx) {
    if (m_currentScenePath.empty()) {
        requestSaveAs();
        return;
    }
    SceneSerializer::save(ctx.scene, ctx.resources, m_currentScenePath);
}

void SceneIOController::requestSaveAs() {
    m_openSaveAsPopup = true;
}

void SceneIOController::requestLoad() {
    m_openLoadPopup = true;
}

void SceneIOController::load(FrameContext& ctx, EditorState& state) {
    if (m_currentScenePath.empty()) {
        m_currentScenePath = std::string(APP_ROOT_DIR) + "/scenes/scene.json";
    }

    // Stash the selection's slot index. Slot indices are stable across
    // save/load — if the same entity is in the file, it'll be at the same
    // slot with a fresh generation, and we re-validate after the load.
    const uint32_t selectedIdx =
        (state.selectedEntity && ctx.scene.isAlive(state.selectedEntity))
            ? state.selectedEntity.index : 0u;
    state.selectedEntity = {};

    if (!SceneSerializer::load(ctx.scene, ctx.resources, m_currentScenePath)) return;

    // Restore selection if its slot is still live in the loaded scene.
    if (selectedIdx != 0 && ctx.scene.isAliveAtIndex(selectedIdx)) {
        state.selectedEntity = EntityId{selectedIdx, ctx.scene.generationOf(selectedIdx)};
    }

    // Notify subscribers (camera controller rebind, etc.). The serializer
    // itself doesn't publish — it has no EventSystem reference.
    if (m_events) m_events->emit(SceneSerializer::SceneLoadedEvent{m_currentScenePath});
}

void SceneIOController::drawDialogs(FrameContext& ctx, EditorState& state) {
    if (m_openSaveAsPopup) {
        ImGui::OpenPopup("Save Scene As");
        m_openSaveAsPopup = false;
    }
    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("Saved into %s/scenes/", APP_ROOT_DIR);
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText("##SaveAsName", m_saveAsBuffer, sizeof(m_saveAsBuffer));

        if (ImGui::Button("Save", ImVec2(120, 0))) {
            m_currentScenePath = std::string(APP_ROOT_DIR) + "/scenes/" + m_saveAsBuffer;
            SceneSerializer::save(ctx.scene, ctx.resources, m_currentScenePath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (m_openLoadPopup) {
        ImGui::OpenPopup("Load Scene");
        m_openLoadPopup = false;
    }
    if (ImGui::BeginPopupModal("Load Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::filesystem::path scenesDir = std::filesystem::path(APP_ROOT_DIR) / "scenes";
        ImGui::TextDisabled("%s", scenesDir.string().c_str());

        // Collect candidates. Cheap enough to re-list each frame the popup is open.
        std::vector<std::filesystem::path> candidates;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(scenesDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                candidates.push_back(entry.path());
            }
        }
        std::sort(candidates.begin(), candidates.end());

        if (candidates.empty()) {
            ImGui::TextDisabled("(no .json files in scenes/)");
        } else {
            ImGui::BeginChild("##SceneList", ImVec2(360, 200), true);
            for (const auto& p : candidates) {
                const std::string filename = p.filename().string();
                const bool isCurrent = (p.string() == m_currentScenePath);
                if (ImGui::Selectable(filename.c_str(), isCurrent, ImGuiSelectableFlags_AllowDoubleClick)) {
                    m_currentScenePath = p.string();
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        load(ctx, state);
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::EndChild();
        }

        const bool canLoad = !m_currentScenePath.empty() && !candidates.empty();
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
