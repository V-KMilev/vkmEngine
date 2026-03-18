#pragma once

#include <imgui.h>

#include "core/system.h"
#include "editor_state.h"
#include "panels/viewport_overlay.h"
#include "panels/inspector_panel.h"
#include "panels/bottom_panel.h"
#include "panels/hierarchy_panel.h"
#include "panels/gizmo_overlay.h"

struct GLFWwindow;

namespace Engine {

class CameraController;
class VisibilitySystem;
class RenderSystem;

class EditorSystem : public System {
    public:
        EditorSystem(
            GLFWwindow* window,
            CameraController* cameraController,
            VisibilitySystem* visibilitySystem,
            RenderSystem* renderSystem
        );
        ~EditorSystem() override;

        EditorSystem(const EditorSystem& other) = delete;
        EditorSystem& operator=(const EditorSystem& other) = delete;

        EditorSystem(EditorSystem && other) = delete;
        EditorSystem& operator=(EditorSystem && other) = delete;

        void update(FrameContext& ctx) override;

    private:
        void drawMenuBar(FrameContext& ctx);
        void drawStatusBar(const FrameContext& ctx);

    private:
        GLFWwindow*       m_window           = nullptr;
        CameraController* m_cameraController = nullptr;
        RenderSystem*     m_renderSystem     = nullptr;

        EditorState      m_state;
        HierarchyPanel   m_hierarchy;
        InspectorPanel   m_inspector;
        BottomPanel      m_bottom;
        ViewportOverlay  m_viewportOverlay;
        GizmoOverlay     m_gizmoOverlay;

        // Input state for F5 toggle
        bool m_f5WasDown = false;

        // Panel resize drag state
        bool m_resizingLeft   = false;
        bool m_resizingRight  = false;
        bool m_resizingBottom = false;
};

} // namespace Engine
