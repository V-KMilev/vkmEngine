#pragma once

#include <filesystem>
#include <string>

#include "core/system.h"
#include "system/render/render_settings.h"
#include "debug/engine_error_log.h"
#include "editor_state.h"
#include "session/material_preview_session.h"
#include "session/scene_io_controller.h"
#include "chrome/editor_menu_bar.h"
#include "chrome/model_import_dialog.h"
#include "chrome/new_project_dialog.h"
#include "chrome/open_project_dialog.h"
#include "chrome/place_prefab_dialog.h"
#include "input/input_ownership.h"
#include "overlays/viewport_overlay.h"
#include "overlays/gizmo_overlay.h"
#include "overlays/viewport_toolbar.h"
#include "overlays/playback_bar.h"
#include "panels/hierarchy_panel.h"
#include "panels/inspector_panel.h"
#include "panels/animation_panel.h"
#include "panels/asset_browser_panel.h"
#include "panels/preferences_panel.h"
#include "panels/material_editor_panel.h"
#include "panels/project_settings_panel.h"
#include "panels/start_screen.h"
#include "panels/render_settings_panel.h"

struct GLFWwindow;

namespace Vkm::Engine {

struct EditorContext;
class AudioSystem;
class BehaviorSystem;
class CameraControllerSystem;
class Scene;
class UISystem;
class RenderSystem;
class ScriptModule;

/**
 * @brief Top-level editor System: owns the panel set, the workspace shell and
 *        the long-lived editor state.
 *
 * Runs at the Editor stage, after Render: its ImGui frame lands on top of the scene
 * and its panels read what this frame published. It holds other systems because it
 * drives the engine rather than taking part in the frame's data flow. Mutation goes
 * through EditorContext; the panels do not know about each other.
 */
class EditorSystem : public System {
    public:
        EditorSystem(
            GLFWwindow* window,
            CameraControllerSystem& cameraController,
            RenderSystem& renderSystem,
            RenderSettings& render,
            AudioSystem& audioSystem,
            BehaviorSystem& behaviorSystem,
            ScriptModule& scriptModule
        );
        ~EditorSystem() override = default;

        EditorSystem(const EditorSystem& other) = delete;
        EditorSystem& operator=(const EditorSystem& other) = delete;

        EditorSystem(EditorSystem && other) = delete;
        EditorSystem& operator=(EditorSystem && other) = delete;

        /**
         * @brief Open the project the host was launched on, before the first frame.
         *
         * The host resolves the root and builds the window; everything scoped to the
         * project is rooted here through the same ProjectController::open that
         * File > Open Project runs.
         *
         * @param ctx Frame context the project's world is built into.
         */
        void init(FrameContext& ctx) override;

        void update(FrameContext& ctx) override;

        /**
         * @brief Save the editor's settings and tear ImGui down.
         *
         * Not in the destructor because both halves need a live window: the settings
         * write reads the render system's state, and the ImGui backends hold a GL
         * context and a GLFW window.
         */
        void shutdown() override;

    private:
        /// Seconds between scans of what a build rewrites.
        static constexpr float DISK_POLL_INTERVAL = 1.0f;
        /// Frames a second while unfocused outside a session.
        static constexpr int   IDLE_FRAMERATE     = 15;

    private:
        /**
         * @brief Bundle this frame with the editor's own collaborators.
         *
         * @param ctx The frame the panels and the lifecycle act on.
         * @return A context whose viewport rect drawWorkspace fills in later.
         */
        EditorContext makeContext(FrameContext& ctx);

        /**
         * @brief Carry the pending scene action one stage on, and run it once
         *        the unsaved-changes guard has cleared it.
         *
         * Called before the ImGui frame opens: New, Open and Open Project rebuild the
         * world, which may not happen with a window still on the ImGui stack.
         *
         * @param ec Editor context the action operates on.
         */
        void resolveSceneAction(EditorContext& ec);

        /**
         * @brief Do the thing the guard cleared.
         *
         * @param ec Editor context the action operates on.
         * @param action Which action was approved.
         * @param payload Its target: a scene file, a project root, or empty.
         */
        void performSceneAction(
            EditorContext& ec,
            EditorState::SceneAction action,
            const std::string& payload
        );

        /**
         * @brief Re-apply the theme when the display or the preference moved.
         *
         * Before the ImGui frame opens, which is laid out against the theme.
         *
         * @param ctx The frame, for the window whose scale is read.
         */
        void applyUiScaleIfMoved(FrameContext& ctx);

        /**
         * @brief Put the Preferences into effect: the fly camera's feel, and
         *        the window's vsync, mode and frame cap.
         *
         * Every frame, against what the window already has, so a Preferences edit and
         * the end of a session are one code path. The window half stands aside during
         * a play session, where the game may set the window up its own way; Stop puts
         * the editor's back. Outside a session an unfocused editor is held to
         * IDLE_FRAMERATE rather than redrawing a scene nobody is looking at.
         *
         * @param ctx The frame, for its window.
         */
        void applyPreferences(FrameContext& ctx);

        /**
         * @brief The once-per-frame housekeeping that owes the UI nothing.
         *
         * Runs before any panel is submitted.
         *
         * @param ctx The frame being serviced.
         */
        void serviceFrame(FrameContext& ctx);

