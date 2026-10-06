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

#include "core/host_chrome.h"
#include "core/clock.h"
#include "core/system.h"
#include "debug/engine_error_log.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "chrome/dock_layout.h"
#include "chrome/editor_status_bar.h"
#include "panels/errors_panel.h"
#include "editor_context.h"
#include "editor_settings.h"
#include "input/editor_keybinds.h"
#include "input/editor_shortcuts.h"
#include "ui/editor_style.h"
#include "ui/editor_dialogs.h"
#include "ui/editor_icons.h"
#include "platform/window/window_manager.h"
#include "system/audio/audio_system.h"
#include "input/camera_controller_system.h"
#include "system/splash/splash_frame.h"
#include "system/ui/ui_draw_data.h"
#include "system/render/render_system.h"
#include "system/script/script_module.h"
#include "ui/editor_theme.h"
#include "io/project_paths.h"
#include "session/project_controller.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Load the text and icon fonts at their unscaled design size.
 *
 * Not pre-multiplied by a scale: ImGui rasterises a glyph at the size it is drawn,
 * so the declared size is a base and the scale factors below apply per frame.
 * A failure leaves the ImGui default font and square icon placeholders.
 */
void buildEditorFonts() {
    ImGuiIO& io = ImGui::GetIO();

    static std::string s_fontPath =
        (ProjectPaths::engineFonts() / "Roboto-Medium.ttf").string();
    if (!io.Fonts->AddFontFromFileTTF(s_fontPath.c_str(), EditorStyle::REFERENCE_FONT_SIZE)) {
        LOG_WARNING("Editor font %s failed to load; using the ImGui default", s_fontPath.c_str());
    }

    static std::string s_iconPath =
        (ProjectPaths::engineFonts() / "lucide.ttf").string();
    if (!loadEditorIconFont(s_iconPath.c_str())) {
        LOG_WARNING("Icon font %s failed to load; icons will draw as placeholders", s_iconPath.c_str());
    }
}

/**
 * @brief The two factors the editor's size is the product of.
 *
 * Kept apart because they change at different times: the display's moves with the
 * window, the user's is where somebody disagrees with it.
 */
struct UiScale {
    float dpi  = 1.0f;
    float user = 1.0f;

    float product() const { return dpi * user; }
};

/**
 * @brief Read the display's content scale and fold the user's multiplier in.
 *
 * Cheap and side-effect free, so a caller can ask every frame.
 *
 * @param window Window whose content scale gives the display's factor.
 * @param userScale The user's multiplier, which Preferences holds in range.
 * @return Both factors.
 */
UiScale resolveUiScale(GLFWwindow* window, float userScale) {
    float scaleX = 1.0f, scaleY = 1.0f;
    glfwGetWindowContentScale(window, &scaleX, &scaleY);
    // Clamped at 1: below it would shrink the font rather than leave it alone.
    return UiScale{std::max(scaleX, 1.0f), userScale};
}

/**
 * @brief Put @p scale into effect for the frames that follow.
 *
 * The theme is re-applied with their product because ImGui's own metrics are
 * absolute pixels that do not follow the font; what the editor draws itself does,
 * through EditorStyle::px().
 *
 * @param scale The factors to install.
 */
void applyUiScale(const UiScale& scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontScaleDpi  = scale.dpi;
    style.FontScaleMain = scale.user;

    applyEditorTheme(scale.product());
}

} // namespace

