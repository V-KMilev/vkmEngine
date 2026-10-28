#define VKM_LOG_CATEGORY "EDITOR"

#include "editor_system.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <GLFW/glfw3.h>

#include "logger.h"

#include "core/host_chrome.h"
#include "core/clock.h"
#include "core/math/rotation.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "core/system.h"
#include "debug/engine_error_log.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "chrome/dock_layout.h"
#include "chrome/editor_status_bar.h"
#include "session/file_import.h"
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
#include "system/loading/loading_frame.h"
#include "system/loading/loading_system.h"
#include "system/splash/splash_frame.h"
#include "system/ui/ui_draw_data.h"
#include "system/render/editor_render_hooks.h"
#include "system/render/render_system.h"
#include "system/script/behavior_system.h"
#include "system/script/script_module.h"
#include "ui/editor_theme.h"
#include "io/project_paths.h"
#include "session/project_controller.h"

namespace Vkm::Engine {

namespace {

/// The roots an effect preview names when the selection does not decide what plays.
const std::vector<EntityId> NO_ROOTS;

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

/**
 * @brief The backend's editor hooks; the editor cannot draw itself without them.
 *
 * @param renderSystem Its backend is set before the editor is made.
 * @throws std::runtime_error when the backend offers no editor hooks.
 */
EditorRenderHooks& requireEditorHooks(RenderSystem& renderSystem) {
    EditorRenderHooks* hooks = editorRenderHooks(renderSystem.backend());
    if (!hooks) throw std::runtime_error("The editor needs a render backend with editor hooks");
    return *hooks;
}

} // namespace

EditorSystem::EditorSystem(
    GLFWwindow* window,
    CameraControllerSystem& cameraController,
    RenderSystem& renderSystem,
    RenderSettings& render,
    AudioSystem& audioSystem,
    LoadingSystem& loadingSystem,
    BehaviorSystem& behaviorSystem,
    ScriptModule& scriptModule
)
    : m_cameraController(cameraController)
    , m_renderSystem(renderSystem)
    , m_hooks(requireEditorHooks(renderSystem))
    , m_render(render)
    , m_audioSystem(audioSystem)
    , m_loadingSystem(loadingSystem)
    , m_behaviorSystem(behaviorSystem)
    , m_scriptModule(scriptModule)
    , m_materialPreviews(m_hooks)
    , m_sceneIO(cameraController, m_materialPreviews, behaviorSystem, loadingSystem)
{
    // The editor's scene is held in memory, so a game's restart is Stop and Play, at a point of
    // the frame that may replace the world.
    behaviorSystem.setRestartHandler([this] { m_state.requestRestart = true; });
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // A panel dragged out of the window becomes a window of its own, on any monitor; the scene's
    // viewport stays in the main one, where the renderer draws (drawViewport).
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
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

    m_hooks.initImGui(window);

    setErrorSink(&m_errorLog);

    LOG_INFO("Initialized");
}

void EditorSystem::shutdown() {
    m_behaviorSystem.setRestartHandler(nullptr);
    LOG_TRACE("Shutting down, saving settings");
    setErrorSink(nullptr);
    if (m_state.projectOpen) {
        m_sceneIO.rememberView(m_state);
        EditorSettings::save(m_state, m_render);
        // A close that skipped the unsaved-changes prompt - an interrupt - keeps its copy.
        if (!m_state.sceneDirty) m_sceneIO.discardRecovery();
    } else {
        EditorSettings::saveUser(m_state);
    }
    m_hooks.shutdownImGui();
    ImGui::DestroyContext();
}

void EditorSystem::init(FrameContext& ctx) {
    // Open to edits from the first frame, before update() first says so.
    ctx.chrome.setAuthoring(true);
    // Files dropped on the window are imported (FileImport::importDropped).
    ctx.window.keepDroppedFiles(true);
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
        m_hooks,
        m_materialPreviews,
        m_audioSystem,
        m_loadingSystem,
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
        explicit ImGuiFrame(EditorRenderHooks& hooks)
            : m_hooks(hooks)
        {
            PROFILE_SCOPE("Editor/ImGuiNewFrame");
            m_hooks.beginImGuiFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
        }

        ~ImGuiFrame() {
            PROFILE_SCOPE("Editor/ImGuiRender");
            ImGui::Render();
            m_hooks.renderImGui(ImGui::GetDrawData());
        }

        ImGuiFrame(const ImGuiFrame& other) = delete;
        ImGuiFrame& operator=(const ImGuiFrame& other) = delete;

        ImGuiFrame(ImGuiFrame && other) = delete;
        ImGuiFrame& operator=(ImGuiFrame && other) = delete;

    private:
        EditorRenderHooks& m_hooks;
};

/**
 * @brief Draw the toasts, newest at the foot, above the status bar.
 *
 * They wait while the pointer is over them; a click puts one away, and an error's or a warning's
 * opens the Console it points to.
 *
 * @param state     Holds them, and is asked for the Console.
 * @param console   Shown the view a click asks for.
 * @param deltaTime Seconds since the last frame.
 */
void drawToasts(EditorState& state, ConsolePanel& console, float deltaTime) {
    static bool s_hovered = false;  // last frame's answer: hovered, they wait
    std::vector<EditorState::Toast>& toasts = state.toasts;
    for (EditorState::Toast& toast : toasts) {
        if (!s_hovered) toast.remaining -= deltaTime;
    }
    const auto spent = [](const EditorState::Toast& toast) { return toast.remaining <= 0.0f; };
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), spent), toasts.end());
    if (toasts.empty()) {
        s_hovered = false;
        return;
    }

    // Above the status bar rather than over it.
    const ImGuiViewport* vp  = ImGui::GetMainViewport();
    const float          pad = EditorStyle::px(12.0f);
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + pad, vp->WorkPos.y + vp->WorkSize.y - pad - EditorStatusBar::height()),
        ImGuiCond_Always,
        ImVec2(0.0f, 1.0f)
    );
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    ImGui::Begin("##Toasts", nullptr, flags);
    ImDrawList*       dl = ImGui::GetWindowDrawList();
    ImDrawListSplitter layers;
    layers.Split(dl, 2);
    const ImVec2 inner(EditorStyle::px(10.0f), EditorStyle::px(7.0f));
    // A small step between them: each is its own block.
    const ImVec2 between(ImGui::GetStyle().ItemSpacing.x, EditorStyle::px(5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, between);
    size_t dismissed = toasts.size();
    for (size_t i = 0; i < toasts.size(); ++i) {
        const EditorState::Toast& toast = toasts[i];
        // Fade the last moments rather than pop out.
        const float alpha = std::min(1.0f, toast.remaining / 0.4f);
        ImVec4      back  = EditorStyle::TOAST_INFO_BG;
        EditorIcon  glyph = EditorIcon::Info;
        if (toast.kind == ToastKind::Error) {
            back  = EditorStyle::TOAST_ERROR_BG;
            glyph = EditorIcon::Error;
        } else if (toast.kind == ToastKind::Warning) {
            back  = EditorStyle::TOAST_WARNING_BG;
            glyph = EditorIcon::Warning;
        }
        back.w = 0.95f * alpha;

        layers.SetCurrentChannel(dl, 1);
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(start.x + inner.x, start.y + inner.y));
        ImGui::BeginGroup();
        inlineIcon(glyph, ImGui::GetTextLineHeight(), ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)));
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, alpha));
        ImGui::TextUnformatted(toast.message.c_str());
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        const ImVec2 end(ImGui::GetItemRectMax().x + inner.x, ImGui::GetItemRectMax().y + inner.y);
        ImGui::SetCursorScreenPos(start);
        const std::optional<ConsoleView> opens = toast.opens();
        if (ImGui::InvisibleButton("##toast", ImVec2(end.x - start.x, end.y - start.y))) {
            dismissed = i;
            if (opens) {
                console.show(*opens);
                state.revealConsole = true;
            }
        }
        const char* tip = opens ? "Click to see the Console" : "Click to put away";
        ImGui::SetItemTooltip("%s", tip);
        ImGui::PopID();
        layers.SetCurrentChannel(dl, 0);
        dl->AddRectFilled(start, end, ImGui::GetColorU32(back), EditorStyle::px(6.0f));
    }
    ImGui::PopStyleVar();
    layers.Merge(dl);
    s_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    ImGui::End();
    if (dismissed < toasts.size()) toasts.erase(toasts.begin() + static_cast<std::ptrdiff_t>(dismissed));
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

    // A panel dragged out is a window of its own: the editor is in front while any of its windows is.
    bool focused = glfwGetWindowAttrib(window.getWindowContext(), GLFW_FOCUSED) != 0;
    for (const ImGuiViewport* viewport : ImGui::GetPlatformIO().Viewports) {
        auto* own = static_cast<GLFWwindow*>(viewport->PlatformHandle);
        if (!focused && own) focused = glfwGetWindowAttrib(own, GLFW_FOCUSED) != 0;
    }
    const int  idleCap = prefs.fpsCap > 0 ? std::min(prefs.fpsCap, IDLE_FRAMERATE) : IDLE_FRAMERATE;
    const int  cap     = focused ? prefs.fpsCap : idleCap;
    if (window.framerate() != cap) window.setFramerate(cap);
}

