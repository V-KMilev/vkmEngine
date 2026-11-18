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

#include "vkm_log/logger.h"

#include "vkm_platform/glfw_api.h"

#include "core/host_chrome.h"
#include "core/clock.h"
#include "base/math/rotation.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/scene/world_transform.h"
#include "core/frame_context.h"
#include "debug/error/engine_error_log.h"
#include "base/profile/profiler.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "chrome/dock_layout.h"
#include "chrome/editor_status_bar.h"
#include "session/asset/file_import.h"
#include "editor_context.h"
#include "editor_settings.h"
#include "input/editor_keybinds.h"
#include "editor_shortcuts.h"
#include "ui/style/editor_style.h"
#include "ui/editor_dialogs.h"
#include "ui/style/editor_icons.h"
#include "vkm_platform/window/window_manager.h"
#include "system/audio/audio_system.h"
#include "view/camera_controller_system.h"
#include "core/frame/loading_frame.h"
#include "system/loading/loading_system.h"
#include "core/frame/splash_frame.h"
#include "core/frame/ui_draw_data.h"
#include "system/render/editor_render_hooks.h"
#include "system/render/render_system.h"
#include "system/script/behavior_system.h"
#include "system/script/module/script_module.h"
#include "io/project/roots.h"
#include "session/project/project_controller.h"
#include "overlays/overlay_strip.h"
#include "overlays/terrain_tool.h"
#include "overlays/viewport_notices.h"
#include "actions/asset_drop.h"

