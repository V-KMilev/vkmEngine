#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "core/system.h"
#include "debug/error/engine_error_log.h"
#include "editor_state.h"
#include "session/project/build_controller.h"
#include "preview/preview_cache.h"
#include "session/play_session.h"
#include "session/scene_session.h"
#include "session/project/reload_watcher.h"
#include "chrome/editor_menu_bar.h"
#include "chrome/dialog/model_import_dialog.h"
#include "chrome/dialog/new_project_dialog.h"
#include "chrome/dialog/open_project_dialog.h"
#include "chrome/dialog/place_prefab_dialog.h"
#include "input/input_ownership.h"
#include "overlays/navigation_axes.h"
#include "overlays/gizmo/gizmo_overlay.h"
#include "overlays/viewport_toolbar.h"
#include "overlays/playback_bar.h"
#include "overlays/terrain_tool_overlay.h"
#include "panels/console/console_panel.h"
#include "panels/terrain_editor_panel.h"
#include "panels/hierarchy_panel.h"
#include "panels/inspector/inspector_panel.h"
#include "panels/animation_panel.h"
#include "panels/asset_browser/asset_browser_panel.h"
#include "panels/preferences_panel.h"
#include "asset_editors/asset_editors.h"
#include "panels/project_settings_panel.h"
#include "chrome/start_screen.h"
#include "panels/render_settings_panel.h"

namespace Vkm::Platform {
class WindowManager;
} // namespace Vkm::Platform

namespace Vkm::Engine {

struct EditorContext;
class AudioSystem;
class LoadingSystem;
class BehaviorSystem;
class CameraControllerSystem;
class EditorRenderHooks;
class Scene;
class RenderSystem;
class ScriptModule;

/**
 * @brief Top-level editor System: owns the panel set, the workspace shell and the editor state.
 *
 * Runs at the Editor stage, after Render, so its ImGui frame lands on the scene and its panels read
 * what this frame published. Mutation goes through EditorContext; the panels do not know each other.
 */
class EditorSystem : public System {
    public:
        EditorSystem(
            Platform::WindowManager& window,
            CameraControllerSystem& cameraController,
            RenderSystem& renderSystem,
            AudioSystem& audioSystem,
            LoadingSystem& loadingSystem,
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
         * Through the same ProjectController::open that File > Open Project runs.
         *
         * @param ctx Frame context the project's world is built into.
         */
        void init(FrameContext& ctx) override;

        void update(FrameContext& ctx) override;

        /// Save the settings and tear ImGui down; not in the destructor, as both need a live window.
        void shutdown() override;

    private:
        static constexpr int IDLE_FRAMERATE = 15;  ///< Frames a second, unfocused outside a session.

    private:
        /**
         * @brief Bundle this frame with the editor's own collaborators.
         *
         * @param ctx The frame the panels and the lifecycle act on.
         * @return A context whose viewport rect drawWorkspace fills in later.
         */
        EditorContext makeContext(FrameContext& ctx);

        /**
         * @brief Re-apply the theme when the display or the preference moved.
         *
         * Before the ImGui frame opens, which is laid out against the theme.
         *
         * @param ctx The frame, for the window whose scale is read.
         */
        void applyUiScaleIfMoved(FrameContext& ctx);

        /**
         * @brief Apply the Preferences: the fly camera's feel, and the window's vsync, mode and frame cap.
         *
         * Every frame, against what the window has, so a Preferences edit and a session's end are one
         * path. The window half stands aside during a session; an unfocused editor runs at IDLE_FRAMERATE.
         *
         * @param ctx The frame, for its window.
         */
        void applyPreferences(FrameContext& ctx);

        /**
         * @brief The once-per-frame housekeeping that owes the UI nothing.
         *
         * Runs before any panel is submitted.
         *
         * @param ec The frame being serviced.
         */
        void serviceFrame(EditorContext& ec);

        /**
         * @brief Draw the corner hint naming the key that brings the editor back.
         *
         * The whole UI while the editor is hidden.
         */
        void drawHiddenHint();

        /**
         * @brief Gather what this frame's input ownership is decided from.
         *
         * Read before the ImGui frame opens, so every hover is the last frame's, the one a click was
         * aimed in, and nothing drawn this frame answers a press first.
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
         * @brief Draw the Viewport window: state its rect to the engine and draw the overlays over the scene.
         *
         * The window has no background, so the frame RenderSystem drew shows through.
         * The rect is its content region, in framebuffer pixels on HostChrome and
         * screen pixels on @p ec.
         *
         * @param ec Per-frame editor context; its viewport rect is filled in.
         */
        void drawViewport(EditorContext& ec);

        /**
         * @brief Title the window with the project, the scene's file and its unsaved mark.
         *
         * Writes only when one of them changed, so a steady frame builds no string and calls no GLFW.
         *
         * @param window The editor's window.
         */
        void syncWindowTitle(Platform::WindowManager& window);

    private:
        CameraControllerSystem& m_cameraController;
        RenderSystem&           m_renderSystem;
        EditorRenderHooks&      m_hooks;
        AudioSystem&            m_audioSystem;
        LoadingSystem&          m_loadingSystem;
        BehaviorSystem&         m_behaviorSystem;
        ScriptModule&           m_scriptModule;

        PreviewCache m_previews;

        FrameContext* m_frame = nullptr;  ///< Kept at init for shutdown(), which is handed no frame.

        EngineErrorLog m_errorLog;  ///< The engine's reportError() sink for the editor's lifetime.

        unsigned long long m_lastErrorTotal = 0;  ///< Last seen, so serviceFrame() toasts only new errors.

        float             m_appliedUiScale = 1.0f;  ///< The installed scale: display content scale
                                                    ///< times the user's multiplier.

        bool m_editorVisible = true;  ///< The whole editor UI, toggled by its keybind.

        std::string         m_titleProject;  ///< What syncWindowTitle last wrote the title from.
        std::string         m_titlePath;
        std::optional<bool> m_titleDirty;    ///< Empty until the first write.

        bool m_viewportHovered = false;  ///< Pointer over the scene rect as last drawn (nextViewportHover).

        PlaySession       m_play;
        SceneSession      m_session;
        ReloadWatcher     m_scripts;
        BuildController   m_build;
        EditorMenuBar     m_menuBar;

        ModelImportDialog m_modelImport;
        PlacePrefabDialog m_placePrefab;
        NewProjectDialog  m_newProject;
        OpenProjectDialog m_openProject;

        EditorState          m_state;
        HierarchyPanel       m_hierarchy;
        InspectorPanel       m_inspector;
        AssetBrowserPanel    m_assetBrowser;
        ConsolePanel         m_console;
        AnimationPanel       m_animation;
        NavigationAxes       m_navigationAxes;
        GizmoOverlay         m_gizmoOverlay;
        TerrainToolOverlay   m_terrainTool;
        ViewportToolbar      m_viewportToolbar;
        PlaybackBar          m_playbar;
        PreferencesPanel     m_preferences;
        TerrainEditorPanel   m_terrainEditor;
        AssetEditors         m_assetEditors;  ///< A window for each asset open in its editor.
        RenderSettingsPanel  m_renderSettings;
        ProjectSettingsPanel m_projectSettings;
        StartScreen          m_startScreen;   ///< Drawn instead of the workspace when there is no project.
};

} // namespace Vkm::Engine