void EditorSystem::pollShaderReload(FrameContext& ctx) {
    m_shaderPollTimer += ctx.clock.getDeltaTime();
    if (m_shaderPollTimer < DISK_POLL_INTERVAL) return;
    m_shaderPollTimer = 0.0f;

    const uint32_t reloaded = m_renderSystem.backend()->reloadChangedShaders();
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
    // A refused snapshot says so in a toast, and the old module stays.
    if (!wasPlaying && !m_sceneIO.captureSnapshot(ctx, m_state, "Script reload")) return;
    m_sceneIO.stopPlaySession(ctx, m_state);
    // A failed restore kept the old world, sets and all; the module stays until a Stop succeeds.
    if (m_sceneIO.isPlaying()) return;

    // AudioSystem would see Stop's new world only after the reload; end the sounds now.
    m_audioSystem.stopEverything();
    // The loading screen's scene may hold the old module's behaviors: dropped, to be built again
    // the next time the screen shows.
    m_loadingSystem.forgetBuiltScreen();
    if (!m_scriptModule.reload(ctx.scene, m_behaviorSystem, ctx.events)) {
        const char* failed = "Script reload failed; fix the build and reload again";
        m_state.pushToast(ToastKind::Error, failed, ConsoleView::Errors);
        return;
    }

    // A reload drops the wire schema the module registered; rebuilt here, or Project
    // Settings shows the game replicating nothing until the next project open.
    m_scriptModule.setupNetwork(ctx.net);

    // Back into a session as Play does: snapshot, then clock. A snapshot that will
    // not serialize leaves the editor stopped rather than running with no way back.
    const bool restarted = wasPlaying && m_sceneIO.snapshotForPlay(ctx, m_state);
    if (restarted) ctx.clock.setPaused(false);

    m_state.pushToast(ToastKind::Info, restarted ? "Reloaded scripts - play restarted" : "Reloaded scripts");
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
            m_state.pushToast(ToastKind::Error, "[" + e.category + "] " + e.source, ConsoleView::Errors);
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
                // Given up, so the copy kept for a crash goes too.
                m_sceneIO.discardRecovery();
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
        signals.stroking        = m_terrainTool.stroking();
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

    // Outside a session the project's settings are the editor's live ones - those held aside
    // while Render Settings previews a tier; in one they hold what Play found, but for the view
    // fields (debug view, grid), which follow the editor so a tier built over them keeps them.
    if (!m_sceneIO.isPlaying()) {
        ctx.projectRender = m_state.renderPreviewHeld ? *m_state.renderPreviewHeld : ctx.render;
    } else {
        EditorSettings::copyViewFields(ctx.render, ctx.projectRender);
    }

    EditorContext ec = makeContext(ctx);

    serviceFrame(ctx);
    m_sceneIO.serviceRecovery(ctx, m_state);
    m_console.poll();
    resolveSceneAction(ec);
    if (m_state.requestRestart) {
        m_state.requestRestart = false;
        m_playbar.restart(ec, m_sceneIO);
    }

    // After the game's systems, and not while the editor's view holds the cursor.
    if (!m_cameraController.isNavigating()) m_sceneIO.holdEjectedCursorFree(ctx);

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
    if (m_cameraController.isActive()) {
        // What the viewport shows: the editor's viewpoint, or the scene camera looked through.
        const EditorViewpoint& view    = m_cameraController.viewpoint();
        const EntityId         through = m_cameraController.lookingThrough();
        const Transform*       lens    = ctx.scene.tryGet<Transform>(through);
        ViewAim& aim = m_state.viewAim;
        aim.rotation = lens ? resolvedWorldRotation(ctx.scene, through, *lens)
                            : Math::fromYawPitch(view.yaw, view.pitch);
        const glm::vec3 from = lens ? resolvedWorldPosition(ctx.scene, through, *lens) : view.position;
        aim.ray      = {from, Math::computeForward(aim.rotation)};
        aim.focus    = m_cameraController.focusDistance();
        aim.known    = true;
    }

    // A splash covers the whole surface; nothing is worth submitting panels over. Nor is the loading
    // screen while it covers a world the editor opened: the panels come in as it lifts. No ImGui
    // frame either: its first builds ImGui's program, which would wait behind every one the driver
    // is still compiling for the renderer, and stop the splash with it.
    if (ctx.splash && ctx.splash->isShowing()) return;
    const float covered = ctx.loading ? ctx.loading->opacity : 0.0f;
    if (covered >= 1.0f) {
        ctx.chrome.setViewport(0, 0, 0, 0);
        return;
    }

    // The editor-toggle keybind is read inside the frame, where ImGui's key state is
    // this frame's, and before the hidden branch, so it works shown or hidden.
    const ImGuiFrame imguiFrame(m_hooks);
    // The Alt an orbit is held with is not a tap that moves the keyboard to the menu bar.
    if (m_cameraController.isOrbiting()) ImGui::SetKeyOwner(ImGuiMod_Alt, ImGui::GetID("##orbit"));
    ImGui::GetStyle().Alpha = 1.0f - covered;

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
    if (m_state.requestHideEditor) {
        m_state.requestHideEditor = false;
        m_editorVisible           = false;
        m_state.commands.endGesture();
    }

    // Fixed meshes are kept only while the game runs undisturbed (HostChrome::setAuthoring).
    const bool running = m_sceneIO.isPlaying() && !ctx.clock.isPaused() && !m_sceneIO.isEjected();
    ctx.chrome.setAuthoring(!running || m_editorVisible);
    // Outside a session only: a running world plays its effects itself.
    const EffectPreviewState& fx = m_state.effects;
    const bool previewing = fx.playing && !m_sceneIO.isPlaying();
    // The selection only when it decides what plays: a copy a frame otherwise.
    const bool rooted = previewing && !fx.all;
    const std::vector<EntityId>& roots = rooted ? m_state.selection : NO_ROOTS;
    ctx.chrome.setEffectPreview({previewing, fx.paused, fx.all, roots, fx.restarts});

    // Drawn shown or hidden: a hidden prompt has no way out, and hidden failure
    // feedback is lost. A Save the prompt answers may ask for a name.
    drawUnsavedChangesDialog(ctx);
    m_sceneIO.drawDialogs(ctx, m_state);
    drawToasts(m_state, m_console, ctx.clock.getDeltaTime());

    ctx.chrome.setCapture(ec.input.hostHoldsPointer(), ec.input.hostHoldsKeyboard());
    m_cameraController.setCapture(ec.input.panelsHoldPointer(), ec.input.panelsHoldKeyboard());

    if (!m_editorVisible) {
        // Next frame fills the whole window, not the stale viewport sub-rect, and holds no gizmo.
        ctx.chrome.setViewport(0, 0, 0, 0);
        m_gizmoOverlay.showWorldGizmos(ec, false);
        drawHiddenHint();
        return;
    }

    if (ec.input.editorHasKeys()) EditorShortcuts::process(ec, m_sceneIO);
    // Said again by the previews and the Console as they draw below, for the next frame's keys.
    m_state.previewHovered   = false;
    m_state.consoleHoldsKeys = false;

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
        // Files dropped on the window: brought into the project, then imported as their kind's
        // Import does.
        const std::vector<std::string> dropped = ctx.window.takeDroppedFiles();
        if (!dropped.empty() && m_state.projectOpen) {
            FileImport::importDropped(dropped, ctx.scene, ctx.resources, m_state);
        } else if (!dropped.empty()) {
            m_state.pushToast(ToastKind::Info, "Open a project to import files into it");
        }
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
        m_renderSettings.draw(ec, m_sceneIO.isPlaying());
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
 * In the panel color and square, so a docked panel reads as part of the frame around
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
        EditorStatusBar::draw(ec, m_console, m_build);
    }
    ImGui::PopStyleVar();
}

