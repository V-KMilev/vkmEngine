#include "editor_system.h"
#include "editor_widgets.h"
#include "editor_theme.h"
#include "editor_actions.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <vector>

#include "core/engine.h"
#include "debug/statistics.h"
#include "platform/window/window_manager.h"
#include "ecs/scene.h"
#include "ecs/component/transform.h"
#include "ecs/component/animation.h"
#include "ecs/component/camera.h"
#include "io/scene_serializer.h"
#include "system/camera/camera_controller.h"
#include "system/event/event_system.h"
#include "system/render/render_system.h"

namespace Engine {

EditorSystem::EditorSystem(
    GLFWwindow* window,
    CameraController* cameraController,
    VisibilitySystem* visibilitySystem,
    RenderSystem* renderSystem,
    EventSystem* events
)
    : m_window(window)
    , m_cameraController(cameraController)
    , m_renderSystem(renderSystem)
    , m_events(events)
    , m_bottom(visibilitySystem, renderSystem)
    , m_preferences(cameraController)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    applyEditorTheme();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    // Anything the editor needs to refresh after a scene reload (camera
    // controller binding today; potentially more in the future) reacts to
    // SceneLoadedEvent rather than being bolted onto the loadScene handler.
    if (m_events) {
        m_sceneLoadedListenerId = m_events->subscribe<SceneSerializer::SceneLoadedEvent>(
            [this](const SceneSerializer::SceneLoadedEvent&) {
                if (!m_cameraController) return;
                // Re-bind the camera controller to whatever active Camera
                // the loaded scene has. The previous handle pointed into a
                // slot that may now hold a different entity (post-load).
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

EditorSystem::~EditorSystem() {
    if (m_events && m_sceneLoadedListenerId != 0) {
        m_events->unsubscribe<SceneSerializer::SceneLoadedEvent>(m_sceneLoadedListenerId);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void EditorSystem::update(FrameContext& ctx) {
    if (!m_state.editorVisible) {
        int f5 = glfwGetKey(m_window, GLFW_KEY_F5);
        if (f5 == GLFW_PRESS && !m_f5WasDown) m_state.editorVisible = true;
        m_f5WasDown = (f5 == GLFW_PRESS);
        if (m_cameraController) m_cameraController->setEditorInputCapture(false, false);
        return;
    }

    if (m_cameraController) {
        bool blockMouse = (!m_state.viewportHovered && !m_cameraController->isLooking())
                       || m_gizmoOverlay.isGizmoOver()
                       || m_viewportToolbar.isHovered()
                       || m_playbar.isHovered();
        m_cameraController->setEditorInputCapture(blockMouse, ImGui::GetIO().WantTextInput);
    }

    if (m_renderSystem) m_renderSystem->setWireframe(false);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    m_viewportOverlay.m_frameTimeHistory[m_viewportOverlay.m_frameTimeOffset] = ctx.deltaTime * 1000.0f;
    m_viewportOverlay.m_frameTimeOffset = (m_viewportOverlay.m_frameTimeOffset + 1) % ViewportOverlay::FRAME_HISTORY_SIZE;
    m_viewportOverlay.updateMetrics(ctx.deltaTime);

    if (!ImGui::GetIO().WantTextInput) {
        const auto& kb = m_state.keybinds;

        if (isPressed(kb.toggleStats))     m_state.showStats     = !m_state.showStats;
        if (isPressed(kb.toggleHierarchy)) m_state.showHierarchy = !m_state.showHierarchy;
        if (isPressed(kb.toggleInspector)) m_state.showInspector = !m_state.showInspector;
        if (isPressed(kb.toggleBottom))    m_state.showBottom    = !m_state.showBottom;
        if (isPressed(kb.toggleEditor))    m_state.editorVisible = false;
        if (isPressed(kb.openPreferences)) m_state.showPreferences = !m_state.showPreferences;

        if (isPressed(kb.saveSceneAs))     openSaveAsPrompt();
        else if (isPressed(kb.saveScene))  saveScene(ctx);
        if (isPressed(kb.loadScene))       openLoadScenePrompt();

        if (isPressed(kb.deleteEntity) && m_state.selectedEntity && ctx.scene.isAlive(m_state.selectedEntity)) {
            EditorActions::deleteEntity(ctx.scene, m_state, m_state.selectedEntity);
        }
        if (isPressed(kb.deselect)) {
            m_state.selectedEntity = {};
        }
        if (isPressed(kb.duplicate) && m_state.selectedEntity && ctx.scene.isAlive(m_state.selectedEntity)) {
            EditorActions::duplicateEntity(ctx.scene, m_state, m_state.selectedEntity);
        }
        if (isPressed(kb.focusSelected) && m_state.selectedEntity && ctx.scene.isAlive(m_state.selectedEntity)) {
            EditorActions::focusOnSelected(ctx, m_state, m_cameraController);
        }

        // Gizmo mode shortcuts (only when camera NOT in fly mode)
        if (!m_cameraController->isLooking()) {
            if (isPressed(kb.gizmoSelect))      m_state.gizmoOperation = GizmoOperation::Select;
            if (isPressed(kb.gizmoTranslate))   m_state.gizmoOperation = GizmoOperation::Translate;
            if (isPressed(kb.gizmoRotate))      m_state.gizmoOperation = GizmoOperation::Rotate;
            if (isPressed(kb.gizmoScale))       m_state.gizmoOperation = GizmoOperation::Scale;
            if (isPressed(kb.gizmoToggleSpace)) {
                m_state.gizmoMode = (m_state.gizmoMode == GizmoMode::Local) ? GizmoMode::World : GizmoMode::Local;
            }
        }
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoDecoration
                               | ImGuiWindowFlags_NoMove
                               | ImGuiWindowFlags_NoResize
                               | ImGuiWindowFlags_NoBringToFrontOnFocus
                               | ImGuiWindowFlags_NoSavedSettings
                               | ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    if (ImGui::Begin("##Editor", nullptr, rootFlags)) {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        drawMenuBar(ctx);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        float toolbarH = ImGui::GetCursorPosY();
        float statusBarH = ImGui::GetFrameHeight() + 4;
        float bottomH = m_state.showBottom ? m_state.bottomPanelHeight : 0.0f;
        float mainH = viewport->WorkSize.y - toolbarH - statusBarH - bottomH;

        float leftW  = m_state.showHierarchy ? m_state.leftPanelWidth : 0.0f;
        float rightW = m_state.showInspector ? m_state.rightPanelWidth : 0.0f;

        // Track panel edge positions for border-less resize detection
        ImVec2 panelAreaStart = ImGui::GetCursorScreenPos();

        if (m_state.showHierarchy) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
            if (ImGui::BeginChild("##Hierarchy", ImVec2(leftW, mainH), ImGuiChildFlags_Borders)) {
                m_hierarchy.draw(ctx, m_state);
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::SameLine(0, 0);
        }

        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
            float centerW = viewport->WorkSize.x - leftW - rightW;
            ImVec2 vpMin = ImGui::GetCursorScreenPos();
            if (ImGui::BeginChild("##Viewport", ImVec2(centerW, mainH), ImGuiChildFlags_None)) {
                ImVec2 vpMax = ImVec2(vpMin.x + centerW, vpMin.y + mainH);
                m_state.viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
                if (m_state.showStats) m_viewportOverlay.draw(ctx, m_state);
                m_viewportOverlay.drawNavigationGizmo(ctx, vpMin, vpMax);
                m_gizmoOverlay.drawTransformGizmo(ctx, m_state, vpMin, centerW, mainH);
                m_viewportToolbar.draw(ctx, m_state, m_cameraController);
                m_playbar.draw(ctx);
                if (!m_viewportToolbar.isHovered() && !m_playbar.isHovered())
                    m_gizmoOverlay.handleViewportPick(ctx, m_state, vpMin, centerW, mainH);
            } else {
                m_state.viewportHovered = false;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 0);
        }

        if (m_state.showInspector) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
            if (ImGui::BeginChild("##Inspector", ImVec2(rightW, mainH), ImGuiChildFlags_Borders)) {
                m_inspector.draw(ctx, m_state);
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        if (m_state.showBottom) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
            if (ImGui::BeginChild("##Bottom", ImVec2(0, bottomH), ImGuiChildFlags_Borders)) {
                m_bottom.draw(ctx, m_state);
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        // Panel border resize detection (no extra layout space needed)
        // Only allow starting a resize when nothing else is being dragged.
        {
            constexpr float RESIZE_ZONE = 4.0f;
            ImVec2 mpos = ImGui::GetMousePos();
            bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            bool alreadyResizing = m_resizingLeft || m_resizingRight || m_resizingBottom;
            // Block new resize if an ImGui widget or gizmo is active
            bool canStartNew = !ImGui::IsAnyItemActive() && !m_gizmoOverlay.isGizmoUsing() && !alreadyResizing;

            auto handleEdge = [&](bool show, float edgePos, bool horizontal,
                                  bool& resizingFlag, float& panelSize, float sign,
                                  float minSize, float maxSize) {
                if (!show) return;
                bool nearEdge;
                if (horizontal) {
                    nearEdge = std::abs(mpos.y - edgePos) <= RESIZE_ZONE
                            && mpos.x >= panelAreaStart.x
                            && mpos.x <= panelAreaStart.x + viewport->WorkSize.x;
                } else {
                    nearEdge = std::abs(mpos.x - edgePos) <= RESIZE_ZONE
                            && mpos.y >= panelAreaStart.y
                            && mpos.y <= panelAreaStart.y + mainH;
                }

                // Continue an existing resize drag
                if (resizingFlag) {
                    ImGui::SetMouseCursor(horizontal ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
                    float d = horizontal ? delta.y : delta.x;
                    panelSize += d * sign;
                    panelSize = std::clamp(panelSize, minSize, maxSize);
                    return;
                }

                // Show cursor hint when hovering (even if can't start)
                if (nearEdge) {
                    ImGui::SetMouseCursor(horizontal ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
                }

                // Only start a new resize on click when nothing else is active
                if (nearEdge && canStartNew && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    resizingFlag = true;
                }
            };

            handleEdge(m_state.showHierarchy, panelAreaStart.x + leftW, false,
                       m_resizingLeft, m_state.leftPanelWidth, 1.0f, 180.0f, 500.0f);
            handleEdge(m_state.showInspector, panelAreaStart.x + viewport->WorkSize.x - rightW, false,
                       m_resizingRight, m_state.rightPanelWidth, -1.0f, 240.0f, 600.0f);
            handleEdge(m_state.showBottom, panelAreaStart.y + mainH, true,
                       m_resizingBottom, m_state.bottomPanelHeight, -1.0f, 100.0f, 500.0f);

            if (!mouseDown) {
                m_resizingLeft = m_resizingRight = m_resizingBottom = false;
            }
        }

        ImGui::PopStyleVar(); // ItemSpacing

        drawStatusBar(ctx);

    } else {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
    ImGui::End();

    // Preferences is a separate floating window, not part of the docked
    // editor layout. Drawn after the root window so it stacks on top.
    if (m_state.showPreferences) {
        m_preferences.draw(&m_state.showPreferences, ctx, m_state);
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (m_renderSystem && m_state.wireframe) m_renderSystem->setWireframe(true);
}

void EditorSystem::drawMenuBar(FrameContext& ctx) {
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        char lbl[48];
        const bool haveCurrent = !m_currentScenePath.empty();
        if (ImGui::MenuItem("Save Scene",   getKeyBindLabel(m_state.keybinds.saveScene, lbl, sizeof(lbl)), false, haveCurrent || true)) {
            saveScene(ctx);
        }
        if (ImGui::MenuItem("Save Scene As...", getKeyBindLabel(m_state.keybinds.saveSceneAs, lbl, sizeof(lbl)))) {
            openSaveAsPrompt();
        }
        if (ImGui::MenuItem("Load Scene...", getKeyBindLabel(m_state.keybinds.loadScene, lbl, sizeof(lbl)))) {
            openLoadScenePrompt();
        }
        if (haveCurrent) {
            ImGui::Separator();
            ImGui::TextDisabled("Current: %s", m_currentScenePath.c_str());
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        char lbl[48];
        if (ImGui::MenuItem("Preferences...",
                getKeyBindLabel(m_state.keybinds.openPreferences, lbl, sizeof(lbl)),
                m_state.showPreferences)) {
            m_state.showPreferences = !m_state.showPreferences;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        char lbl[48];
        ImGui::MenuItem("Stats Overlay", getKeyBindLabel(m_state.keybinds.toggleStats, lbl, sizeof(lbl)), &m_state.showStats);
        ImGui::MenuItem("Hierarchy",     getKeyBindLabel(m_state.keybinds.toggleHierarchy, lbl, sizeof(lbl)), &m_state.showHierarchy);
        ImGui::MenuItem("Inspector",     getKeyBindLabel(m_state.keybinds.toggleInspector, lbl, sizeof(lbl)), &m_state.showInspector);
        ImGui::MenuItem("Bottom Panel",  getKeyBindLabel(m_state.keybinds.toggleBottom, lbl, sizeof(lbl)), &m_state.showBottom);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Scene")) {
        EditorActions::drawCreateEntityMenu(ctx.scene, ctx.resources, m_state);
        ImGui::Separator();
        char deselectLbl[48];
        if (ImGui::MenuItem("Deselect", getKeyBindLabel(m_state.keybinds.deselect, deselectLbl, sizeof(deselectLbl)))) {
            m_state.selectedEntity = {};
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About")) ImGui::OpenPopup("##About");
        ImGui::EndMenu();
    }

    if (ImGui::BeginPopup("##About")) {
        ImGui::Text("%s  v%s", APP_NAME, APP_VERSION);
        ImGui::Separator();
        ImGui::TextDisabled("Branch:");   ImGui::SameLine(); ImGui::Text("%s", APP_BRANCH);
        ImGui::TextDisabled("Commit:");   ImGui::SameLine(); ImGui::Text("%.8s", APP_COMMIT_HASH);
        ImGui::TextDisabled("Built:");    ImGui::SameLine(); ImGui::Text("%s", APP_BUILD_DATE);
        ImGui::TextDisabled("OpenGL:");   ImGui::SameLine(); ImGui::Text("%s", (const char*)glGetString(GL_VERSION));
        ImGui::TextDisabled("Renderer:"); ImGui::SameLine(); ImGui::Text("%s", (const char*)glGetString(GL_RENDERER));
        ImGui::TextDisabled("ImGui:");    ImGui::SameLine(); ImGui::Text("%s", IMGUI_VERSION);
        ImGui::EndPopup();
    }

    // ---- Save As modal -----------------------------------------------------
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

    // ---- Load Scene picker -------------------------------------------------
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
                        loadScene(ctx);
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::EndChild();
        }

        const bool canLoad = !m_currentScenePath.empty() && !candidates.empty();
        ImGui::BeginDisabled(!canLoad);
        if (ImGui::Button("Load", ImVec2(120, 0))) {
            loadScene(ctx);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    const auto& info = ctx.statistics.getFrameInfo();
    char fps[32];
    snprintf(fps, sizeof(fps), "%.0f FPS", info.frameRateInfo.frameRate);
    float fpsW = ImGui::CalcTextSize(fps).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - fpsW - 16.0f);
    float rate = info.frameRateInfo.frameRate;
    ImVec4 fpsColor = rate >= 60 ? ImVec4(0.4f, 0.8f, 0.4f, 1.0f) :
                      rate >= 30 ? ImVec4(0.9f, 0.8f, 0.3f, 1.0f) :
                                   ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, fpsColor);
    ImGui::TextUnformatted(fps);
    ImGui::PopStyleColor();

    ImGui::EndMenuBar();
}

void EditorSystem::drawStatusBar(const FrameContext& ctx) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.11f, 1.0f));

    if (ImGui::BeginChild("##Status", ImVec2(0, 0), ImGuiChildFlags_None)) {
        ImGui::SetCursorPosX(8);
        ImGui::AlignTextToFramePadding();

        if (m_state.selectedEntity && ctx.scene.isAlive(m_state.selectedEntity)) {
            ImGui::TextDisabled("Selected:");
            ImGui::SameLine(0, 4);
            char selName[64];
            getEntityDisplayName(ctx.scene, m_state.selectedEntity, selName, sizeof(selName));
            ImGui::Text("%s", selName);

            if (ctx.scene.has<Transform>(m_state.selectedEntity)) {
                const auto& pos = ctx.scene.get<Transform>(m_state.selectedEntity).position;
                ImGui::SameLine(0, 16);
                ImGui::TextDisabled("Pos: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
            }
        } else {
            ImGui::TextDisabled("No selection");
        }

        char right[128];
        snprintf(right, sizeof(right), "%s v%s | %s | %.8s",
                 APP_NAME, APP_VERSION, APP_BRANCH, APP_COMMIT_HASH);
        float rw = ImGui::CalcTextSize(right).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - rw - 16);
        ImGui::TextDisabled("%s", right);
    }
    ImGui::EndChild();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void EditorSystem::saveScene(FrameContext& ctx) {
    if (m_currentScenePath.empty()) {
        openSaveAsPrompt();
        return;
    }
    SceneSerializer::save(ctx.scene, ctx.resources, m_currentScenePath);
}

void EditorSystem::openSaveAsPrompt() {
    m_openSaveAsPopup = true;
}

void EditorSystem::openLoadScenePrompt() {
    m_openLoadPopup = true;
}

void EditorSystem::loadScene(FrameContext& ctx) {
    if (m_currentScenePath.empty()) {
        m_currentScenePath = std::string(APP_ROOT_DIR) + "/scenes/scene.json";
    }

    // Stash the selection's slot index. Slot indices are stable across
    // save/load — if the same entity is in the file, it'll be at the same
    // slot with a fresh generation, and we re-validate after the load.
    const uint32_t selectedIdx =
        (m_state.selectedEntity && ctx.scene.isAlive(m_state.selectedEntity))
            ? m_state.selectedEntity.index : 0u;
    m_state.selectedEntity = {};

    if (!SceneSerializer::load(ctx.scene, ctx.resources, m_currentScenePath)) return;

    // Restore selection if its slot is still live in the loaded scene.
    if (selectedIdx != 0 && ctx.scene.isAliveAtIndex(selectedIdx)) {
        m_state.selectedEntity = EntityId{selectedIdx, ctx.scene.generationOf(selectedIdx)};
    }

    // Notify subscribers (camera controller rebind, etc.). The serializer
    // itself doesn't publish — it has no EventSystem reference.
    if (m_events) m_events->emit(SceneSerializer::SceneLoadedEvent{m_currentScenePath});
}

} // namespace Engine