EditorSystem::EditorSystem(
    GLFWwindow* window,
    CameraControllerSystem& cameraController,
    RenderSystem& renderSystem,
    RenderSettings& render,
    AudioSystem& audioSystem,
    BehaviorSystem& behaviorSystem,
    ScriptModule& scriptModule
)
    : m_cameraController(cameraController)
    , m_renderSystem(renderSystem)
    , m_render(render)
    , m_audioSystem(audioSystem)
    , m_behaviorSystem(behaviorSystem)
    , m_scriptModule(scriptModule)
    , m_materialPreviews(renderSystem)
    , m_sceneIO(cameraController, m_materialPreviews, behaviorSystem)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Multi-viewport stays off: a panel dragged out would be a second GLFW window
    // with its own context to share, for a layout the dockspace already gives.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Windows move by title bar only, so a drag on the content stays with the content.
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // Static because ImGui holds the c_str for its lifetime. User root: see
    // docs/reference/io.md, "Which root owns a path".
    static std::string s_iniPath = (ProjectPaths::userRoot() / "imgui.ini").string();
    io.IniFilename = s_iniPath.c_str();
    // Otherwise ImGui writes a log into the working directory.
    io.LogFilename = nullptr;

    buildEditorFonts();
    // EditorSettings::loadUser runs in init(), so this is the display's scale;
    // update() installs the user's on the first frame.
    const UiScale scale = resolveUiScale(window, m_state.prefs.uiScale);
    applyUiScale(scale);
    m_appliedUiScale = scale.product();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(("#version " + std::to_string(OPENGL_GLSL_VERSION)).c_str());

    setErrorSink(&m_errorLog);

    LOG_INFO("Initialized");
}

