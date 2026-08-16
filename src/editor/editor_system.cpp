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

#include "core/engine.h"

namespace Engine {

EditorSystem::EditorSystem(
    Engine& engine,
    GLFWwindow* window,
    CameraControllerSystem& cameraController,
    UISystem& uiSystem,
    VisibilitySystem& visibilitySystem,
    RenderSystem& renderSystem,
    ScriptModule& scriptModule,
    const std::string& projectName
)
    : m_engine(engine)
    , m_window(window)
    , m_cameraController(cameraController)
    , m_uiSystem(uiSystem)
    , m_renderSystem(renderSystem)
    , m_visibilitySystem(visibilitySystem)
    , m_scriptModule(scriptModule)
    , m_materialPreviews(renderSystem)
    , m_sceneIO(cameraController, m_materialPreviews)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Floating windows (Material Editor, Preferences) move only by their
    // title bar - dragging inside the body must not drag the window, so
    // viewport orbiting on the material preview stays put.
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // ImGui ini lives next to the engine: window positions and table column
    // widths are how *this user* likes the editor laid out, not something a
    // project owns - and the path is captured once for ImGui's lifetime, so a
    // project-rooted one would go stale the moment another project is opened.
    // Static so the c_str pointer stays valid for ImGui's lifetime.
    static std::string s_iniPath = (ProjectPaths::engineRoot() / "imgui.ini").string();
    io.IniFilename = s_iniPath.c_str();

    // A real TTF instead of ImGui's 13 px bitmap default - the single biggest
    // visual upgrade every panel inherits. Roboto Medium already ships with the
    // engine (the in-game UI bakes its SDF font from it), so the editor reuses
    // it; swap the path to restyle. Sized against the window's content scale
    // so text stays crisp on HiDPI displays.
    {
        float scaleX = 1.0f, scaleY = 1.0f;
        glfwGetWindowContentScale(window, &scaleX, &scaleY);
        const float fontSize = std::floor(15.0f * std::max(scaleX, 1.0f));
        static std::string s_fontPath =
            (ProjectPaths::engineFonts() / "Roboto-Medium.ttf").string();
        if (!io.Fonts->AddFontFromFileTTF(s_fontPath.c_str(), fontSize)) {
            LOG_WARNING("Editor font %s failed to load; using the ImGui default",
                        s_fontPath.c_str());
        }

        // The icon font (Lucide). Missing file falls back to the built-in
        // vector glyphs, so this is a soft dependency.
        static std::string s_iconPath =
            (ProjectPaths::engineFonts() / "lucide.ttf").string();
        if (!loadEditorIconFont(s_iconPath.c_str())) {
            LOG_WARNING("Icon font %s failed to load; using vector glyphs",
                        s_iconPath.c_str());
        }
    }

    applyEditorTheme();

    // Restore persisted editor state (panel widths, toggles, snap, keybinds,
    // recent scenes). Missing/invalid file is non-fatal - defaults apply.
    // The grid defaults off engine-wide (it is an editor aid); the editor
    // wants it on out of the box. Set before the load so a persisted value
    // still wins.
    m_renderSystem.getSettings().grid = true;
    EditorSettings::load(m_state, m_renderSystem.getSettings());

    // Handed in rather than re-read: loadProject deliberately reports the
    // project once per open. Opening another one refreshes this
    // (ProjectController::open).
    m_state.projectName = projectName;

    const size_t recentBefore = m_state.recentScenes.size();

