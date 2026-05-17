#include "editor_system.h"
#include "framework/editor_context.h"
#include "ui/editor_theme.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include "system/camera/camera_controller.h"
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
    m_panels = { &m_hierarchy, &m_inspector, &m_bottom, &m_preferences,
                 &m_materialEditor, &m_assetBrowser };

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Floating windows (Material Editor, Preferences) move only by their
    // title bar - dragging inside the body must not drag the window, so
    // viewport orbiting on the material preview stays put.
    io.ConfigWindowsMoveFromTitleBarOnly = true;

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
    // F5 toggles the whole editor UI, both directions, owned here with one
    // raw-GLFW edge detector. It cannot live in the shortcut path: while the
    // editor is hidden there is no ImGui frame to poll, so a split hide/show
    // across two input systems let one physical press fire twice (hide, then
    // instantly re-show on the next frame).
    const bool f5Down = glfwGetKey(m_window, GLFW_KEY_F5) == GLFW_PRESS;
    if (f5Down && !m_f5WasDown) m_state.editorVisible = !m_state.editorVisible;
    m_f5WasDown = f5Down;

    if (!m_state.editorVisible) {
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

    m_shortcuts.process(ec, m_sceneIO);

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
        drawWorkspace(ec);

    } else {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
    ImGui::End();

    // Separate floating window; drawn after the root so it stacks on top.
    if (m_state.showPreferences) {
        m_preferences.draw(ec);
    }
    if (m_state.showMaterialEditor) {
        m_materialEditor.draw(ec);
    }
    if (m_state.showAssetBrowser) {
        m_assetBrowser.draw(ec);
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (m_renderSystem) m_renderSystem->setWireframe(m_state.wireframe);
}

void EditorSystem::drawWorkspace(EditorContext& ec) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

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

    EditorPanelResize::Layout L;
    L.areaStart  = panelAreaStart;
    L.mainH      = mainH;
    L.workW      = viewport->WorkSize.x;
    L.leftW      = leftW;
    L.rightW     = rightW;
    L.showLeft   = m_state.showHierarchy;
    L.showRight  = m_state.showInspector;
    L.showBottom = m_state.showBottom;
    m_panelResize.process(m_state, L, m_gizmoOverlay.isGizmoUsing());

    ImGui::PopStyleVar(); // ItemSpacing

    m_statusBar.draw(ec);
}

} // namespace Engine
