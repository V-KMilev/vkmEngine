#define VKM_LOG_CATEGORY "EDITOR"

#include "editor_system.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include "logger.h"

#include "core/clock.h"
#include "core/system.h"
#include "debug/engine_error_log.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "framework/editor_context.h"
#include "framework/editor_settings.h"
#include "input/editor_keybinds.h"
#include "ui/editor_style.h"
#include "ui/editor_dialogs.h"
#include "ui/editor_icons.h"
#include "platform/window/window_manager.h"
#include "system/camera/camera_controller_system.h"
#include "system/ui/ui_system.h"
#include "system/render/render_system.h"
#include "system/render/editor_render_hooks.h"
#include "system/script/script_module.h"
#include "system/render/render_view.h"
#include "ui/editor_theme.h"
#include "io/project_paths.h"

namespace Vkm::Engine {

EditorSystem::EditorSystem(
    GLFWwindow* window,
    CameraControllerSystem& cameraController,
    UISystem& uiSystem,
    VisibilitySystem& visibilitySystem,
    RenderSystem& renderSystem,
    AudioSystem& audioSystem,
    ScriptModule& scriptModule
)
    : m_cameraController(cameraController)
    , m_uiSystem(uiSystem)
    , m_renderSystem(renderSystem)
    , m_visibilitySystem(visibilitySystem)
    , m_audioSystem(audioSystem)
    , m_scriptModule(scriptModule)
    , m_materialPreviews(renderSystem)
    , m_sceneIO(cameraController, m_materialPreviews)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Floating windows (Preferences, Render Settings) move only by their
    // title bar - dragging inside the body must not drag the window, so a
    // drag that means something to the content stays with the content.
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // Under the user root (see docs/reference/system/io.md, "Which root owns a
    // path"), and static because ImGui holds the c_str for its whole lifetime -
    // a project-rooted path would go stale on the next Open Project anyway.
    static std::string s_iniPath = (ProjectPaths::userRoot() / "imgui.ini").string();
    io.IniFilename = s_iniPath.c_str();

    // A real TTF instead of ImGui's 13 px bitmap default; Roboto Medium already
    // ships with the engine, so the editor reuses it. Sized against the window's
    // content scale so text stays crisp on HiDPI displays.
    float scaleX = 1.0f, scaleY = 1.0f;
    glfwGetWindowContentScale(window, &scaleX, &scaleY);
    // Clamped at 1 because a scale below it would shrink the font rather than
    // leave it alone. The theme reads the same number, so chrome and text are
    // never scaled by two different ones.
    const float uiScale = std::max(scaleX, 1.0f);
    {
        const float fontSize = std::floor(15.0f * uiScale);
        static std::string s_fontPath =
            (ProjectPaths::engineFonts() / "Roboto-Medium.ttf").string();
        if (!io.Fonts->AddFontFromFileTTF(s_fontPath.c_str(), fontSize)) {
            LOG_WARNING("Editor font %s failed to load; using the ImGui default",
                        s_fontPath.c_str());
        }

        // The icon font (Lucide). It ships with the engine, so a failure means
        // the file was removed; the editor still runs, with square placeholders.
        static std::string s_iconPath =
            (ProjectPaths::engineFonts() / "lucide.ttf").string();
        if (!loadEditorIconFont(s_iconPath.c_str())) {
            LOG_WARNING("Icon font %s failed to load; icons will draw as placeholders",
                        s_iconPath.c_str());
        }
    }

    applyEditorTheme(uiScale);

    // The fly controls are an authoring tool, so the editor is what asks for
    // them. Off by default rather than switched off by the runtime: right-drag
    // grabs the pointer, and a behavior reaches no system to give it back.
    m_cameraController.setEnabled(true);

    // The grid defaults off engine-wide (it is an editor aid); the editor wants
    // it on out of the box. Set before init() reads the project's settings, so
    // a persisted value still wins.
    m_renderSystem.getSettings().grid = true;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    // Capture engine-reported recoverable errors into our log for the Errors tab.
    setErrorSink(&m_errorLog);

    LOG_INFO("Initialized");
}