void EditorSystem::drawPanels(EditorContext& ec) {
    const ImVec2 sidePad(EditorStyle::px(6.0f), EditorStyle::px(6.0f));
    const ImVec2 panelPad(EditorStyle::px(8.0f), EditorStyle::px(6.0f));

    // Before the viewport's brush reads it.
    followTerrainSelection(m_state, ec.frame.scene);

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
    }

    if (m_state.assetToEdit) {
        m_assetEditors.open(m_state.assetToEdit->type, m_state.assetToEdit->key, ec.frame.resources);
        m_state.assetToEdit.reset();
    }
    {
        PROFILE_SCOPE("Panel/AssetEditors");
        m_assetEditors.draw(ec);
    }

    if (m_state.showTerrain) {
        if (m_state.revealTerrain) ImGui::SetNextWindowFocus();
        if (beginPanel(TERRAIN_WINDOW, panelPad)) {
            PROFILE_SCOPE("Panel/Terrain");
            m_terrainEditor.draw(ec);
        }
        ImGui::End();
    }
    m_state.revealTerrain = false;

    // A run brings the bottom row and its Console's Build view forward; asked for, the Console as it is.
    const bool revealBuild   = m_build.takeReveal();
    const bool revealConsole = revealBuild || m_state.revealConsole;
    m_state.revealConsole    = false;
    if (revealBuild) m_console.show(ConsoleView::Build);
    if (revealConsole) m_state.showAssets = true;

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

        // The tab counts the errors, so they show while another tab is in front.
        const size_t errors = ec.errorLog.entries().size();
        char console[48];
        if (errors > 0) snprintf(console, sizeof(console), "Console (%zu)%s", errors, CONSOLE_WINDOW);
        else            snprintf(console, sizeof(console), "Console%s", CONSOLE_WINDOW);
        if (revealConsole) ImGui::SetNextWindowFocus();
        if (beginPanel(console, panelPad)) m_console.draw(m_state, ec.errorLog, m_build);
        ImGui::End();
    }
}

