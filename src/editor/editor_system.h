#pragma once

#include <imgui.h>

#include "core/system.h"
#include "ecs/entity.h"

struct GLFWwindow;

namespace Engine {

class CameraController;
class VisibilitySystem;
class RenderSystem;
class Scene;
class ResourceManager;

class EditorSystem : public System {
    public:
        EditorSystem(
            GLFWwindow* window,
            CameraController* cameraController,
            VisibilitySystem* visibilitySystem,
            RenderSystem* renderSystem
        );
        ~EditorSystem() override;

        EditorSystem(const EditorSystem&) = delete;
        EditorSystem& operator=(const EditorSystem&) = delete;
        EditorSystem(EditorSystem&&) = delete;
        EditorSystem& operator=(EditorSystem&&) = delete;

        void update(FrameContext& ctx) override;

    private:
        // Layout regions (drawn as BeginChild inside a single fullscreen window)
        void drawMenuBar(FrameContext& ctx);
        void drawHierarchyPanel(FrameContext& ctx);
        void drawViewportOverlay(const FrameContext& ctx);
        void drawInspectorPanel(FrameContext& ctx);
        void drawBottomPanel(FrameContext& ctx);
        void drawStatusBar(const FrameContext& ctx);

        // Hierarchy helpers
        void drawEntityNode(Scene& scene, EntityId entity);
        void drawEntityContextMenu(Scene& scene, EntityId entity);
        void drawCreateEntityMenu(Scene& scene, ResourceManager& resources);

        // Inspector sections
        void drawTransformSection(Scene& scene, EntityId id);
        void drawMeshSection(Scene& scene, EntityId id);
        void drawLightSection(Scene& scene, EntityId id);
        void drawCameraSection(Scene& scene, EntityId id);
        void drawAnimationSection(Scene& scene, EntityId id);
        void drawHierarchySection(const Scene& scene, EntityId id);
        void drawAddComponentMenu(Scene& scene, EntityId id);

        // Settings/Resources (bottom tabs)
        void drawSettingsTab(FrameContext& ctx);
        void drawResourcesTab(const FrameContext& ctx);

        // Navigation gizmo (ImGui DrawList, replaces GL render pass)
        void drawNavigationGizmo(const FrameContext& ctx, ImVec2 regionMin, ImVec2 regionMax);

        // Entity operations
        EntityId createEntity(Scene& scene, ResourceManager& resources, const char* type);
        void duplicateEntity(Scene& scene, EntityId source);
        void deleteEntity(Scene& scene, EntityId entity);

        // Widget helpers
        static bool drawVec3Control(const char* label, float* values,
                                     float resetValue = 0.0f, float speed = 0.1f);
        static void drawPropertyLabel(const char* label);

        const char* getEntityDisplayName(const Scene& scene, EntityId id) const;
        const char* getEntityIcon(const Scene& scene, EntityId id) const;

    private:
        CameraController* m_cameraController = nullptr;
        VisibilitySystem* m_visibilitySystem = nullptr;
        RenderSystem*     m_renderSystem     = nullptr;

        EntityId m_selectedEntity{};

        // Layout dimensions
        float m_leftPanelWidth   = 260.0f;
        float m_rightPanelWidth  = 340.0f;
        float m_bottomPanelHeight = 200.0f;

        // Panel visibility
        bool m_showStats     = true;
        bool m_showHierarchy = true;
        bool m_showInspector = true;
        bool m_showBottom    = true;

        // Frame time graph
        static constexpr int FRAME_HISTORY_SIZE = 240;
        float m_frameTimeHistory[FRAME_HISTORY_SIZE] = {};
        int   m_frameTimeOffset = 0;
        float m_frameTimeMax    = 0.0f;

        // Hierarchy state
        char m_hierarchyFilter[64] = {};

        // Rendering state
        bool m_wireframe = false;
        bool m_viewportHovered = false;
};

} // namespace Engine