void EditorSystem::shutdown() {
    LOG_TRACE("Shutting down, saving settings");
    setErrorSink(nullptr);
    if (m_state.projectOpen) {
        m_sceneIO.rememberView(m_state);
        EditorSettings::save(m_state, m_render);
    } else {
        EditorSettings::saveUser(m_state);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void EditorSystem::init(FrameContext& ctx) {
    EditorContext ec = makeContext(ctx);
    EditorSettings::loadUser(m_state);
    // Without a project every path resolves against the engine's own directory,
    // so the editor shows a picker rather than a workspace.
    ProjectController::open(
        ec,
        m_scriptModule,
        m_sceneIO,
        ProjectPaths::projectRoot().string(),
        ProjectController::OpenKind::Startup
    );
}

EditorContext EditorSystem::makeContext(FrameContext& ctx) {
    return EditorContext{
        ctx,
        m_state,
        m_cameraController,
        m_renderSystem,
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
            if (!state.sceneDirty) state.actionStage = EditorState::ActionStage::Run;
            break;
        case EditorState::ActionStage::Saving:
            // Backing out of the Save-As the answer opened withdraws the action.
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

void EditorSystem::performSceneAction(
    EditorContext& ec,
    EditorState::SceneAction action,
    const std::string& payload
) {
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
            ProjectController::open(
                ec,
                m_scriptModule,
                m_sceneIO,
                payload,
                ProjectController::OpenKind::Requested
            );
            break;
        case EditorState::SceneAction::None:
            break;
    }
}

namespace {

/**
 * @brief One ImGui frame: NewFrame on the way in, Render and submit on the way out.
 *
 * A missed `Render` leaves a half-open frame the next `NewFrame` asserts on, so the
 * pairing is a scope. The submit lands after RenderSystem drew the scene, so the UI
 * is on top.
 */
class ImGuiFrame {
    public:
        ImGuiFrame() {
            PROFILE_SCOPE("Editor/ImGuiNewFrame");
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
        }

        ~ImGuiFrame() {
            PROFILE_SCOPE("Editor/ImGuiRender");
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

        ImGuiFrame(const ImGuiFrame& other) = delete;
        ImGuiFrame& operator=(const ImGuiFrame& other) = delete;

        ImGuiFrame(ImGuiFrame && other) = delete;
        ImGuiFrame& operator=(ImGuiFrame && other) = delete;
};

void drawToast(EditorState& state, float deltaTime) {
    if (state.toastTimeRemaining <= 0.0f) return;
    state.toastTimeRemaining -= deltaTime;
    if (state.toastTimeRemaining <= 0.0f) {
        state.toastTimeRemaining = 0.0f;
        return;
    }

    // Fade the last 0.4s rather than pop out.
    const float alpha = std::min(1.0f, state.toastTimeRemaining / 0.4f);

    ImVec4 bg;
    switch (state.toastKind) {
        case ToastKind::Error:   bg = EditorStyle::TOAST_ERROR_BG;   break;
        case ToastKind::Warning: bg = EditorStyle::TOAST_WARNING_BG; break;
        default:                 bg = EditorStyle::TOAST_INFO_BG;    break;
    }
    bg.w = 0.95f * alpha;

    // Above the status bar rather than over it.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float pad = EditorStyle::px(12.0f);
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + pad, vp->WorkPos.y + vp->WorkSize.y - pad - EditorStatusBar::height()),
        ImGuiCond_Always,
        ImVec2(0.0f, 1.0f)
    );
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
    ImGui::PushStyleColor(ImGuiCol_Text,     ImVec4(1.0f, 1.0f, 1.0f, alpha));
    const ImGuiWindowFlags toastFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::Begin("##Toast", nullptr, toastFlags);
    ImGui::TextUnformatted(state.toastMessage.c_str());
    ImGui::End();
    ImGui::PopStyleColor(2);
}

// Compared against its last write's inputs, so an unchanged title builds no
// string and calls no GLFW.
void syncWindowTitle(WindowManager& window, const std::string& project, const std::string& path, bool dirty) {
    static std::string s_project;
    static std::string s_path;
    static int         s_dirty = -1;   // neither answer, so the first call writes
    if (project == s_project && path == s_path && static_cast<int>(dirty) == s_dirty) return;
    s_project = project;
    s_path    = path;
    s_dirty   = dirty ? 1 : 0;

    const std::string fname = path.empty() ? "untitled" : std::filesystem::path(path).filename().string();
    const std::string prefix = project.empty() ? std::string() : project + " - ";
    window.setTitle(prefix + fname + (dirty ? " *" : "") + " - vkmEngine");
}
} // namespace

void EditorSystem::applyUiScaleIfMoved(FrameContext& ctx) {
    const UiScale scale = resolveUiScale(ctx.window.getWindowContext(), m_state.prefs.uiScale);
    if (scale.product() == m_appliedUiScale) return;
    applyUiScale(scale);
    m_appliedUiScale = scale.product();
}

void EditorSystem::applyPreferences(FrameContext& ctx) {
    const Preferences& prefs = m_state.prefs;
    m_cameraController.setSettings(prefs.camera);

    WindowManager& window = ctx.window;
    if (m_sceneIO.isPlaying() || !window.isOpen()) return;
    if (window.vsync() != prefs.vsync)     window.setVSync(prefs.vsync);
    if (window.mode() != prefs.windowMode) window.updateMode(prefs.windowMode);

    const bool focused = glfwGetWindowAttrib(window.getWindowContext(), GLFW_FOCUSED) != 0;
    const int  idleCap = prefs.fpsCap > 0 ? std::min(prefs.fpsCap, IDLE_FRAMERATE) : IDLE_FRAMERATE;
    const int  cap     = focused ? prefs.fpsCap : idleCap;
    if (window.framerate() != cap) window.setFramerate(cap);
}

void EditorSystem::pollShaderReload(FrameContext& ctx) {
    m_shaderPollTimer += ctx.clock.getDeltaTime();
    if (m_shaderPollTimer < DISK_POLL_INTERVAL) return;
    m_shaderPollTimer = 0.0f;

    RenderBackend* backend = m_renderSystem.backend();
    if (!backend) return;

    const uint32_t reloaded = backend->reloadChangedShaders();
    if (reloaded > 0) {
        m_state.pushToast(ToastKind::Info, "Reloaded " + std::to_string(reloaded) + " shader(s)");
    }
}

void EditorSystem::pollScriptRebuild(FrameContext& ctx) {
    m_scriptPollTimer += ctx.clock.getDeltaTime();
    if (m_scriptPollTimer < DISK_POLL_INTERVAL) return;
    m_scriptPollTimer = 0.0f;

    // Watched even if it did not load: that is the module whose next build should land.
    const std::filesystem::path& path = m_scriptModule.modulePath();
    if (path.empty()) return;

    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);

    // The first look sets what "unchanged" is, or opening a project would read as a
    // rebuild. A module not built yet is recorded as absent, so its first build differs.
    if (path != m_scriptModulePath) {
        m_scriptModulePath  = path;
        m_scriptModuleStamp = ec ? std::filesystem::file_time_type{} : stamp;
        return;
    }
    if (ec) return;   // mid-write, or no module on disk; the next poll asks again
    if (stamp == m_scriptModuleStamp) {
        m_scriptModuleSettling = {};
        return;
    }
    // A linker moves the stamp in several passes; act once it has held for a poll.
    if (stamp != m_scriptModuleSettling) {
        m_scriptModuleSettling = stamp;
        return;
    }
    m_scriptModuleStamp = stamp;

    if (m_sceneIO.isPlaying()) {
        m_state.pushToast(ToastKind::Info, "Scripts rebuilt - File > Reload Scripts to pick them up");
        return;
    }

    m_state.pushToast(ToastKind::Info, "Scripts rebuilt - reloading");
    m_state.requestScriptReload = true;
}

void EditorSystem::serviceScriptReload(FrameContext& ctx) {
    if (!m_state.requestScriptReload) return;
    m_state.requestScriptReload = false;

    // A reload runs every onStart again, where a game builds its world, so a running
    // session would get a second world beside the first. Stopping restores the scene.
    const bool wasPlaying = m_sceneIO.isPlaying();

    // Stop rebuilds the scene through its serializer, leaving no component set the
    // module created (those hold its code). An edit scene takes the same round trip.
    if (!wasPlaying && !m_sceneIO.captureSnapshot(ctx, m_state)) {
        const char* message =
            "Script reload refused: the scene could not be snapshotted, so the old "
            "module stays loaded";
        m_state.pushToast(ToastKind::Error, message);
        return;
    }
    m_sceneIO.stopPlaySession(ctx, m_state);
    // A failed restore kept the old world, sets and all; the module stays until a Stop succeeds.
    if (m_sceneIO.isPlaying()) return;

    // AudioSystem would see Stop's graph change only after the reload; end the sounds now.
    m_audioSystem.stopEverything();
    if (!m_scriptModule.reload(ctx.scene, m_behaviorSystem, ctx.events)) {
        m_state.pushToast(
            ToastKind::Error,
            "Script reload failed - see the Errors panel. Fix the build and reload again."
        );
        return;
    }

    // A reload drops the wire schema the module registered; rebuilt here, or Project
    // Settings shows the game replicating nothing until the next project open.
    m_scriptModule.setupNetwork(ctx.net);

    // Back into a session as Play does: snapshot, then clock. A snapshot that will
    // not serialize leaves the editor stopped rather than running with no way back.
    if (wasPlaying && m_sceneIO.captureSnapshot(ctx, m_state)) ctx.clock.setPaused(false);

    m_state.pushToast(ToastKind::Info, wasPlaying ? "Reloaded scripts - play restarted" : "Reloaded scripts");
}

void EditorSystem::serviceFrame(FrameContext& ctx) {
    syncWindowTitle(ctx.window, m_state.project.name, m_sceneIO.path(), m_state.sceneDirty);

    m_materialPreviews.onFrameBegin();

    // totalPushed ignores repeats, so a behavior that throws then gets disabled
    // toasts exactly once.
    if (const unsigned long long total = m_errorLog.totalPushed();
            total > m_lastErrorTotal) {
        m_lastErrorTotal = total;
        const auto& recent = m_errorLog.entries();
        if (!recent.empty()) {
            const auto& e = recent.back();
            m_state.pushToast(
                ToastKind::Error,
                "[" + e.category + "] " + e.source + " - see the Errors panel"
            );
        }
    }

    pollShaderReload(ctx);
    pollScriptRebuild(ctx);
    serviceScriptReload(ctx);
    m_build.update(m_state, m_scriptModule.modulePath());

    // Before any UI reads the selection.
    m_state.pruneSelection(ctx.scene);

    // Put a titlebar close through the guard, which raises it again once the scene is safe.
    if (ctx.window.shouldClose()) {
        ctx.window.cancelClose();
        m_state.requestSceneAction(EditorState::SceneAction::Quit);
    }
}

void EditorSystem::drawUnsavedChangesDialog(FrameContext& ctx) {
    bool want = m_state.pendingAction != EditorState::SceneAction::None
        && m_state.actionStage == EditorState::ActionStage::Ask;

    if (beginDialog("Unsaved Changes", want)) {
        ImGui::TextUnformatted("This scene has unsaved changes.");
        ImGui::Spacing();
        const char* shownPath = m_sceneIO.path().empty() ? "(untitled scene)" : m_sceneIO.path().c_str();
        ImGui::TextDisabled("%s", shownPath);

        switch (dialogButtons(want, "Save", "Don't Save")) {
            case DialogResult::Confirm:
                // The save is for the authored scene Stop puts back, and a save
                // inside a session is refused, so end the session first.
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

    // Dismissed unanswered: drop the request, or the prompt reopens with no way out.
    if (!want && m_state.pendingAction != EditorState::SceneAction::None
            && m_state.actionStage == EditorState::ActionStage::Ask) {
        m_state.clearSceneAction();
    }
}

void EditorSystem::drawHiddenHint() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float pad = EditorStyle::px(10.0f);

    // Fits the longest KeyLabel plus the sentence around it.
    char hint[80];
    snprintf(
        hint,
        sizeof(hint),
        "Press %s to show editor",
        keyLabel(m_state.prefs.keybinds.toggleEditor).buf
    );

    const ImVec2 corner(vp->WorkPos.x + vp->WorkSize.x - pad, vp->WorkPos.y + vp->WorkSize.y - pad);
    ImGui::SetNextWindowPos(corner, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
    const ImGuiWindowFlags hintFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::Begin("##EditorHiddenHint", nullptr, hintFlags);
    ImGui::TextDisabled("%s", hint);
    ImGui::End();
    ImGui::PopStyleColor();
}

InputSignals EditorSystem::inputSignals(const FrameContext& ctx) const {
    InputSignals signals;
    signals.popupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup);
    if (m_editorVisible) {
        signals.viewportHovered = m_viewportHovered;
        signals.overlayHovered  = m_viewportToolbar.isHovered() || m_playbar.isHovered()
            || m_viewportOverlay.isHovered();
        signals.gizmoHovered    = m_gizmoOverlay.isGizmoOver();
        signals.gizmoDragging   = m_gizmoOverlay.isGizmoUsing();
    } else {
        // Hidden, the scene is the whole window, under nothing but a modal.
        signals.viewportHovered = !signals.popupOpen;
    }
    signals.gameUIHovered  = ctx.ui && ctx.ui->pointerTarget;
    signals.cursorCaptured = ctx.window.cursorMode() == CursorMode::Disabled;
    signals.playing        = m_sceneIO.isPlaying();
    signals.ejected        = m_sceneIO.isEjected();
    signals.typing         = ImGui::GetIO().WantTextInput;
    signals.rebinding      = m_preferences.isCapturingKey();
    return signals;
}

void EditorSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("EditorSystem");

    applyUiScaleIfMoved(ctx);
    applyPreferences(ctx);

    EditorContext ec = makeContext(ctx);

    serviceFrame(ctx);
    resolveSceneAction(ec);

    // After the game's systems, and not while the editor's view holds the cursor.
    if (!m_cameraController.isLooking()) m_sceneIO.holdEjectedCursorFree(ctx);

    ec.input = resolveInputOwnership(inputSignals(ctx));
    // A grabbed cursor's click is the fly camera's or the game's, never a panel's.
    ImGuiIO& io = ImGui::GetIO();
    if (ec.input.pointer == PointerOwner::Captured) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    else                                            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    // Nor are the game's keys a panel's: arrows would navigate the last panel
    // touched, and Space press what they land on.
    if (ec.input.gameHasKeyboard()) io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    else                            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // While a session shows the game's camera, the right button and cursor are the game's.
    m_cameraController.setActive(!ec.input.gameHasViewport());

    // The editor-toggle keybind is read inside the frame, where ImGui's key state is
    // this frame's, and before the hidden branch, so it works shown or hidden.
    const ImGuiFrame imguiFrame;

    // A splash covers the whole surface; nothing is worth submitting panels over.
    if (ctx.splash && ctx.splash->isShowing()) return;

    // Before the hidden branch too: these leave a session whose game has the keyboard
    // and the cursor.
    if (ec.input.editorHasSessionKeys()) {
        m_playbar.processKeys(ec, m_sceneIO);
        if (isPressed(m_state.prefs.keybinds.toggleEditor, false)) {
            m_editorVisible = !m_editorVisible;
            // Close the undo step a held drag was merging into; the hidden path never
            // reaches the gesture boundary below.
            if (!m_editorVisible) m_state.commands.endGesture();
        }
    }

    // Drawn shown or hidden: a hidden prompt has no way out, and hidden failure
    // feedback is lost.
    drawUnsavedChangesDialog(ctx);
    drawToast(m_state, ctx.clock.getDeltaTime());

    ctx.chrome.setCapture(ec.input.hostHoldsPointer(), ec.input.hostHoldsKeyboard());
    m_cameraController.setCapture(ec.input.panelsHoldPointer(), ec.input.panelsHoldKeyboard());

    if (!m_editorVisible) {
        // Next frame fills the whole window, not the stale viewport sub-rect.
        ctx.chrome.setViewport(0, 0, 0, 0);
        drawHiddenHint();
        return;
    }

    if (ec.input.editorHasKeys()) EditorShortcuts::process(ec, m_sceneIO);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    // Hosts the menu bar, dockspace and status bar, but is no window of the layout:
    // nothing docks into it, it never comes to the front, and it is not in the ini.
    ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_MenuBar;

    // Square, or the theme's rounding leaves triangular gaps in the menu bar's corners.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    bool workspace = false;
    if (ImGui::Begin("##Editor", nullptr, rootFlags)) {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);

        PROFILE_SCOPE("Editor/Panels");
        m_menuBar.draw(ec, m_sceneIO);
        m_newProject.draw(m_state);
        m_openProject.draw(m_state);
        m_modelImport.draw(ctx.scene, ctx.resources, m_state);
        m_placePrefab.draw(ctx.scene, ctx.resources, m_state);
        workspace = m_state.projectOpen;
        if (workspace) drawWorkspace(ec);
        else           m_startScreen.draw(ec);

    } else {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }
    ImGui::End();

    // Only beside the dockspace they dock into; without one this frame they would float.
    if (workspace) drawPanels(ec);

    // With no workspace - New Project from the start screen - a run shows in a window of its own.
    if (!workspace && !m_build.idle()) {
        const ImVec2 size(EditorStyle::px(720.0f), EditorStyle::px(320.0f));
        ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Build##start")) m_build.draw(m_state);
        ImGui::End();
    }

    // Floating windows, drawn after the root so they stack on top.
    if (m_state.showPreferences) {
        PROFILE_SCOPE("Panel/Preferences");
        m_preferences.draw(ec);
    }
    if (m_state.showRenderSettings) {
        PROFILE_SCOPE("Panel/RenderSettings");
        m_renderSettings.draw(ec);
    }
    if (m_state.showProjectSettings) {
        PROFILE_SCOPE("Panel/ProjectSettings");
        m_projectSettings.draw(ec);
    }

    // The gesture boundary, after every panel could push. A gizmo drag holds the mouse
    // with no item active; a keyboard-tweaked slider stays active with the mouse up.
    if (!ImGui::IsAnyMouseDown() && !ImGui::IsAnyItemActive()) {
        m_state.commands.endGesture();
    }
}