EditorSystem::~EditorSystem() {
    LOG_TRACE("Shutting down, saving settings");
    setErrorSink(nullptr);
    EditorSettings::save(m_state, m_renderSystem.getSettings());
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void EditorSystem::init(FrameContext& ctx) {
    EditorContext ec = makeContext(ctx);
    m_project.open(ec, m_scriptModule, m_sceneIO, ProjectPaths::projectRoot().string(),
                   ProjectController::OpenKind::Startup);
}

EditorContext EditorSystem::makeContext(FrameContext& ctx) {
    return EditorContext{
        ctx,
        m_state,
        m_cameraController,
        m_renderSystem,
        m_visibilitySystem,
        m_materialPreviews,
        m_audioSystem,
        m_errorLog,
        {},
        {}
    };
}

void EditorSystem::resolveSceneAction(EditorContext& ec) {
    EditorState& state = ec.state;
    if (state.pendingAction == EditorState::SceneAction::None) return;

    switch (state.actionStage) {
        case EditorState::ActionStage::Ask:
            // Nothing to lose: a scene with no unsaved edits needs no prompt.
            if (!state.sceneDirty) state.actionStage = EditorState::ActionStage::Run;
            break;
        case EditorState::ActionStage::Saving:
            // The Save answer either lands, or the author backs out of the
            // Save-As it opened - which withdraws the action along with it.
            if (!state.sceneDirty) state.actionStage = EditorState::ActionStage::Run;
            else if (!m_sceneIO.isSaveDialogActive()) state.clearSceneAction();
            break;
        case EditorState::ActionStage::Run:
            break;
    }
    if (state.actionStage != EditorState::ActionStage::Run) return;

    const EditorState::SceneAction action = state.pendingAction;
    const std::string payload = state.actionPayload;
    state.clearSceneAction();
    performSceneAction(ec, action, payload);
}

void EditorSystem::performSceneAction(EditorContext& ec, EditorState::SceneAction action,
                                      const std::string& payload) {
    switch (action) {
        case EditorState::SceneAction::Quit:
            ec.frame.window.requestClose();
            break;
        case EditorState::SceneAction::New:
            m_sceneIO.newScene(ec.frame, ec.state);
            break;
        case EditorState::SceneAction::Open:
            m_sceneIO.loadPath(ec.frame, ec.state, payload);
            break;
        case EditorState::SceneAction::OpenProject:
            m_project.open(ec, m_scriptModule, m_sceneIO, payload,
                           ProjectController::OpenKind::Switch);
            break;
        case EditorState::SceneAction::None:
            break;
    }
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
        case EditorState::ToastKind::Error:   bg = EditorStyle::TOAST_ERROR_BG;   break;
        case EditorState::ToastKind::Warning: bg = EditorStyle::TOAST_WARNING_BG; break;
        default:                              bg = EditorStyle::TOAST_INFO_BG;    break;
    }
    bg.w = 0.95f * alpha;

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

// The single writer of the window title: "<project> - <file> [*] - vkmEngine".
// One place owns the format - anything else setting the title is overwritten
// here on the next frame. Compared against its own last output so the GLFW
// call happens only when the content actually changed.
void syncWindowTitle(WindowManager& window, const std::string& project,
                     const std::string& path, bool dirty) {
    static std::string s_last;
    const std::string fname = path.empty()
        ? "untitled" : std::filesystem::path(path).filename().string();
    std::string title = (project.empty() ? std::string() : project + " - ")
                      + fname + (dirty ? " *" : "") + " - vkmEngine";
    if (title != s_last) {
        window.setTitle(title);
        s_last = std::move(title);
    }
}
}

void EditorSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("EditorSystem");

    EditorContext ec = makeContext(ctx);

    // Taken every frame either way, so a session's own flying does not sit in
    // the flag until the next Stop; see docs/reference/editor.md, "Flying the
    // camera is an edit".
    const bool cameraMoved = m_cameraController.takeCameraMoved();
    if (cameraMoved && !m_sceneIO.isPlaying()) m_state.markSceneDirty();

    syncWindowTitle(ctx.window, m_state.projectName, m_sceneIO.path(), m_state.sceneDirty);

    m_materialPreviews.onFrameBegin();

    // Surface newly-reported errors as a toast (the persistent list is in
    // Bottom > Errors). totalPushed ignores repeats, so a behavior that throws
    // then gets disabled toasts exactly once.
    if (const unsigned long long total = m_errorLog.totalPushed();
            total > m_lastErrorTotal) {
        m_lastErrorTotal = total;
        const auto recent = m_errorLog.snapshot();
        if (!recent.empty()) {
            const auto& e = recent.front();
            m_state.pushToast(EditorState::ToastKind::Error,
                "[" + e.category + "] " + e.source + " - see Bottom > Errors");
        }
    }

    // Polled rather than watched: a filesystem watcher is a per-platform
    // dependency for what a once-a-second scan of a few dozen files answers.
    // Editor-only - a shipped runtime has no shader sources to watch.
    m_shaderPollTimer += ctx.clock.getDeltaTime();
    if (m_shaderPollTimer >= SHADER_POLL_INTERVAL) {
        m_shaderPollTimer = 0.0f;
        if (RenderBackend* backend = m_renderSystem.backend()) {
            const uint32_t reloaded = backend->reloadChangedShaders();
            if (reloaded > 0) {
                m_state.pushToast(EditorState::ToastKind::Info,
                    "Reloaded " + std::to_string(reloaded) + " shader(s)");
            }
        }
    }

    // Hot-reload the gameplay module on request (Edit > Reload Scripts);
    // behaviors are serialized and recreated, entities untouched.
    if (m_state.requestScriptReload) {
        m_state.requestScriptReload = false;
        if (m_scriptModule.reload(ctx.scene)) {
            m_state.pushToast(EditorState::ToastKind::Info, "Reloaded scripts");
        } else {
            // The durable record is the Errors tab entry ScriptModule::reload
            // reports; this is the glance-level notice that points at it.
            m_state.pushToast(EditorState::ToastKind::Error,
                "Script reload failed - see Bottom > Errors. Fix the build and reload again.");
        }
    }

    // Selection hygiene: deletes / scene swaps can leave dead ids in the
    // multi-select set - prune once per frame before any UI reads it.
    m_state.selection.erase(
        std::remove_if(m_state.selection.begin(), m_state.selection.end(),
            [&](EntityId id) { return !id || !ctx.scene.isAlive(id); }),
        m_state.selection.end());
    if (m_state.selectedEntity && !ctx.scene.isAlive(m_state.selectedEntity)) {
        m_state.selectedEntity = m_state.selection.empty() ? EntityId{}
                                                           : m_state.selection.back();
    }

    // A titlebar close is a request like any other: withdraw it and put it
    // through the guard, which raises it again once the scene is safe.
    if (ctx.window.shouldClose()) {
        ctx.window.cancelClose();
        m_state.requestSceneAction(EditorState::SceneAction::Quit);
    }

    resolveSceneAction(ec);

    // Before anything else: the editor-toggle keybind is processed here so the
    // rebind UI in Preferences drives it, and the toggle sits in both the hidden
    // and visible branches because the ImGui frame exists in both.
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
    // Drawn before anything else, so it is visible whether the editor is shown
    // or hidden. It answers the pending request rather than acting on it -
    // resolveSceneAction performs what it approves, outside the ImGui frame.
    {
        bool want = m_state.pendingAction != EditorState::SceneAction::None
                 && m_state.actionStage == EditorState::ActionStage::Ask;
        if (beginDialog("Unsaved Changes", want)) {
            ImGui::TextUnformatted("This scene has unsaved changes.");
            ImGui::Spacing();
            ImGui::TextDisabled("%s", m_sceneIO.path().empty()
                ? "(untitled scene)" : m_sceneIO.path().c_str());

            switch (dialogButtons(want, "Save", "Don't Save")) {
                case DialogResult::Confirm:
                    // The scene this save is for is the authored one Stop puts
                    // back, not the simulation's copy - and a save inside a
                    // session is refused outright, so end the session first.
                    m_sceneIO.stopPlaySession(ctx, m_state);
                    m_sceneIO.save(ctx, m_state);
                    m_state.actionStage = EditorState::ActionStage::Saving;
                    break;
                case DialogResult::Alt:
                    m_state.actionStage = EditorState::ActionStage::Run;
                    break;
                case DialogResult::Cancel:
                    m_state.clearSceneAction();
                    break;
                default: break;
            }
            endDialog();
        }
        // Dismissed without answering (Escape, or the window closing): the
        // request goes with it, or the prompt reopens with no way out.
        if (!want && m_state.pendingAction != EditorState::SceneAction::None
                && m_state.actionStage == EditorState::ActionStage::Ask) {
            m_state.clearSceneAction();
        }
    }

    // Toast renders in both visible/hidden paths - failure feedback should
    // not vanish just because F5 was pressed.
    drawToast(m_state, ctx.clock.getDeltaTime());

    if (!m_state.editorVisible) {
        m_cameraController.setEditorInputCapture(false, false);
        m_uiSystem.setEditorPointerCapture(false);

        // No panels to layout this frame - let the 3D pipeline fill the
        // whole window next frame, not the stale viewport sub-rect.
        ctx.window.setSceneViewport(0, 0,
            static_cast<uint32_t>(ctx.window.getWidth()),
            static_cast<uint32_t>(ctx.window.getHeight()));

        // While the editor is hidden, draw a tiny corner hint so new users
        // know how to bring it back, naming the live (rebindable) toggle key.
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            const float pad = EditorStyle::px(10.0f);
            // Wide enough for the longest KeyLabel plus the sentence around it,
            // so a rebound chord is never cut off mid-word.
            char hint[80];
            snprintf(hint, sizeof(hint), "Press %s to show editor",
                     keyLabel(m_state.keybinds.toggleEditor).buf);
            ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - pad,
                                           vp->WorkPos.y + vp->WorkSize.y - pad),
                                    ImGuiCond_Always, ImVec2(1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
            ImGui::Begin("##F5Hint", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::TextDisabled("%s", hint);
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
        // The game UI lays out inside the same viewport rect the chrome above is
        // drawn over, so it needs the same answer about who owns the pointer.
        m_uiSystem.setEditorPointerCapture(blockMouse);
    }

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
        // The three dialogs are owned here rather than by the menu bar, because
        // each serves more than one place that asks for it and a menu closes the
        // frame its item is clicked.
        m_newProject.draw(m_state);
        m_openProject.draw(m_state);
        m_modelImport.draw(ctx.scene, ctx.resources, m_state);
        m_placePrefab.draw(ctx.scene, ctx.resources, m_state);
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
    if (m_state.showRenderSettings) {
        PROFILE_SCOPE("Panel/RenderSettings");
        m_renderSettings.draw(ec);
    }

    // The gesture boundary, after every panel has had its chance to push. Both
    // halves are needed: a gizmo drag holds the mouse with no ImGui item active,
    // while a keyboard-tweaked slider keeps its item active with the mouse up.
    if (!ImGui::IsAnyMouseDown() && !ImGui::IsAnyItemActive()) {
        m_state.commands.endGesture();
    }

    {
        PROFILE_SCOPE("Editor/ImGuiRender");
        // Runs after RenderSystem (Render stage) drew the scene this frame, so
        // the UI composites on top. Submit is here rather than a separate
        // backend hook now that everything is single-threaded.
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}

void EditorSystem::drawWorkspace(EditorContext& ec) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Zero spacing tiles the panel children edge-to-edge; each panel restores
    // the theme spacing inside its child, so its content - and every popup
    // opened from it, which snapshots the style at Begin - keeps that rhythm.
    const ImVec2 themeSpacing = ImGui::GetStyle().ItemSpacing;
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
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, themeSpacing);
            m_hierarchy.draw(ec);
            ImGui::PopStyleVar();
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
            // The engine sizes next frame's FBOs and projection to this rect
            // rather than to the full GLFW window. The rect is ImGui's, in
            // window screen coords; the engine wants framebuffer pixels.
            const float vpScale = ec.frame.window.framebufferScale();
            ec.frame.window.setSceneViewport(
                static_cast<uint32_t>(std::max(0.0f, vpMin.x * vpScale)),
                static_cast<uint32_t>(std::max(0.0f, vpMin.y * vpScale)),
                static_cast<uint32_t>(std::max(1.0f, centerW * vpScale)),
                static_cast<uint32_t>(std::max(1.0f, mainH   * vpScale)));
            m_state.viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
            m_viewportOverlay.drawNoCameraNotice(ec);
            m_viewportOverlay.drawNavigationGizmo(ec);
            m_gizmoOverlay.drawLightGizmos(ec);
            m_gizmoOverlay.drawCameraGizmos(ec);
            m_gizmoOverlay.drawProbeGizmos(ec);
            m_gizmoOverlay.drawEffectGizmos(ec);
            m_gizmoOverlay.drawAudioGizmos(ec);
            if (m_state.showColliders) m_gizmoOverlay.drawColliderGizmos(ec);
            if (m_state.showBounds)    m_gizmoOverlay.drawBoundsGizmos(ec);
            if (m_state.showSkeletons) m_gizmoOverlay.drawSkeletonGizmos(ec);
            m_gizmoOverlay.drawSelectionOutline(ec);
            m_gizmoOverlay.drawTransformGizmo(ec);
            m_viewportToolbar.draw(ec);
            m_viewportToolbar.drawViewMode(ec);
            m_playbar.draw(ec, m_sceneIO);
            if (!m_viewportToolbar.isHovered() && !m_playbar.isHovered()
                    && !m_viewportOverlay.isHovered())
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
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, themeSpacing);
            drawRightTabs(ec);
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();

        // Read after EndChild, which is where ImGui makes the child itself the
        // last item. Before it, these are whatever widget the Inspector drew
        // last - which is a rectangle inside the panel rather than the panel.
        m_state.rightPanelMin = ImGui::GetItemRectMin();
        m_state.rightPanelMax = ImGui::GetItemRectMax();
        ImGui::PopStyleVar();
    }

    if (m_state.showBottom) {
        PROFILE_SCOPE("Panel/Bottom");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        if (ImGui::BeginChild("##Bottom", ImVec2(0, bottomH), ImGuiChildFlags_Borders)) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, themeSpacing);
            m_bottom.draw(ec);
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    drawFloatingMaterial(ec);


    m_panelResize.process(m_state, panelAreaStart, mainH,
                          viewport->WorkSize.x, m_gizmoOverlay.isGizmoUsing());

    ImGui::PopStyleVar(); // ItemSpacing

    {
        PROFILE_SCOPE("Panel/StatusBar");
        m_statusBar.draw(ec);
    }
}

