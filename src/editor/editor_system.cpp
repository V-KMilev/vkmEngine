#include "editor_system.h"
#include "framework/editor_context.h"
#include "framework/editor_settings.h"
#include "ui/editor_theme.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include <string>

#include "system/camera/camera_controller.h"
#include "system/render/render_system.h"

namespace Engine {

EditorSystem::EditorSystem(
    GLFWwindow* window,
    CameraController& cameraController,
    VisibilitySystem& visibilitySystem,
    RenderSystem& renderSystem,
    EventSystem& events
)
    : m_window(window)
    , m_cameraController(cameraController)
    , m_renderSystem(renderSystem)
    , m_visibilitySystem(visibilitySystem)
    , m_events(events)
    , m_sceneIO(events, cameraController, renderSystem)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Floating windows (Material Editor, Preferences) move only by their
    // title bar - dragging inside the body must not drag the window, so
    // viewport orbiting on the material preview stays put.
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // ImGui ini lives next to the engine so floating-window positions and
    // table column widths persist alongside our own editor_settings.json.
    // Static so the c_str pointer stays valid for ImGui's lifetime.
    static std::string s_iniPath = std::string(APP_ROOT_DIR) + "/imgui.ini";
    io.IniFilename = s_iniPath.c_str();

    applyEditorTheme();

    // Restore persisted editor state (panel widths, toggles, snap, keybinds,
    // recent scenes). Missing/invalid file is non-fatal - defaults apply.
    EditorSettings::load(m_state);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");
}

EditorSystem::~EditorSystem() {
    EditorSettings::save(m_state);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

namespace {
    // Keep the GLFW window title in sync with the scene state: "<file> [*] - VKM Engine".
    // Updates only when something the title shows changes to avoid GLFW churn.
    void syncWindowTitle(GLFWwindow* w, const std::string& path, bool dirty) {
        static std::string s_last;
        std::string fname = "untitled";
        if (!path.empty()) {
            const size_t s = path.find_last_of("/\\");
            fname = (s == std::string::npos) ? path : path.substr(s + 1);
        }
        std::string title = fname + (dirty ? " *" : "") + " - VKM Engine";
        if (title != s_last) {
            glfwSetWindowTitle(w, title.c_str());
            s_last = std::move(title);
        }
    }
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

    syncWindowTitle(m_window, m_sceneIO.path(), m_state.sceneDirty);

    if (!m_state.editorVisible) {
        m_cameraController.setEditorInputCapture(false, false);

        // While the editor is hidden, draw a tiny corner hint so new users
        // know how to bring it back. Otherwise F5 is a one-way trap door.
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            const float pad = 10.0f;
            const ImVec2 size(180, 28);
            ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - size.x - pad,
                                           vp->WorkPos.y + vp->WorkSize.y - size.y - pad));
            ImGui::SetNextWindowSize(size);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
            ImGui::Begin("##F5Hint", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::TextDisabled("Press F5 to show editor");
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        return;
    }

    {
        bool blockMouse = (!m_state.viewportHovered && !m_cameraController.isLooking())
                       || m_gizmoOverlay.isGizmoOver()
                       || m_viewportToolbar.isHovered()
                       || m_playbar.isHovered();
        m_cameraController.setEditorInputCapture(blockMouse, ImGui::GetIO().WantTextInput);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    m_viewportOverlay.pushFrameTime(ctx.deltaTime * 1000.0f);
    m_viewportOverlay.updateMetrics(ctx.deltaTime);

    EditorContext ec{ ctx, m_state, m_cameraController, m_renderSystem,
                      m_visibilitySystem, m_events, {}, {} };

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

    // Full-viewport host: square it (theme WindowRounding=6 would round the
    // top corners and leave triangular gaps in the menu bar). Floating
    // windows still keep their rounding.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    if (ImGui::Begin("##Editor", nullptr, rootFlags)) {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);

        m_menuBar.draw(ec, m_sceneIO);
        drawWorkspace(ec);

    } else {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
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
            m_gizmoOverlay.drawLightGizmos(ec);
            m_gizmoOverlay.drawCameraGizmos(ec);
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