namespace {

/**
 * @brief Begin one of the workspace's dockable panels.
 *
 * In the panel colour and square, so a docked panel reads as part of the frame around
 * the viewport: rounded, the corners it shares with the workspace's would show whatever
 * the backbuffer last held, a different thing in each of the two. Padding and rounding
 * are popped after Begin, so a popup opened inside gets the theme's.
 * The caller ends the window whatever this returns. No focus on appearing:
 * a group shown again would open on whichever window began last, not the tab it was
 * left on.
 *
 * @param name    The window's name, one of dock_layout.h's.
 * @param padding The window's padding, in screen pixels.
 * @return Whether the window is showing - not a tab behind another.
 */
bool beginPanel(const char* name, ImVec2 padding) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_ChildBg));
    const bool showing = ImGui::Begin(
        name,
        nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing
    );
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    return showing;
}

} // namespace

void EditorSystem::drawWorkspace(EditorContext& ec) {
    const ImGuiID dockspace = ImGui::GetID("##Workspace");

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 size(avail.x, std::max(1.0f, avail.y - EditorStatusBar::height()));

    if (m_state.requestResetLayout || !hasLayout(dockspace)) {
        m_state.requestResetLayout = false;
        buildDefaultLayout(dockspace, size.x, size.y);
    }

    // No item spacing, or the status bar is pushed past the bottom edge.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::DockSpace(dockspace, size);
    {
        PROFILE_SCOPE("Panel/StatusBar");
        EditorStatusBar::draw(ec);
    }
    ImGui::PopStyleVar();
}