void EditorSystem::drawRightTabs(EditorContext& ec) {
    if (!ImGui::BeginTabBar("##RightTabs", ImGuiTabBarFlags_DrawSelectedOverline)) return;

    if (ImGui::BeginTabItem("Inspector")) {
        PROFILE_SCOPE("Panel/Inspector");
        m_inspector.draw(ec);
        ImGui::EndTabItem();
    }

    // The request is answered by this frame's bar or not at all: left set, it
    // would take the tab back the next time the panel is shown.
    ImGuiTabItemFlags materialFlags = ImGuiTabItemFlags_None;
    if (m_state.revealMaterialTab) {
        materialFlags = ImGuiTabItemFlags_SetSelected;
        m_state.revealMaterialTab = false;
    }
    if (!m_state.materialFloating) {
        const bool open = ImGui::BeginTabItem("Material", nullptr, materialFlags);

        // Asked of the tab itself, before its body: a drag that leaves the bar
        // is a request to detach, and the distance keeps a click from being one.
        if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImGui::SetTooltip("Drag out to open in a window");
        }
        if (ImGui::IsItemActive() &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left, EditorStyle::px(24.0f))) {
            m_state.materialFloating = true;
            m_state.materialDetachAt = ImGui::GetMousePos();
        }

        if (open) {
            PROFILE_SCOPE("Panel/MaterialEditor");
            m_materialEditor.draw(ec);
            ImGui::EndTabItem();
        }
    }

    ImGui::EndTabBar();
}