namespace Vkm::Engine {

namespace {

/// The roots an effect preview names when the selection does not decide what plays.
const std::vector<EntityId> NO_ROOTS;

/**
 * @brief Load the text, heading and icon fonts at their unscaled design size.
 *
 * ImGui rasterizes a glyph at the size it is drawn, so the scale factors apply per frame. A failure
 * leaves the ImGui default font and square icon placeholders.
 */
void buildEditorFonts() {
    ImGuiIO& io = ImGui::GetIO();

    // The first font added is the one text is set in.
    static std::string s_textPath    = (Roots::engineFonts() / "Roboto-Regular.ttf").string();
    static std::string s_headingPath = (Roots::engineFonts() / "Roboto-Medium.ttf").string();
    if (!io.Fonts->AddFontFromFileTTF(s_textPath.c_str(), EditorStyle::REFERENCE_FONT_SIZE)) {
        LOG_WARNING("Editor font %s failed to load; using the ImGui default", s_textPath.c_str());
    }
    ImFont* heading = io.Fonts->AddFontFromFileTTF(s_headingPath.c_str(), EditorStyle::REFERENCE_FONT_SIZE);
    if (!heading) {
        LOG_WARNING("Heading font %s failed to load; headings take the text font", s_headingPath.c_str());
    }
    EditorStyle::setHeadingFont(heading);

    static std::string s_iconPath =
        (Roots::engineFonts() / "lucide.ttf").string();
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
 * The theme is re-applied, as ImGui's own metrics are pixels that do not follow the font; what the
 * editor draws itself follows it through EditorStyle::px().
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
    Platform::WindowManager& window,
    CameraControllerSystem& cameraController,
    RenderSystem& renderSystem,
    AudioSystem& audioSystem,
    LoadingSystem& loadingSystem,
    BehaviorSystem& behaviorSystem,
    ScriptModule& scriptModule
)
    : m_cameraController(cameraController)
    , m_renderSystem(renderSystem)
    , m_hooks(requireEditorHooks(renderSystem))
    , m_audioSystem(audioSystem)
    , m_loadingSystem(loadingSystem)
    , m_behaviorSystem(behaviorSystem)
    , m_scriptModule(scriptModule)
    , m_previews(m_hooks)
    , m_session(cameraController, m_previews, behaviorSystem, loadingSystem, m_play)
    , m_scripts(scriptModule, behaviorSystem)
{
    // A game's restart is Stop and Play, at a point of the frame that may replace the world.
    behaviorSystem.setRestartHandler([this] { m_state.requests.ask(Ask::RestartPlay); });
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // A panel may leave the window; the scene's viewport stays in the main one (drawViewport).
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    // Windows move by title bar only, so a drag on the content stays with the content.
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // Static: ImGui keeps the c_str. User root: docs/reference/io.md, "Which root owns a path".
    static std::string s_iniPath = (Roots::userRoot() / "imgui.ini").string();
    io.IniFilename = s_iniPath.c_str();
    // Otherwise ImGui writes a log into the working directory.
    io.LogFilename = nullptr;

    buildEditorFonts();
    // EditorSettings::loadUser runs in init(), so this is the display's scale; update() adds the user's.
    const UiScale scale = resolveUiScale(window.getWindowContext(), m_state.prefs.uiScale);
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
        m_session.rememberView(m_state);
        EditorSettings::save(m_state, m_frame->chrome);
        // A close that skipped the unsaved-changes prompt - an interrupt - keeps its copy.
        if (!m_state.sceneDirty) m_session.discardRecovery();
    } else {
        EditorSettings::saveUser(m_state);
    }
    m_hooks.shutdownImGui();
    ImGui::DestroyContext();
}

void EditorSystem::init(FrameContext& ctx) {
    m_frame = &ctx;
    // Open to edits from the first frame, before update() first says so.
    ctx.chrome.authoring = true;
    // Files dropped on the window are imported (FileImport::importDropped).
    ctx.window.keepDroppedFiles(true);
    EditorContext ec = makeContext(ctx);
    EditorSettings::loadUser(m_state);
    // Without a project, paths resolve against the engine's directory, so the editor shows a picker.
    ProjectController::open(
        ec,
        m_scriptModule,
        Roots::projectRoot().string(),
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
        m_previews,
        m_audioSystem,
        m_loadingSystem,
        m_errorLog,
        m_session,
        m_play,
        {},
        {}
    };
}

namespace {

/**
 * @brief One ImGui frame: NewFrame on the way in, Render and submit on the way out.
 *
 * A scope, as a missed `Render` leaves a half-open frame the next `NewFrame` asserts on. The submit
 * lands after RenderSystem drew the scene, so the UI is on top.
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

// The slots of what the Hierarchy's eye hides, each subtree whole; nothing allocated for none.
std::vector<uint32_t> hiddenSlots(const Scene& scene, const std::vector<EntityId>& hidden) {
    std::vector<uint32_t> slots;
    for (const EntityId root : hidden) {
        if (!scene.isAlive(root)) continue;
        for (const EntityId each : HierarchyOperations::collectSubtree(scene, root)) {
            slots.push_back(each.slot());
        }
    }
    return slots;
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

    Platform::WindowManager& window = ctx.window;
    if (m_play.isPlaying() || !window.isOpen()) return;
    if (window.vsync() != prefs.vsync)     window.setVSync(prefs.vsync);
    if (window.mode() != prefs.windowMode) window.updateMode(prefs.windowMode);

    // A panel dragged out is a window of its own: the editor is in front while any of its windows is.
    bool focused = glfwGetWindowAttrib(window.getWindowContext(), GLFW_FOCUSED) != 0;
    for (const ImGuiViewport* viewport : ImGui::GetPlatformIO().Viewports) {
        auto* own = static_cast<GLFWwindow*>(viewport->PlatformHandle);
        if (!focused && own) focused = glfwGetWindowAttrib(own, GLFW_FOCUSED) != 0;
    }
    const int idleCap = prefs.fpsCap > 0 ? std::min(prefs.fpsCap, IDLE_FRAMERATE) : IDLE_FRAMERATE;
    const int cap     = focused ? prefs.fpsCap : idleCap;
    if (window.framerate() != cap) window.setFramerate(cap);
}

void EditorSystem::syncWindowTitle(Platform::WindowManager& window) {
    const std::string& project = m_state.project.name;
    const std::string& path    = m_session.path();
    const bool dirty = m_state.sceneDirty;
    if (project == m_titleProject && path == m_titlePath && m_titleDirty == dirty) return;
    m_titleProject = project;
    m_titlePath    = path;
    m_titleDirty   = dirty;

    const std::string fname  = path.empty() ? "untitled" : std::filesystem::path(path).filename().string();
    const std::string prefix = project.empty() ? std::string() : project + " - ";
    window.setTitle(prefix + fname + (dirty ? " *" : "") + " - vkmEngine");
}

void EditorSystem::serviceFrame(EditorContext& ec) {
    FrameContext& ctx = ec.frame;
    syncWindowTitle(ctx.window);

    m_previews.onFrameBegin();

    // totalPushed ignores repeats, so a behavior that throws then gets disabled toasts once.
    if (const unsigned long long total = m_errorLog.totalPushed();
            total > m_lastErrorTotal) {
        m_lastErrorTotal = total;
        const auto& recent = m_errorLog.entries();
        if (!recent.empty()) {
            const auto& e = recent.back();
            m_state.toasts.push(ToastKind::Error, "[" + e.category + "] " + e.source, ConsoleView::Errors);
        }
    }

    m_scripts.update(ec);
    m_build.update(m_state, m_scriptModule.modulePath());

    // Before any UI reads the selection.
    m_state.selection.prune(ctx.scene);

    // Put a titlebar close through the guard, which raises it again once the scene is safe.
    if (ctx.window.shouldClose()) {
        ctx.window.cancelClose();
        m_state.sceneActions.request(SceneActions::Kind::Quit);
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
            || m_navigationAxes.isHovered();
        signals.gizmoHovered    = m_gizmoOverlay.isGizmoOver();
        signals.gizmoDragging   = m_gizmoOverlay.isGizmoUsing();
        signals.stroking        = m_terrainTool.stroking();
    } else {
        // Hidden, the scene is the whole window, under nothing but a modal.
        signals.viewportHovered = !signals.popupOpen;
    }
    signals.gameUIHovered = ctx.ui && ctx.ui->pointerTarget;
    signals.cursorCaptured =
        ctx.window.cursorMode() == Platform::CursorMode::Disabled && !ctx.window.cursorFree();
    signals.playing        = m_play.isPlaying();
    signals.ejected        = m_play.isEjected();
    signals.typing         = ImGui::GetIO().WantTextInput;
    signals.rebinding      = m_preferences.isCapturingKey();
    return signals;
}

void EditorSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("EditorSystem");

    applyUiScaleIfMoved(ctx);
    applyPreferences(ctx);

    EditorContext ec = makeContext(ctx);

    serviceFrame(ec);
    m_session.serviceRecovery(ctx, m_state);
    m_console.poll();
    // What puts a world back or opens one waits for here, before the ImGui frame: see editor.md,
    // "The editor's frame".
    if (m_state.requests.take(Ask::StopPlay)) m_session.stopPlaySession(ctx, m_state);
    m_state.sceneActions.resolve(ec, m_scriptModule);
    if (m_state.requests.take(Ask::RestartPlay)) m_playbar.restart(ec);

    // After the game's systems, and not while the editor's view holds the cursor.
    if (!m_cameraController.isNavigating()) m_play.holdEjectedCursorFree(ctx);

    ec.input = resolveInputOwnership(inputSignals(ctx));
    // A grabbed cursor's click is the fly camera's or the game's, never a panel's.
    ImGuiIO& io = ImGui::GetIO();
    if (ec.input.pointer == PointerOwner::Captured) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    else                                            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    // Nor are the game's keys a panel's: arrows would navigate the last panel touched, Space press it.
    if (ec.input.gameHasKeyboard()) io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    else                            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // While a session shows the game's camera, the right button and cursor are the game's.
    m_cameraController.setActive(!ec.input.gameHasViewport());
    if (m_cameraController.isActive()) {
        // What the viewport shows: the editor's viewpoint, or the scene camera looked through.
        const EditorViewpoint& view = m_cameraController.viewpoint();
        const EntityId through = m_cameraController.lookingThrough();
        const Transform* lens = ctx.scene.tryGet<Transform>(through);
        ViewAim& aim = m_state.viewAim;
        aim.rotation = lens ? resolvedWorldRotation(ctx.scene, through, *lens)
                            : Math::fromYawPitch(view.yaw, view.pitch);
        const glm::vec3 from = lens ? resolvedWorldPosition(ctx.scene, through, *lens) : view.position;
        aim.ray      = {from, Math::computeForward(aim.rotation)};
        aim.focus    = m_cameraController.focusDistance();
        aim.known    = true;
    }

    // No panels and no ImGui frame under a splash or a full loading cover: ImGui's first frame builds
    // its program, which would wait behind the renderer's compiles and stall the splash.
    if (ctx.splash && ctx.splash->isShowing()) return;
    const float covered = ctx.loading ? ctx.loading->opacity : 0.0f;
    if (covered >= 1.0f) {
        ctx.chrome.viewport = {0, 0, 0, 0};
        return;
    }

    // The editor-toggle keybind is read inside the frame, where ImGui's key state is this frame's,
    // and before the hidden branch, so it works shown or hidden.
    const ImGuiFrame imguiFrame(m_hooks);
    // The Alt an orbit is held with is not a tap that moves the keyboard to the menu bar.
    if (m_cameraController.isOrbiting()) ImGui::SetKeyOwner(ImGuiMod_Alt, ImGui::GetID("##orbit"));
    ImGui::GetStyle().Alpha = 1.0f - covered;

    // Before the hidden branch too: these leave a session whose game has the keyboard and the cursor.
    if (ec.input.editorHasSessionKeys()) {
        m_playbar.processKeys(ec);
        if (isPressed(m_state.prefs.keybinds.toggleEditor, false)) {
            m_editorVisible = !m_editorVisible;
            // Close the undo step a held drag merges into; the hidden path skips the gesture boundary.
            if (!m_editorVisible) m_state.commands.endGesture();
        }
    }
    if (m_state.requests.take(Ask::HideEditor)) {
        m_editorVisible = false;
        m_state.commands.endGesture();
    }

    // Fixed meshes are kept only while the game runs undisturbed (HostChrome::authoring).
    const bool running = m_play.isPlaying() && !ctx.clock.isPaused() && !m_play.isEjected();
    ctx.chrome.authoring = !running || m_editorVisible;
    // Outside a session only: a running world plays its effects itself.
    const EffectPreviewState& fx = m_state.effects;
    const bool previewing = fx.playing && !m_play.isPlaying();
    // The selection only when it decides what plays: a copy a frame otherwise.
    const bool rooted = previewing && !fx.all;
    const std::vector<EntityId>& roots = rooted ? m_state.selection.entities() : NO_ROOTS;
    ctx.chrome.effectPreview = {previewing, fx.paused, fx.all, roots, fx.restarts};
    ctx.chrome.hiddenInView  = hiddenSlots(ctx.scene, m_state.hiddenInView);

    // Drawn shown or hidden: a hidden prompt has no way out, and hidden failure
    // feedback is lost. A Save the prompt answers may ask for a name.
    m_state.sceneActions.drawDialog(ec);
    m_session.drawDialogs(ctx, m_state);
    if (const std::optional<ConsoleView> view = m_state.toasts.draw(ctx.clock.getDeltaTime())) {
        m_console.show(*view);
        m_state.requests.ask(Ask::RevealConsole);
    }

    ctx.chrome.capturePointer  = ec.input.hostHoldsPointer();
    ctx.chrome.captureKeyboard = ec.input.hostHoldsKeyboard();
    m_cameraController.setCapture(ec.input.panelsHoldPointer(), ec.input.panelsHoldKeyboard());

    if (!m_editorVisible) {
        // Next frame fills the whole window, not the stale viewport sub-rect, and holds no gizmo.
        ctx.chrome.viewport = {0, 0, 0, 0};
        m_gizmoOverlay.showWorldGizmos(ec, false);
        drawHiddenHint();
        m_state.sceneActions.pauseForResolve(ec);
        return;
    }

    if (ec.input.editorHasKeys()) EditorShortcuts::process(ec);
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
        m_menuBar.draw(ec, m_build);
        m_newProject.draw(m_state);
        m_openProject.draw(m_state);
        m_modelImport.draw(ctx.scene, ctx.resources, m_state);
        // Files dropped on the window are brought into the project and imported by kind.
        const std::vector<std::string> dropped = ctx.window.takeDroppedFiles();
        if (!dropped.empty() && m_state.projectOpen) {
            FileImport::importDropped(dropped, ctx.scene, ctx.resources, m_state);
        } else if (!dropped.empty()) {
            m_state.toasts.push(ToastKind::Info, "Open a project to import files into it");
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
        if (ImGui::Begin("Build##start")) m_build.draw();
        ImGui::End();
    }

    // Floating windows, drawn after the root so they stack on top.
    if (m_state.showPreferences) {
        PROFILE_SCOPE("Panel/Preferences");
        m_preferences.draw(ec);
    }
    if (m_state.showRenderSettings) {
        PROFILE_SCOPE("Panel/RenderSettings");
        m_renderSettings.draw(ec, m_play.isPlaying());
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
    // Once nothing more can ask this frame, as at the hidden branch's return.
    m_state.sceneActions.pauseForResolve(ec);
}

namespace {

/**
 * @brief Begin one of the workspace's dockable panels.
 *
 * Square and in the panel color, so a docked panel reads as part of the frame around the viewport.
 * Its style is popped after Begin, so a popup inside gets the theme's; no focus on appearing, so a
 * group shown again opens on the tab it was left on. The caller ends the window whatever this returns.
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

    if (m_state.requests.take(Ask::ResetLayout) || !hasLayout(dockspace)) {
        buildDefaultLayout(dockspace, size.x, size.y);
    }

    // No item spacing, or the status bar is pushed past the bottom edge.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::DockSpace(dockspace, size);
    {
        PROFILE_SCOPE("Panel/StatusBar");
        EditorStatusBar::draw(ec, m_console);
    }
    ImGui::PopStyleVar();
}

void EditorSystem::drawPanels(EditorContext& ec) {
    const ImVec2 sidePad(EditorStyle::px(6.0f), EditorStyle::px(6.0f));
    const ImVec2 panelPad(EditorStyle::px(8.0f), EditorStyle::px(6.0f));

    // Before the viewport's brush reads it.
    followTerrainSelection(m_state, ec.frame.scene);

    if (m_state.workspace.showHierarchy) {
        PROFILE_SCOPE("Panel/Hierarchy");
        if (beginPanel(HIERARCHY_WINDOW, sidePad)) m_hierarchy.draw(ec);
        ImGui::End();
    }

    drawViewport(ec);

    if (m_state.workspace.showInspector) {
        if (beginPanel(INSPECTOR_WINDOW, panelPad)) {
            PROFILE_SCOPE("Panel/Inspector");
            m_inspector.draw(ec);
        }
        ImGui::End();
    }

    if (const std::optional<AssetToEdit> asked = m_state.requests.take<AssetToEdit>()) {
        m_assetEditors.open(asked->type, asked->key, ec.frame.resources);
    }
    {
        PROFILE_SCOPE("Panel/AssetEditors");
        m_assetEditors.draw(ec);
    }

    const bool revealTerrain = m_state.requests.take(Ask::RevealTerrain);
    if (m_state.workspace.showTerrain) {
        if (revealTerrain) ImGui::SetNextWindowFocus();
        if (beginPanel(TERRAIN_WINDOW, panelPad)) {
            PROFILE_SCOPE("Panel/Terrain");
            m_terrainEditor.draw(ec);
        }
        ImGui::End();
    }

    // A run brings the bottom row and its Console's Build view forward; asked for, the Console as it is.
    const bool revealBuild   = m_build.takeReveal();
    const bool revealConsole = m_state.requests.take(Ask::RevealConsole) || revealBuild;
    if (revealBuild) m_console.show(ConsoleView::Build);
    if (revealConsole) m_state.workspace.showAssets = true;

    if (m_state.workspace.showAssets) {
        if (beginPanel(ASSETS_WINDOW, panelPad)) {
            PROFILE_SCOPE("Panel/AssetBrowser");
            m_assetBrowser.draw(ec);
        }
        ImGui::End();
        if (beginPanel(ANIMATION_WINDOW, panelPad)) {
            PROFILE_SCOPE("Panel/Animation");
            m_animation.draw(ec);
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

    // While the game holds the view the playbar and the view bar have a band of their own above it, so
    // they cover none of the game's screen: a HUD at the top is the game's. The band is filled, as the
    // frame draws nothing there.
    const float band = ec.input.gameHasViewport() ? OverlayStrip::height() + 2.0f * OverlayStrip::inset()
                                                  : 0.0f;
    if (showing && band > 0.0f) {
        const ImVec2 at    = ImGui::GetCursorScreenPos();
        const float  width = ImGui::GetContentRegionAvail().x;
        const ImU32  fill  = ImGui::GetColorU32(ImGuiCol_WindowBg);
        ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + width, at.y + band), fill);
        m_playbar.draw(ec, 0.0f, width - ViewportToolbar::viewBarWidth() - OverlayStrip::groupGap());
        m_viewportToolbar.drawViewBar(ec);
        ImGui::SetCursorPosY(ImGui::GetCursorStartPos().y + band);
    }

    // Overlays place themselves in their window's coordinates, so they get one exactly
    // the scene's rect, tab bar or not.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    const ImVec2 vpMin  = ImGui::GetCursorScreenPos();
    const ImVec2 vpSize = ImGui::GetContentRegionAvail();
    const ImGuiWindowFlags areaFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (showing && ImGui::BeginChild("##ViewportArea", ImVec2(0, 0), ImGuiChildFlags_None, areaFlags)) {
        ec.viewportPos  = vpMin;
        ec.viewportSize = vpSize;
        // Next frame renders to this rect. ImGui's coords are the desktop's, so the main
        // window's corner comes off before they become its framebuffer's pixels.
        const float vpScale = ec.frame.window.framebufferScale();
        ec.frame.chrome.viewport = {
            static_cast<uint32_t>(std::max(0.0f, (vpMin.x - main->Pos.x) * vpScale)),
            static_cast<uint32_t>(std::max(0.0f, (vpMin.y - main->Pos.y) * vpScale)),
            static_cast<uint32_t>(std::max(1.0f, vpSize.x * vpScale)),
            static_cast<uint32_t>(std::max(1.0f, vpSize.y * vpScale))
        };
        m_viewportHovered = nextViewportHover(
            m_viewportHovered,
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows),
            ec.input
        );

        // A viewport too small to hold the axes clear of the strips draws none.
        const float inset     = OverlayStrip::inset();
        const float needWidth = inset + ViewportToolbar::toolStripWidth()
            + OverlayStrip::groupGap() + NavigationAxes::reach();
        const float needHeight = ViewportToolbar::viewBarBottom()
            + OverlayStrip::groupGap() + NavigationAxes::reach();
        const bool axesFit = vpSize.x >= needWidth && vpSize.y >= needHeight;
        drawNoCameraNotice(ec);
        drawLoadingNotice(ec);
        drawFlySpeed(ec, m_cameraController);
        m_navigationAxes.drawNavigationGizmo(ec, axesFit && m_cameraController.isActive());
        // The shapes the engine draws into the next frame; the markers that pick, here.
        m_gizmoOverlay.showWorldGizmos(ec, true);
        const GizmoVisibility& show = m_state.workspace.gizmos;
        if (show.lights)    m_gizmoOverlay.markers().markLights(ec);
        if (show.cameras)   m_gizmoOverlay.markers().markCameras(ec);
        if (show.probes)    m_gizmoOverlay.markers().markProbes(ec);
        if (show.effects)   m_gizmoOverlay.markers().markEffects(ec);
        if (show.audio)     m_gizmoOverlay.markers().markAudio(ec);
        if (show.colliders) m_gizmoOverlay.markers().markJoints(ec);
        m_gizmoOverlay.markers().drawUIRects(ec);
        m_gizmoOverlay.drawTransformGizmo(ec);
        m_gizmoOverlay.drawUIRectTool(ec);
        m_viewportToolbar.draw(ec);
        if (band == 0.0f) {
            m_viewportToolbar.drawViewBar(ec);
            m_playbar.draw(
                ec,
                inset + ViewportToolbar::toolStripWidth() + OverlayStrip::groupGap(),
                vpSize.x - inset - ViewportToolbar::viewBarWidth() - OverlayStrip::groupGap()
            );
        }
        acceptViewportDrop(ec);
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