void EditorSystem::drawPanels(EditorContext& ec) {
    const ImVec2 sidePad(EditorStyle::px(6.0f), EditorStyle::px(6.0f));
    const ImVec2 panelPad(EditorStyle::px(8.0f), EditorStyle::px(6.0f));

    if (m_state.showHierarchy) {
        PROFILE_SCOPE("Panel/Hierarchy");
        if (beginPanel(HIERARCHY_WINDOW, sidePad)) m_hierarchy.draw(ec, m_sceneIO);
        ImGui::End();
    }

    drawViewport(ec);

    if (m_state.showInspector) {
        if (beginPanel(INSPECTOR_WINDOW, panelPad)) {
            PROFILE_SCOPE("Panel/Inspector");
            m_inspector.draw(ec, m_sceneIO);
        }
        ImGui::End();

        // Answered this frame or not at all: left set, it would take the focus back the
        // next time the window is shown.
        if (m_state.revealMaterial) {
            ImGui::SetNextWindowFocus();
            m_state.revealMaterial = false;
        }
        if (beginPanel(MATERIAL_WINDOW, panelPad)) {
            PROFILE_SCOPE("Panel/MaterialEditor");
            m_materialEditor.draw(ec);
        }
        ImGui::End();
    }

    // A run brings the bottom row and its Build tab forward.
    const bool revealBuild = m_build.takeReveal();
    if (revealBuild) m_state.showAssets = true;

    if (m_state.showAssets) {
        if (beginPanel(ASSETS_WINDOW, panelPad)) {
            PROFILE_SCOPE("Panel/AssetBrowser");
            m_assetBrowser.draw(ec);
        }
        ImGui::End();
        if (beginPanel(ANIMATION_WINDOW, panelPad)) {
            PROFILE_SCOPE("Panel/Animation");
            m_animation.draw(ec, m_sceneIO);
        }
        ImGui::End();
        if (beginPanel(ERRORS_WINDOW, panelPad)) drawErrorsPanel(ec.errorLog);
        ImGui::End();

        dockNextBeside(ERRORS_WINDOW);
        if (revealBuild) ImGui::SetNextWindowFocus();
        if (beginPanel(BUILD_WINDOW, panelPad)) m_build.draw(m_state);
        ImGui::End();
    }
}