void EditorSystem::drawFloatingMaterial(EditorContext& ec) {
    if (!m_state.materialFloating) return;

    if (m_state.materialDetachAt.x != 0.0f || m_state.materialDetachAt.y != 0.0f) {
        // Under the cursor that pulled it out, so the window arrives where the
        // hand already is rather than wherever it was last left.
        ImGui::SetNextWindowPos(m_state.materialDetachAt, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        m_state.materialDetachAt = {};
    }
    ImGui::SetNextWindowSize(ImVec2(EditorStyle::px(520.0f), EditorStyle::px(680.0f)),
                             ImGuiCond_FirstUseEver);

    // Closing re-docks rather than hiding: a window that can be lost behind the
    // viewport is the complaint that moved this panel out of one, so the close
    // box gives it back to the tab bar instead of making it vanish.
    bool open = true;
    const bool wasBegun = ImGui::Begin("Material Editor", &open);

    // Dragged by its title bar, with nothing inside it holding the mouse. There
    // is no public "is this window moving", and this is what moving one looks
    // like from outside.
    const bool dragging = ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() &&
                          ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool overPanel = m_state.showInspector &&
        mouse.x >= m_state.rightPanelMin.x && mouse.x <= m_state.rightPanelMax.x &&
        mouse.y >= m_state.rightPanelMin.y && mouse.y <= m_state.rightPanelMax.y;

    if (wasBegun) {
        PROFILE_SCOPE("Panel/MaterialEditor");
        m_materialEditor.draw(ec);
    }
    ImGui::End();

    if (dragging && overPanel) {
        // Painted over everything, because the panel it lands in is behind the
        // window being dragged onto it.
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImVec4 wash = EditorStyle::Accent::MatBase;
        wash.w = 0.18f;
        fg->AddRectFilled(m_state.rightPanelMin, m_state.rightPanelMax,
                          ImGui::GetColorU32(wash), EditorStyle::px(4.0f));
        fg->AddRect(m_state.rightPanelMin, m_state.rightPanelMax,
                    ImGui::GetColorU32(EditorStyle::Accent::MatBase),
                    EditorStyle::px(4.0f), 0, EditorStyle::px(2.0f));
    }
    // Gated on the drag: a release over the panel that did not follow one is an
    // ordinary click in the Inspector, and it used to close the window.
    if (dragging && overPanel && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_state.materialFloating = false;
    }
    if (!open) m_state.materialFloating = false;
}

} // namespace Vkm::Engine
