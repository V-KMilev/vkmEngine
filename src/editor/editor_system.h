#pragma once

#include <imgui.h>

#include "core/system.h"
#include "framework/editor_state.h"
#include "framework/material_preview_session.h"
#include "framework/scene_io_controller.h"
#include "framework/editor_menu_bar.h"
#include "framework/editor_status_bar.h"
#include "framework/editor_shortcuts.h"
#include "framework/editor_panel_resize.h"
#include "framework/editor_actions.h"   // ModelImportDialog
#include "overlays/viewport_overlay.h"
#include "overlays/gizmo_overlay.h"
#include "overlays/viewport_toolbar.h"
#include "overlays/playback_bar.h"
#include "overlays/runtime_settings_overlay.h"
#include "panels/hierarchy_panel.h"
#include "panels/inspector_panel.h"
#include "panels/bottom_panel.h"
#include "panels/preferences_panel.h"
#include "panels/material_editor.h"
#include "panels/asset_browser.h"
#include "panels/environment_inspector.h"

struct GLFWwindow;

namespace Engine {

struct EditorContext;
class CameraController;
class EventSystem;
class Scene;
class VisibilitySystem;
class RenderSystem;

/**
 * @brief Top-level editor System: owns the panel set, the workspace shell,
 *        and the long-lived editor-state.
 *
 * Constructed once at boot with non-owning pointers to the rendering /
 * input / event collaborators it needs. Each update():
 *  - Routes input intent (capture flags) to CameraController.
 *  - Drives the menu bar, status bar, shortcut handler and panel resizer.
 *  - Draws the docked panels and the floating preview/overlay panels.
 *  - Lets the SceneIOController emit any pending Save-As / Load dialogs.
 *
 * Everything mutating goes through the EditorContext aggregate; the
 * panels themselves are plain classes that don't know about each other.
 */
class EditorSystem : public System {
    public:
        EditorSystem(
            GLFWwindow* window,
            CameraController& cameraController,
            VisibilitySystem& visibilitySystem,
            RenderSystem& renderSystem,
            EventSystem& events
        );
        ~EditorSystem() override;

        EditorSystem(const EditorSystem& other) = delete;
        EditorSystem& operator=(const EditorSystem& other) = delete;

        EditorSystem(EditorSystem && other) = delete;
        EditorSystem& operator=(EditorSystem && other) = delete;

        void update(FrameContext& ctx) override;

        /// ImGui's panel draws + ImGui::Render() happen on main in update();
        /// the draw-submission step waits here so it can run on the thread
        /// that holds the rendering backend's context.
        bool hasBackendWork() const override { return true; }
        void executeBackend(FrameContext& ctx) override;

    private:
        /**
         * @brief ImDrawData* stashed by update() for executeBackend() to consume.
         *
         * Pointer is into ImGui's internal allocator and is valid until
         * the next ImGui::NewFrame on main - the engine guarantees
         * executeBackend runs (and finishes) before the next NewFrame
         * via waitForFrame at the top of the next iteration's mutator
         * phase.
         */
        struct ImDrawData* m_pendingDrawData = nullptr;

        /**
         * @brief Lay out the root-window panel arrangement.
         *
         * Drives the docked panels, the viewport (with its overlays),
         * border-resize and the status bar. Kept as a method on the shell
         * (rather than its own unit) so it doesn't need every panel
         * passed back in.
         */
        void drawWorkspace(EditorContext& ec);

        /**
         * @brief Mirror m_state.selectedEntity into a Selected tag component.
         *
         * Runs once per frame after panels settle so RenderView::build can
         * flag drawables without taking an editor dependency. Single-select
         * today; the loop already handles a future multi-select cleanly.
         */
        void syncSelectionTag(Scene& scene);

    private:
        GLFWwindow*       m_window;
        CameraController& m_cameraController;
        RenderSystem&     m_renderSystem;
        VisibilitySystem& m_visibilitySystem;
        EventSystem&      m_events;

        MaterialPreviewSession m_materialPreviews;

        SceneIOController m_sceneIO;
        EditorMenuBar     m_menuBar;
        EditorStatusBar   m_statusBar;
        EditorShortcuts   m_shortcuts;
        EditorPanelResize m_panelResize;
        EditorActions::ModelImportDialog m_modelImport;

        EditorState      m_state;
        HierarchyPanel   m_hierarchy;
        InspectorPanel   m_inspector;
        BottomPanel      m_bottom;
        ViewportOverlay  m_viewportOverlay;
        GizmoOverlay     m_gizmoOverlay;
        ViewportToolbar  m_viewportToolbar;
        ViewportPlaybar  m_playbar;
        RuntimeSettingsOverlay m_runtimeSettings;
        PreferencesPanel m_preferences;
        MaterialEditorPanel m_materialEditor;
        AssetBrowserPanel m_assetBrowser;

        /**
         * @brief Standalone floating Render Settings window.
         *
         * Same EnvironmentInspector the right-side Inspector uses when the
         * Environment entity is selected; this one is opened from View ->
         * Render Settings so the user can edit world-level rendering
         * config without first hunting the Environment entity in the
         * hierarchy.
         */
        EnvironmentInspector m_renderSettingsUI;
};

} // namespace Engine
