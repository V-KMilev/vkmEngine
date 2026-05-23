#include "editor_system.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include "debug/profiler.h"
#include "framework/editor_context.h"
#include "framework/editor_settings.h"
#include "input/editor_keybinds.h"
#include "platform/window/window_manager.h"
#include "system/camera/camera_controller.h"
#include "system/render/render_system.h"
#include "ui/editor_theme.h"

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

    // Drop recent-scene entries whose files no longer exist. Without this
    // the Open Recent menu accumulates dead links across sessions.
    m_state.recentScenes.erase(
        std::remove_if(m_state.recentScenes.begin(), m_state.recentScenes.end(),
            [](const std::string& p) {
                std::error_code ec;
                return !std::filesystem::exists(p, ec);
            }),
        m_state.recentScenes.end());

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
    void drawToast(EditorState& state, float deltaTime) {
        if (state.toastTimeRemaining <= 0.0f) return;
        state.toastTimeRemaining -= deltaTime;
        if (state.toastTimeRemaining <= 0.0f) {
            state.toastTimeRemaining = 0.0f;
            return;
        }

        // Fade the last 0.4s so the toast doesn't pop out.
        const float alpha = std::min(1.0f, state.toastTimeRemaining / 0.4f);

        ImVec4 bg;
        switch (state.toastKind) {
            case EditorState::ToastKind::Error:   bg = ImVec4(0.55f, 0.18f, 0.18f, 0.95f * alpha); break;
            case EditorState::ToastKind::Warning: bg = ImVec4(0.55f, 0.42f, 0.10f, 0.95f * alpha); break;
            default:                              bg = ImVec4(0.16f, 0.16f, 0.19f, 0.95f * alpha); break;
        }

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float pad = 12.0f;
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + pad,
                                        vp->WorkPos.y + vp->WorkSize.y - pad),
                                ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
        ImGui::PushStyleColor(ImGuiCol_Text,     ImVec4(1.0f, 1.0f, 1.0f, alpha));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::Begin("##Toast", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoInputs     | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextUnformatted(state.toastMessage.c_str());
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    // Keep the window title in sync with the scene state: "<file> [*] - VKM Engine".
    // Updates only when the title content actually changes to avoid OS churn.
    void syncWindowTitle(WindowManager& window, const std::string& path, bool dirty) {
        static std::string s_last;
        std::string fname = "untitled";
        if (!path.empty()) {
            const size_t s = path.find_last_of("/\\");
            fname = (s == std::string::npos) ? path : path.substr(s + 1);
        }
        std::string title = fname + (dirty ? " *" : "") + " - VKM Engine";
        if (title != s_last) {
            window.setTitle(title);
            s_last = std::move(title);
        }
    }
}

void EditorSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("EditorSystem");
    syncWindowTitle(ctx.window, m_sceneIO.path(), m_state.sceneDirty);

    // Intercept window-close while the scene is dirty: clear shouldClose,
    // open the save-on-quit modal next frame. A clean scene closes through
    // normally. The modal lives in the ImGui frame below so it works in
    // both visible and hidden editor states.
    if (ctx.window.wantsClose() && m_state.sceneDirty
            && !m_state.confirmingQuit) {
        ctx.window.cancelClose();
        m_state.confirmingQuit = true;
    }

    // After a "Save" choice in the modal, three outcomes are possible:
    //   (a) Save was synchronous (path set) -> sceneDirty drops to false -> close now.
    //   (b) Save-As opened, user picks a name -> sceneDirty drops later -> close then.
    //   (c) Save-As opened, user cancels -> no save dialog active, scene still
    //       dirty -> user changed their mind, drop the intent.
    if (m_state.closeAfterSave) {
        if (!m_state.sceneDirty) {
            ctx.window.requestClose();
            m_state.closeAfterSave = false;
        } else if (!m_sceneIO.isSaveDialogActive()) {
            m_state.closeAfterSave = false;
        }
    }

    // Begin the ImGui frame before *anything* else: the editor-toggle
    // keybind (default F5) is processed here so the rebind UI in
    // Preferences actually drives it. We do the same toggle in both the
    // hidden and visible branches because the ImGui frame exists in both.
    {
        PROFILE_SCOPE("Editor/ImGuiNewFrame");
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    if (isPressed(m_state.keybinds.toggleEditor)) {
        m_state.editorVisible = !m_state.editorVisible;
        // Releasing input capture immediately on hide stops a held drag
        // from continuing while the editor isn't drawing.
        if (!m_state.editorVisible) m_panelResize.resetDragState();
    }

    // Save-on-quit modal: top-priority, drawn before anything else so it's
    // visible whether the editor is shown or hidden. Reachable only via
    // the close-intercept above.
    if (m_state.confirmingQuit) {
        ImGui::OpenPopup("Unsaved Changes");
    }
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted("This scene has unsaved changes.");
        ImGui::Spacing();
        ImGui::TextDisabled("%s", m_sceneIO.path().empty()
            ? "(untitled scene)" : m_sceneIO.path().c_str());
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(110, 0))) {
            // save() opens Save-As if there's no current path; closeAfterSave
            // defers the actual window-close until sceneDirty drops to false.
            m_sceneIO.save(ctx, m_state);
            m_state.closeAfterSave = true;
            m_state.confirmingQuit = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(110, 0))) {
            ctx.window.requestClose();
            m_state.confirmingQuit = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0))) {
            m_state.confirmingQuit = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Toast renders in both visible/hidden paths - failure feedback should
    // not vanish just because F5 was pressed.
    drawToast(m_state, ctx.deltaTime);

    if (!m_state.editorVisible) {
        m_cameraController.setEditorInputCapture(false, false);

        // No panels to layout this frame - let the 3D pipeline fill the
        // whole window next frame, not the stale viewport sub-rect.
        ctx.window.setSceneViewport(0, 0,
            static_cast<uint32_t>(ctx.window.getWidth()),
            static_cast<uint32_t>(ctx.window.getHeight()));

        // While the editor is hidden, draw a tiny corner hint so new users
        // know how to bring it back. Otherwise F5 is a one-way trap door.
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

        PROFILE_SCOPE("Editor/Panels");
        m_menuBar.draw(ec, m_sceneIO);
        // ModelImportDialog is owned here (not in the menu bar) so it
        // serves all three import-intent sources: the menu, the Inspector
        // empty-state button, and the Hierarchy "+" menu.
        m_modelImport.draw(ctx.scene, ctx.resources, m_state);
        drawWorkspace(ec);

    } else {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }
    ImGui::End();

    // Separate floating window; drawn after the root so it stacks on top.
    if (m_state.showPreferences) {
        PROFILE_SCOPE("Panel/Preferences");
        m_preferences.draw(ec);
    }
    if (m_state.showMaterialEditor) {
        PROFILE_SCOPE("Panel/MaterialEditor");
        m_materialEditor.draw(ec);
    }
    if (m_state.showAssetBrowser) {
        PROFILE_SCOPE("Panel/AssetBrowser");
        m_assetBrowser.draw(ec);
    }

    {
        PROFILE_SCOPE("Editor/ImGuiRender");
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
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
        PROFILE_SCOPE("Panel/Hierarchy");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
        if (ImGui::BeginChild("##Hierarchy", ImVec2(leftW, mainH), ImGuiChildFlags_Borders)) {
            m_hierarchy.draw(ec);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::SameLine(0, 0);
    }

    {
        PROFILE_SCOPE("Panel/Viewport");
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        float centerW = viewport->WorkSize.x - leftW - rightW;
        ImVec2 vpMin = ImGui::GetCursorScreenPos();
        if (ImGui::BeginChild("##Viewport", ImVec2(centerW, mainH), ImGuiChildFlags_None)) {
            ec.viewportPos  = vpMin;
            ec.viewportSize = ImVec2(centerW, mainH);
            // Tell the engine the viewport rect so next frame's render
            // pipeline sizes its FBOs and projection to this rect instead
            // of the full GLFW window.
            ec.frame.window.setSceneViewport(
                static_cast<uint32_t>(std::max(0.0f, vpMin.x)),
                static_cast<uint32_t>(std::max(0.0f, vpMin.y)),
                static_cast<uint32_t>(std::max(1.0f, centerW)),
                static_cast<uint32_t>(std::max(1.0f, mainH)));
            m_state.viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
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
        PROFILE_SCOPE("Panel/Inspector");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        if (ImGui::BeginChild("##Inspector", ImVec2(rightW, mainH), ImGuiChildFlags_Borders)) {
            m_inspector.draw(ec);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    if (m_state.showBottom) {
        PROFILE_SCOPE("Panel/Bottom");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        if (ImGui::BeginChild("##Bottom", ImVec2(0, bottomH), ImGuiChildFlags_Borders)) {
            m_bottom.draw(ec);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    m_panelResize.process(m_state, panelAreaStart, mainH,
                          viewport->WorkSize.x, m_gizmoOverlay.isGizmoUsing());

    ImGui::PopStyleVar(); // ItemSpacing

    {
        PROFILE_SCOPE("Panel/StatusBar");
        m_statusBar.draw(ec);
    }
}

} // namespace Engine
