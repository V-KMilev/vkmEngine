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
#include "panels/render_settings_panel.h"

struct GLFWwindow;

namespace Vkm::Engine {

struct EditorContext;
class AudioSystem;
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
            GLFWwindow* window,
            CameraControllerSystem& cameraController,
            UISystem& uiSystem,
            VisibilitySystem& visibilitySystem,
            RenderSystem& renderSystem,
            AudioSystem& audioSystem,
            ScriptModule& scriptModule
        );
        ~EditorSystem() override;

        EditorSystem(const EditorSystem& other) = delete;
        EditorSystem& operator=(const EditorSystem& other) = delete;

        EditorSystem(EditorSystem && other) = delete;
        EditorSystem& operator=(EditorSystem && other) = delete;

        /**
         * @brief Open the project the host was launched on, before the first frame.
         *
         * The editor has one project-open sequence and this is the other way
         * into it: the host resolves the root and builds the window, and
         * everything scoped to the project - its asset library, its editor
         * settings, its gameplay module, its scene - is rooted here through the
         * same ProjectController::open that File > Open Project runs. Written
         * once, so a project-scoped thing added later cannot reach one way in
         * and miss the other.
         *
         * @param ctx Frame context the project's world is built into.
         */
        void init(FrameContext& ctx) override;

        void update(FrameContext& ctx) override;

    private:
        static constexpr float SHADER_POLL_INTERVAL = 1.0f;  ///< Seconds between shader-source scans.

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
         * The one place a destructive action is answered and performed. Called
         * before the ImGui frame opens, because all four rebuild the world and
         * none of them may do that with a window still on the ImGui stack.
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
        void performSceneAction(EditorContext& ec, EditorState::SceneAction action,
                                const std::string& payload);

        void drawWorkspace(EditorContext& ec);

    private:
        CameraControllerSystem& m_cameraController;
        UISystem&         m_uiSystem;
        RenderSystem&     m_renderSystem;
        VisibilitySystem& m_visibilitySystem;
        AudioSystem&      m_audioSystem;
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

        float             m_shaderPollTimer = 0.0f;

        SceneIOController m_sceneIO;
        ProjectController m_project;
        EditorMenuBar     m_menuBar;
        EditorStatusBar   m_statusBar;
        EditorShortcuts   m_shortcuts;
        EditorPanelResize m_panelResize;
        EditorActions::ModelImportDialog m_modelImport;
        EditorActions::PlacePrefabDialog m_placePrefab;
        EditorActions::OpenProjectDialog m_openProject;

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
        RenderSettingsPanel m_renderSettings;
};

} // namespace Vkm::Engine
