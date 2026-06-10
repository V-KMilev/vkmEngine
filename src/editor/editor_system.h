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
#include "panels/hierarchy_panel.h"
#include "panels/inspector_panel.h"
#include "panels/bottom_panel.h"
#include "panels/preferences_panel.h"
#include "panels/material_editor.h"
#include "panels/asset_browser.h"

struct GLFWwindow;

namespace Engine {

struct EditorContext;
class Engine;
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
            Engine& engine,
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

    private:
        /**
         * @brief Lay out the root-window panel arrangement.
         *
         * Drives the docked panels, the viewport (with its overlays),
         * border-resize and the status bar. Kept as a method on the shell
         * (rather than its own unit) so it doesn't need every panel
         * passed back in.
         */
        void drawWorkspace(EditorContext& ec);

    private:
        Engine&           m_engine;
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
        PreferencesPanel m_preferences;
        MaterialEditorPanel m_materialEditor;
        AssetBrowserPanel m_assetBrowser;
};

} // namespace Engine