        /**
         * @brief Poll the shader sources and reload what changed.
         *
         * Polled, not watched: a filesystem watcher is a per-platform dependency for
         * what a once-a-second scan answers. Editor-only; a shipped runtime has no
         * shader sources.
         *
         * @param ctx The frame, for its delta.
         */
        void pollShaderReload(FrameContext& ctx);

        /**
         * @brief Notice that `vkm build` has rewritten the gameplay module.
         *
         * A script reload stops the play session (see serviceScriptReload), so this
         * reloads on its own only while nothing is playing, and says so otherwise.
         *
         * @param ctx The frame, for its delta.
         */
        void pollScriptRebuild(FrameContext& ctx);

        /**
         * @brief Rebuild the gameplay module if File > Reload Scripts asked.
         *
         * @param ctx The frame, for the scene behaviors are rebound into and the
         *        session the wire schema is rebuilt on.
         */
        void serviceScriptReload(FrameContext& ctx);

        /**
         * @brief Ask about unsaved changes, and record the answer.
         *
         * Records the answer without acting on it: performing it rebuilds the world, which
         * cannot happen with an ImGui window open, so resolveSceneAction does it.
         *
         * @param ctx The frame, for the play session a Save ends before it writes.
         */
        void drawUnsavedChangesDialog(FrameContext& ctx);

        /**
         * @brief Draw the corner hint naming the key that brings the editor back.
         *
         * The whole UI while the editor is hidden.
         */
        void drawHiddenHint();

        /**
         * @brief Gather what this frame's input ownership is decided from.
         *
         * Read before the ImGui frame opens, so every hover is the last frame's - the
         * frame a click was aimed in - and nothing drawn this frame answers a press first.
         *
         * @param ctx The frame, for the game's UI and the window's cursor.
         * @return The signals resolveInputOwnership takes.
         */
        InputSignals inputSignals(const FrameContext& ctx) const;

        /**
         * @brief Submit the dockspace the panels dock into, and the status bar under it.
         *
         * Builds the default layout when the ini holds no dockspace, and again when
         * Window > Reset Layout asks.
         *
         * @param ec Per-frame editor context.
         */
        void drawWorkspace(EditorContext& ec);

        /**
         * @brief Draw the workspace's dockable windows.
         *
         * After the root window has ended, so each is a window of its own that ImGui
         * places, docked or floating.
         *
         * @param ec Per-frame editor context.
         */
        void drawPanels(EditorContext& ec);

        /**
         * @brief Draw the Viewport window: state its rect to the engine and draw the
         *        overlays over the scene.
         *
         * The window has no background, so the frame RenderSystem drew shows through.
         * The rect is its content region, in framebuffer pixels on HostChrome and
         * screen pixels on @p ec.
         *
         * @param ec Per-frame editor context; its viewport rect is filled in.
         */
        void drawViewport(EditorContext& ec);

    private:
        CameraControllerSystem& m_cameraController;
        RenderSystem&           m_renderSystem;
        RenderSettings&         m_render;
        AudioSystem&            m_audioSystem;
        BehaviorSystem&         m_behaviorSystem;
        ScriptModule&           m_scriptModule;

        MaterialPreviewSession m_materialPreviews;

        /**
         * @brief The recoverable-error log, the engine's reportError() sink for this
         *        editor's lifetime.
         */
        EngineErrorLog m_errorLog;

        /**
         * @brief Last EngineErrorLog total observed, so serviceFrame() toasts only new
         *        errors, not once per frame.
         */
        unsigned long long m_lastErrorTotal = 0;

        float             m_shaderPollTimer = 0.0f;
        float             m_scriptPollTimer = 0.0f;
        /// The module last polled and its write time; another project's module is a first look.
        std::filesystem::path           m_scriptModulePath;
        std::filesystem::file_time_type m_scriptModuleStamp{};
        /// A changed write time seen once, acted on when the next poll sees it unchanged.
        std::filesystem::file_time_type m_scriptModuleSettling{};

        /**
         * @brief The installed UI scale: display content scale times the user's multiplier.
         *
         * Held because re-applying the theme writes every metric in it.
         */
        float             m_appliedUiScale = 1.0f;

        bool m_editorVisible = true;  ///< The whole editor UI, toggled by its keybind

        /// The pointer is over the viewport's scene rect, as last drawn; see nextViewportHover.
        bool m_viewportHovered = false;

        SceneIOController m_sceneIO;
        EditorMenuBar     m_menuBar;

        ModelImportDialog m_modelImport;
        PlacePrefabDialog m_placePrefab;
        NewProjectDialog  m_newProject;
        OpenProjectDialog m_openProject;

        EditorState          m_state;
        HierarchyPanel       m_hierarchy;
        InspectorPanel       m_inspector;
        AssetBrowserPanel    m_assetBrowser;
        AnimationPanel       m_animation;
        ViewportOverlay      m_viewportOverlay;
        GizmoOverlay         m_gizmoOverlay;
        ViewportToolbar      m_viewportToolbar;
        PlaybackBar          m_playbar;
        PreferencesPanel     m_preferences;
        MaterialEditorPanel  m_materialEditor;
        RenderSettingsPanel  m_renderSettings;
        ProjectSettingsPanel m_projectSettings;
        StartScreen          m_startScreen;   ///< Drawn instead of the workspace when there is no project.
};

} // namespace Vkm::Engine