void EditorSystem::drawViewport(EditorContext& ec) {
    PROFILE_SCOPE("Panel/Viewport");

    setViewportWindowClass();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const ImGuiWindowFlags viewportFlags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoFocusOnAppearing;
    const bool showing = ImGui::Begin(VIEWPORT_WINDOW, nullptr, viewportFlags);
    ImGui::PopStyleVar();

    // Overlays place themselves in their window's coordinates, so they get one exactly
    // the scene's rect, tab bar or not.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    const ImVec2 vpMin = ImGui::GetCursorScreenPos();
    const ImVec2 vpSize = ImGui::GetContentRegionAvail();
    const ImGuiWindowFlags areaFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (showing && ImGui::BeginChild("##ViewportArea", ImVec2(0, 0), ImGuiChildFlags_None, areaFlags)) {
        ec.viewportPos  = vpMin;
        ec.viewportSize = vpSize;
        // Next frame renders to this rect; ImGui's screen coords become framebuffer pixels.
        const float vpScale = ec.frame.window.framebufferScale();
        ec.frame.chrome.setViewport(
            static_cast<uint32_t>(std::max(0.0f, vpMin.x * vpScale)),
            static_cast<uint32_t>(std::max(0.0f, vpMin.y * vpScale)),
            static_cast<uint32_t>(std::max(1.0f, vpSize.x * vpScale)),
            static_cast<uint32_t>(std::max(1.0f, vpSize.y * vpScale))
        );
        m_viewportHovered = nextViewportHover(
            m_viewportHovered,
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows),
            ec.input
        );

        // A viewport too small to hold the axes clear of the strips draws none.
        const float inset      = EditorStyle::overlayInset();
        const float needWidth  = inset + ViewportToolbar::toolStripWidth()
            + EditorStyle::overlayGroupGap() + ViewportOverlay::reach();
        const float needHeight = ViewportToolbar::viewBarBottom()
            + EditorStyle::overlayGroupGap() + ViewportOverlay::reach();
        const bool  axesFit    = vpSize.x >= needWidth && vpSize.y >= needHeight;
        m_viewportOverlay.drawNoCameraNotice(ec);
        m_viewportOverlay.drawNavigationGizmo(ec, axesFit && m_cameraController.isActive());
        m_gizmoOverlay.drawLightGizmos(ec);
        m_gizmoOverlay.drawCameraGizmos(ec);
        m_gizmoOverlay.drawProbeGizmos(ec);
        m_gizmoOverlay.drawEffectGizmos(ec);
        m_gizmoOverlay.drawAudioGizmos(ec);
        if (m_state.showColliders) {
            m_gizmoOverlay.drawColliderGizmos(ec);
            m_gizmoOverlay.drawJointGizmos(ec);
        }
        if (m_state.showBounds)    m_gizmoOverlay.drawBoundsGizmos(ec);
        if (m_state.showSkeletons) m_gizmoOverlay.drawSkeletonGizmos(ec);
        m_gizmoOverlay.drawSelectionOutline(ec);
        m_gizmoOverlay.drawTransformGizmo(ec);
        m_viewportToolbar.draw(ec);
        m_viewportToolbar.drawViewBar(ec);
        m_playbar.draw(
            ec,
            m_sceneIO,
            inset + ViewportToolbar::toolStripWidth() + EditorStyle::overlayGroupGap(),
            vpSize.x - inset - ViewportToolbar::viewBarWidth() - EditorStyle::overlayGroupGap()
        );
        m_gizmoOverlay.handleViewportPick(ec);
    } else {
        m_viewportHovered = nextViewportHover(m_viewportHovered, false, ec.input);
    }
    if (showing) ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
}

} // namespace Vkm::Engine