void EditorSystem::drawViewport(EditorContext& ec) {
    PROFILE_SCOPE("Panel/Viewport");

    setViewportWindowClass();
    // The scene draws into the main window's framebuffer, so its panel cannot leave it.
    const ImGuiViewport* main = ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(main->ID);

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
        // Next frame renders to this rect. ImGui's coords are the desktop's, so the main
        // window's corner comes off before they become its framebuffer's pixels.
        const float vpScale = ec.frame.window.framebufferScale();
        ec.frame.chrome.setViewport(
            static_cast<uint32_t>(std::max(0.0f, (vpMin.x - main->Pos.x) * vpScale)),
            static_cast<uint32_t>(std::max(0.0f, (vpMin.y - main->Pos.y) * vpScale)),
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
        m_viewportOverlay.drawLoadingNotice(ec);
        m_viewportOverlay.drawFlySpeed(ec, m_cameraController);
        m_viewportOverlay.drawNavigationGizmo(ec, axesFit && m_cameraController.isActive());
        // The shapes the engine draws into the next frame; the markers that pick, here.
        m_gizmoOverlay.showWorldGizmos(ec, true);
        const GizmoVisibility& show = m_state.gizmos;
        if (show.lights)    m_gizmoOverlay.markLights(ec);
        if (show.cameras)   m_gizmoOverlay.markCameras(ec);
        if (show.probes)    m_gizmoOverlay.markProbes(ec);
        if (show.effects)   m_gizmoOverlay.markEffects(ec);
        if (show.audio)     m_gizmoOverlay.markAudio(ec);
        if (show.colliders) m_gizmoOverlay.markJoints(ec);
        m_gizmoOverlay.drawUIRects(ec);
        m_gizmoOverlay.drawTransformGizmo(ec);
        m_gizmoOverlay.drawUIRectTool(ec);
        m_viewportToolbar.draw(ec);
        m_viewportToolbar.drawViewBar(ec);
        m_playbar.draw(
            ec,
            m_sceneIO,
            inset + ViewportToolbar::toolStripWidth() + EditorStyle::overlayGroupGap(),
            vpSize.x - inset - ViewportToolbar::viewBarWidth() - EditorStyle::overlayGroupGap()
        );
        AssetBrowserPanel::acceptViewportDrop(ec);
        // A click the brush takes picks nothing.
        if (!m_terrainTool.update(ec)) m_gizmoOverlay.handleViewportPick(ec);
    } else {
        m_viewportHovered = nextViewportHover(m_viewportHovered, false, ec.input);
    }
    if (showing) ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
}

} // namespace Vkm::Engine
