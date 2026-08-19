#pragma once

#include <string>

#include <imgui.h>

#include "core/system.h"
#include "debug/engine_error_log.h"
#include "framework/editor_state.h"
#include "framework/material_preview_session.h"
#include "framework/project_controller.h"
#include "framework/scene_io_controller.h"
#include "framework/editor_menu_bar.h"
#include "framework/editor_status_bar.h"
#include "framework/editor_shortcuts.h"
#include "framework/editor_panel_resize.h"
#include "framework/editor_actions.h"
#include "overlays/viewport_overlay.h"
#include "overlays/gizmo_overlay.h"
#include "overlays/viewport_toolbar.h"
#include "overlays/playback_bar.h"
#include "panels/hierarchy_panel.h"
#include "panels/inspector_panel.h"
#include "panels/bottom_panel.h"
#include "panels/preferences_panel.h"
#include "panels/material_editor_panel.h"
#include "panels/asset_browser_panel.h"
#include "panels/render_settings_panel.h"

struct GLFWwindow;

namespace Engine {

struct EditorContext;
class Engine;
class CameraControllerSystem;
class Scene;
class UISystem;
class VisibilitySystem;
class RenderSystem;
class ScriptModule;

/**
 * @brief Top-level editor System: owns the panel set, the workspace shell,
 *        and the long-lived editor-state.
 *
 * Constructed once at boot with non-owning pointers to the rendering /
 * input / event collaborators it needs.
 *
 * Everything mutating goes through the EditorContext aggregate; the
 * panels themselves are plain classes that don't know about each other.
 */
class EditorSystem : public System {
    public:
        EditorSystem(
            Engine& engine,
            GLFWwindow* window,
            CameraControllerSystem& cameraController,
            UISystem& uiSystem,
            VisibilitySystem& visibilitySystem,
            RenderSystem& renderSystem,
            ScriptModule& scriptModule,
            const std::string& projectName
        );
        ~EditorSystem() override;

        EditorSystem(const EditorSystem& other) = delete;
        EditorSystem& operator=(const EditorSystem& other) = delete;

        EditorSystem(EditorSystem && other) = delete;
        EditorSystem& operator=(EditorSystem && other) = delete;

        void update(FrameContext& ctx) override;

    private:
        // Shader hot reload polls the shader directory on this interval rather
        // than every frame; a save is a human action, so a second of latency is
        // imperceptible and the scan stays off the frame budget.
        static constexpr float SHADER_POLL_INTERVAL = 1.0f;
        float m_shaderPollTimer = 0.0f;

        /**
         * @brief Open the project a menu or dialog chose, after the draw.
         *
         * Deferred because opening rebuilds the scene while the UI that asked is
         * still being drawn, and guarded because it destroys the current scene:
         * an unsaved one prompts first and this runs once that resolves.
         *
         * @param ec Editor context to re-root.
         */
        void openPendingProject(EditorContext& ec);

        /**
         * @brief Execute a guarded destructive scene action once the
         *        unsaved-changes flow resolves it.
         *
         * @param ctx Frame context the action operates on.
         * @param action Which action was confirmed.
         */
        void performSceneAction(FrameContext& ctx, EditorState::PendingSceneAction action);

        void drawWorkspace(EditorContext& ec);

    private:
        Engine&           m_engine;
        CameraControllerSystem& m_cameraController;
        UISystem&         m_uiSystem;
        RenderSystem&     m_renderSystem;
        VisibilitySystem& m_visibilitySystem;
        ScriptModule&     m_scriptModule;

        MaterialPreviewSession m_materialPreviews;

        // The editor owns the recoverable-error log; installed as the engine's
        // reportError() sink for this editor's lifetime (runtime installs none).
        EngineErrorLog m_errorLog;

        /**
         * @brief Last EngineErrorLog total observed, so update() toasts only newly
         * reported errors (not once per frame a disabled behavior lingers).
         */
        unsigned long long m_lastErrorTotal = 0;

        SceneIOController m_sceneIO;
        ProjectController m_project;
        bool              m_notedStartupProject = false;
        EditorMenuBar     m_menuBar;
        EditorStatusBar   m_statusBar;
        EditorShortcuts   m_shortcuts;
        EditorPanelResize m_panelResize;
        EditorActions::ModelImportDialog m_modelImport;
        EditorActions::PlacePrefabDialog m_placePrefab;

        EditorState      m_state;
        HierarchyPanel   m_hierarchy;
        InspectorPanel   m_inspector;
        BottomPanel      m_bottom;
        ViewportOverlay  m_viewportOverlay;
        GizmoOverlay     m_gizmoOverlay;
        ViewportToolbar  m_viewportToolbar;
        PlaybackBar      m_playbar;
        PreferencesPanel m_preferences;
        MaterialEditorPanel m_materialEditor;
        AssetBrowserPanel m_assetBrowser;
        RenderSettingsPanel m_renderSettings;
};

} // namespace Engine