    // Drop recent-scene entries whose files no longer exist. Without this
    // the Open Recent menu accumulates dead links across sessions.
    m_state.recentScenes.erase(
        std::remove_if(m_state.recentScenes.begin(), m_state.recentScenes.end(),
            [](const std::string& p) {
                std::error_code ec;
                return !std::filesystem::exists(p, ec);
            }),
        m_state.recentScenes.end());
    if (recentBefore != m_state.recentScenes.size()) {
        LOG_INFO("Pruned %zu stale entries from Open Recent",
            recentBefore - m_state.recentScenes.size());
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 430");

    // Capture engine-reported recoverable errors into our log for the Errors tab.
    setErrorSink(&m_errorLog);

    LOG_INFO("Initialized (%zu recent scene(s) restored)",
        m_state.recentScenes.size());
}

EditorSystem::~EditorSystem() {
    LOG_TRACE("Shutting down, saving settings");
    setErrorSink(nullptr);
    EditorSettings::save(m_state, m_renderSystem.getSettings());
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void EditorSystem::openPendingProject(EditorContext& ec) {
    if (ec.state.pendingProjectOpen.empty()) return;

    // Opening a project throws the current scene away, so it asks first, the
    // same way New Scene does. The chosen path stays parked until the prompt is
    // answered; answering it clears the flag and this runs on the next frame.
    if (ec.state.sceneDirty) {
        ec.state.confirmAction = EditorState::PendingSceneAction::OpenProject;
        return;
    }

    // Deferred out of the menu: opening rebuilds the scene, and the menu that
    // asked is still being drawn when it asks.
    const std::string path = ec.state.pendingProjectOpen;
    ec.state.pendingProjectOpen.clear();
    m_project.open(ec, m_scriptModule, m_sceneIO, path);
}

void EditorSystem::performSceneAction(FrameContext& ctx, EditorState::PendingSceneAction action) {
    switch (action) {
        case EditorState::PendingSceneAction::Quit:
            ctx.window.requestClose();
            break;
        case EditorState::PendingSceneAction::New:
            m_sceneIO.newScene(ctx, m_state);
            break;
        case EditorState::PendingSceneAction::Open:
            m_sceneIO.loadPath(ctx, m_state, m_state.pendingScenePath);
            m_state.pendingScenePath.clear();
            break;
        case EditorState::PendingSceneAction::OpenProject:
            // The project is still parked in pendingProjectOpen; dropping the
            // dirty flag lets the deferred open through on the next frame.
            m_state.sceneDirty = false;
            break;
        case EditorState::PendingSceneAction::None:
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

// The single writer of the window title: "<project> - <file> [*] - VKM Engine".
// Which project, which scene and whether it is dirty are all facts the title
// carries, so one place owns the format - anything else setting the title is
// overwritten here on the next frame. Compared against its own last output so
// the GLFW call happens only when the content actually changed.
void syncWindowTitle(WindowManager& window, const std::string& project,
                     const std::string& path, bool dirty) {
    static std::string s_last;
    const std::string fname = path.empty()
        ? "untitled" : std::filesystem::path(path).filename().string();
    std::string title = (project.empty() ? std::string() : project + " - ")
                      + fname + (dirty ? " *" : "") + " - VKM Engine";
    if (title != s_last) {
        window.setTitle(title);
        s_last = std::move(title);
    }
}
}

void EditorSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("EditorSystem");
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

    // Shader hot reload. Polled rather than watched: a filesystem watcher is a
    // per-platform dependency for something a once-a-second directory scan of a
    // few dozen files already answers. Editor-only - a shipped runtime has no
    // shader sources to watch and should not be touching the disk each frame.
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

    // Hot-reload the gameplay module on request (Edit > Reload Scripts):
    // serialize behaviors, swap the game module, recreate them - entities untouched.
    if (m_state.requestScriptReload) {
        m_state.requestScriptReload = false;
        if (m_scriptModule.reload(ctx.scene)) {
            m_state.pushToast(EditorState::ToastKind::Info, "Reloaded scripts");
        } else {
            m_state.pushToast(EditorState::ToastKind::Error,
                "Script reload failed - see log. Fix the build and reload again.");
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

    // Intercept window-close while the scene is dirty: clear shouldClose,
    // open the save-on-quit modal next frame. A clean scene closes through
    // normally. The modal lives in the ImGui frame below so it works in
    // both visible and hidden editor states.
    if (ctx.window.shouldClose() && m_state.sceneDirty
            && m_state.confirmAction == EditorState::PendingSceneAction::None) {
        ctx.window.cancelClose();
        m_state.confirmAction = EditorState::PendingSceneAction::Quit;
    }

    // After a "Save" choice in the modal, three outcomes are possible:
    //   (a) Save was synchronous (path set) -> sceneDirty drops to false -> close now.
    //   (b) Save-As opened, user picks a name -> sceneDirty drops later -> close then.
    //   (c) Save-As opened, user cancels -> no save dialog active, scene still
    //       dirty -> user changed their mind, drop the intent.
    if (m_state.afterSaveAction != EditorState::PendingSceneAction::None) {
        if (!m_state.sceneDirty) {
            performSceneAction(ctx, m_state.afterSaveAction);
            m_state.afterSaveAction = EditorState::PendingSceneAction::None;
        } else if (!m_sceneIO.isSaveDialogActive()) {
            m_state.afterSaveAction = EditorState::PendingSceneAction::None;
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
    // Save-guard modal: top-priority, drawn before anything else so it's
    // visible whether the editor is shown or hidden. Shared by the window
    // close-intercept above and File > New Scene - the pending intent is
    // whichever confirming* flag is set. The dialog scaffold owns the open
    // handshake (fixing the old bug where OpenPopup re-fired every frame and
    // Escape could never dismiss it) and the Enter/Escape contract.
    {
        bool want = m_state.confirmAction != EditorState::PendingSceneAction::None;
        if (beginDialog("Unsaved Changes", want)) {
            const EditorState::PendingSceneAction action = m_state.confirmAction;
            ImGui::TextUnformatted("This scene has unsaved changes.");
            ImGui::Spacing();
            ImGui::TextDisabled("%s", m_sceneIO.path().empty()
                ? "(untitled scene)" : m_sceneIO.path().c_str());

            switch (dialogButtons(want, "Save", true, "Cancel", "Don't Save")) {
                case DialogResult::Confirm:
                    // save() opens Save-As if there's no current path; the
                    // deferred action fires once sceneDirty drops to false.
                    m_sceneIO.save(ctx, m_state);
                    m_state.afterSaveAction = action;
                    break;
                case DialogResult::Alt:
                    performSceneAction(ctx, action);
                    break;
                case DialogResult::Cancel:
                    // Abandon the project that was waiting on this answer, or
                    // the prompt reopens next frame and there is no way out.
                    m_state.pendingProjectOpen.clear();
                    break;
                default: break;
            }
            endDialog();
        }
        if (!want) m_state.confirmAction = EditorState::PendingSceneAction::None;
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
        // know how to bring it back. Uses the live (rebindable) toggle key
        // and auto-sizes with the font instead of a fixed 180x28 box.
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            const float pad = EditorStyle::px(10.0f);
            char hint[64];
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

    EditorContext ec{
        ctx,
        m_state,
        m_engine,
        m_cameraController,
        m_renderSystem,
        m_visibilitySystem,
        m_materialPreviews,
        m_errorLog,
        {},
        {}
    };

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
        // Once, on the first UI frame: settings have been loaded by now, so the
        // startup project takes its place at the front of the list.
        if (!m_notedStartupProject) {
            m_project.noteCurrentProject(ec);
            m_notedStartupProject = true;
        }
        m_project.drawDialog(ec, m_scriptModule, m_sceneIO);
        // ModelImportDialog is owned here (not in the menu bar) so it
        // serves all three import-intent sources: the menu, the Inspector
        // empty-state button, and the Hierarchy "+" menu.
        m_modelImport.draw(ctx.scene, ctx.resources, m_state);
        drawWorkspace(ec);
        openPendingProject(ec);

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
    if (m_state.showRenderSettings) {
        PROFILE_SCOPE("Panel/RenderSettings");
        m_renderSettings.draw(ec);
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
    // the theme spacing INSIDE its child, so panel content - and every popup
    // opened from it, which snapshots the style at Begin - keeps the theme's
    // rhythm instead of inheriting the tiling hack (menus opened from panels
    // used to render squashed while the same menu from the menu bar did not).
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
            // Tell the engine the viewport rect so next frame's render
            // pipeline sizes its FBOs and projection to this rect instead
            // of the full GLFW window. The rect is ImGui's, in window screen
            // coords; the engine wants framebuffer pixels.
            const float vpScale = ec.frame.window.framebufferScale();
            ec.frame.window.setSceneViewport(
                static_cast<uint32_t>(std::max(0.0f, vpMin.x * vpScale)),
                static_cast<uint32_t>(std::max(0.0f, vpMin.y * vpScale)),
                static_cast<uint32_t>(std::max(1.0f, centerW * vpScale)),
                static_cast<uint32_t>(std::max(1.0f, mainH   * vpScale)));
            m_state.viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
            m_viewportOverlay.drawNavigationGizmo(ec);
            m_gizmoOverlay.drawLightGizmos(ec);
            m_gizmoOverlay.drawCameraGizmos(ec);
            m_gizmoOverlay.drawProbeGizmos(ec);
            m_gizmoOverlay.drawEffectGizmos(ec);
            if (m_state.showColliders) m_gizmoOverlay.drawColliderGizmos(ec);
            if (m_state.showBounds)    m_gizmoOverlay.drawBoundsGizmos(ec);
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
        PROFILE_SCOPE("Panel/Inspector");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
        if (ImGui::BeginChild("##Inspector", ImVec2(rightW, mainH), ImGuiChildFlags_Borders)) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, themeSpacing);
            m_inspector.draw(ec);
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
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

    m_panelResize.process(m_state, panelAreaStart, mainH,
                          viewport->WorkSize.x, m_gizmoOverlay.isGizmoUsing());

    ImGui::PopStyleVar(); // ItemSpacing

    {
        PROFILE_SCOPE("Panel/StatusBar");
        m_statusBar.draw(ec);
    }
}

} // namespace Engine
