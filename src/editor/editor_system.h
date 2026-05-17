#pragma once

#include <vector>

#include <imgui.h>

#include "core/system.h"
#include "framework/editor_state.h"
#include "framework/editor_panel.h"
#include "framework/scene_io_controller.h"
#include "framework/editor_menu_bar.h"
#include "framework/editor_status_bar.h"
#include "framework/editor_shortcuts.h"
#include "framework/editor_panel_resize.h"
#include "overlays/viewport_overlay.h"
#include "panels/inspector_panel.h"
#include "panels/bottom_panel.h"
#include "panels/hierarchy_panel.h"
#include "overlays/gizmo_overlay.h"
#include "overlays/viewport_toolbar.h"
#include "overlays/playback_bar.h"
#include "panels/preferences_panel.h"
#include "panels/material_editor.h"
#include "panels/asset_browser.h"

struct GLFWwindow;

namespace Engine {

struct EditorContext;
class CameraController;
class EventSystem;
class VisibilitySystem;
class RenderSystem;

class EditorSystem : public System {
    public:
        EditorSystem(
            GLFWwindow* window,
            CameraController* cameraController,
            VisibilitySystem* visibilitySystem,
            RenderSystem* renderSystem,
            EventSystem* events
        );
        ~EditorSystem() override;

        EditorSystem(const EditorSystem& other) = delete;
        EditorSystem& operator=(const EditorSystem& other) = delete;

        EditorSystem(EditorSystem && other) = delete;
        EditorSystem& operator=(EditorSystem && other) = delete;

        void update(FrameContext& ctx) override;

    private:
        /// The root-window panel arrangement: the docked panels, the viewport
        /// (with its overlays), border-resize and the status bar. This is the
        /// shell's own job (driving the panels), so it stays a method here
        /// rather than a unit that would need every panel passed back in.
        void drawWorkspace(EditorContext& ec);

    private:
        GLFWwindow*       m_window           = nullptr;
        CameraController* m_cameraController = nullptr;
        RenderSystem*     m_renderSystem     = nullptr;
        VisibilitySystem* m_visibilitySystem = nullptr;
        EventSystem*      m_events           = nullptr;

        SceneIOController m_sceneIO;
        EditorMenuBar     m_menuBar;
        EditorStatusBar   m_statusBar;
        EditorShortcuts   m_shortcuts;
        EditorPanelResize m_panelResize;

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

        /// Registry of the registered panels (the docked panels and the
        /// Preferences window). Points at the members above; iteration
        /// seam for panel-wide operations. Viewport overlays are drawn
        /// explicitly and are deliberately not in here.
        std::vector<EditorPanel*> m_panels;

        // Input state for F5 toggle
        bool m_f5WasDown = false;
};

} // namespace Engine
