#include "framework/scene_io_controller.h"
#include "framework/editor_state.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <string>
#include <vector>

#include "core/system.h"
#include "ecs/scene.h"
#include "ecs/component/camera.h"
#include "io/scene_serializer.h"
#include "system/camera/camera_controller.h"
#include "system/event/event_system.h"
#include "system/render/render_system.h"

namespace Engine {

SceneIOController::SceneIOController(EventSystem* events, CameraController* cameraController,
                                     RenderSystem* renderSystem)
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

    // Re-bind the camera controller to the new scene's active Camera -
    // the prior handle's slot may now hold a different entity (or be empty).
    if (m_cameraController) {
        Entity rebound{};
        ctx.scene.forEach<Camera>([&](EntityId id, const Camera& c) {
            if (c.active && !rebound.getID()) rebound = Entity{id};
        });
        m_cameraController->setCameraEntity(rebound);
    }

    // TAA / other reprojection-based post effects must drop their
    // history - sampling the old scene's history over the new view
    // smears for several frames after the swap.
    if (m_renderSystem) m_renderSystem->invalidateTemporalHistory();

    // Tell external subscribers (anything that cares about scene swaps)
    // the load happened. The two effects above used to ride this event
    // back to us via a self-subscribe; that needed Engine::get() to fetch
    // the Scene, so we just do them directly here now that we have ctx.
    if (m_events) m_events->emit(SceneSerializer::SceneLoadedEvent{m_currentScenePath});
}

void SceneIOController::drawDialogs(FrameContext& ctx, EditorState& state) {
    if (m_openSaveAsPopup) {
        ImGui::OpenPopup("Save Scene As");
        m_openSaveAsPopup = false;
    }
    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("Saved into %s/scenes/ (.json appended automatically)", APP_ROOT_DIR);
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
        const std::string finalPath = std::string(APP_ROOT_DIR) + "/scenes/" + finalName;
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
            m_currentScenePath = finalPath;
            SceneSerializer::save(ctx.scene, ctx.resources, m_currentScenePath);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    const std::filesystem::path scenesDir = std::filesystem::path(APP_ROOT_DIR) / "scenes";
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
