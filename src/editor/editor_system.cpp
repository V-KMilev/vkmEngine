#include "editor_system.h"
#include "framework/editor_context.h"
#include "ui/editor_widgets.h"
#include "ui/editor_theme.h"
#include "input/editor_actions.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "core/engine.h"
#include "debug/statistics.h"
#include "platform/window/window_manager.h"
#include "ecs/scene.h"
#include "ecs/component/transform.h"
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
    , m_visibilitySystem(visibilitySystem)
    , m_events(events)
    , m_sceneIO(events, cameraController)
{
    m_panels = { &m_hierarchy, &m_inspector, &m_bottom, &m_preferences };

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    applyEditorTheme();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");
}

EditorSystem::~EditorSystem() {
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

    m_viewportOverlay.pushFrameTime(ctx.deltaTime * 1000.0f);
    m_viewportOverlay.updateMetrics(ctx.deltaTime);

    EditorContext ec{ ctx, m_state, m_cameraController, m_renderSystem,
                      m_visibilitySystem, m_events };

    if (!ImGui::GetIO().WantTextInput) {
        const auto& kb = m_state.keybinds;

        if (isPressed(kb.toggleStats))     m_state.showStats     = !m_state.showStats;
        if (isPressed(kb.toggleHierarchy)) m_state.showHierarchy = !m_state.showHierarchy;
        if (isPressed(kb.toggleInspector)) m_state.showInspector = !m_state.showInspector;
        if (isPressed(kb.toggleBottom))    m_state.showBottom    = !m_state.showBottom;
        if (isPressed(kb.toggleEditor))    m_state.editorVisible = false;
        if (isPressed(kb.openPreferences)) m_state.showPreferences = !m_state.showPreferences;

        if (isPressed(kb.saveSceneAs))     m_sceneIO.requestSaveAs();
        else if (isPressed(kb.saveScene))  m_sceneIO.save(ctx);
        if (isPressed(kb.loadScene))       m_sceneIO.requestLoad();

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

        m_menuBar.draw(ec, m_sceneIO);

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
                m_hierarchy.draw(ec);
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
                ec.viewportPos  = vpMin;
                ec.viewportSize = ImVec2(centerW, mainH);
                m_state.viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
                if (m_state.showStats) m_viewportOverlay.draw(ec);
                m_viewportOverlay.drawNavigationGizmo(ec);
                m_gizmoOverlay.drawTransformGizmo(ec);
                m_viewportToolbar.draw(ec);
                m_playbar.draw(ec);
                if (!m_viewportToolbar.isHovered() && !m_playbar.isHovered())
                    m_gizmoOverlay.handleViewportPick(ec);
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
                m_inspector.draw(ec);
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        if (m_state.showBottom) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
            if (ImGui::BeginChild("##Bottom", ImVec2(0, bottomH), ImGuiChildFlags_Borders)) {
                m_bottom.draw(ec);
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

    // Separate floating window; drawn after the root so it stacks on top.
    if (m_state.showPreferences) {
        m_preferences.draw(ec);
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (m_renderSystem && m_state.wireframe) m_renderSystem->setWireframe(true);
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

} // namespace Engine
