#pragma once

#include <cstddef>
#include <cstdint>

namespace Engine {

class CameraController;
class VisibilitySystem;
class RenderSystem;
struct FrameContext;
struct EditorState;

/**
 * @brief Editor bottom panel with tabbed settings, environment, camera, display, keybinds, gizmo, and resources.
 *
 * Provides tabs for rendering settings (wireframe, pass toggles, culling), environment
 * (ambient light, clear color, grid, AABB debug), camera controller, display (fullscreen, VSync),
 * keybind rebinding, gizmo snap config, and cached resource statistics.
 */
class BottomPanel {
    public:
        BottomPanel(CameraController* cam, VisibilitySystem* vis, RenderSystem* render)
            : m_cameraController(cam), m_visibilitySystem(vis), m_renderSystem(render) {}

        void draw(FrameContext& ctx, EditorState& state);

    private:
        void drawRenderingTab(FrameContext& ctx, EditorState& state);
        void drawEnvironmentTab(FrameContext& ctx);
        void drawViewportTab(FrameContext& ctx, EditorState& state);
        void drawEditorTab(EditorState& state);
        void drawStatisticsTab(FrameContext& ctx);

        CameraController* m_cameraController = nullptr;
        VisibilitySystem* m_visibilitySystem = nullptr;
        RenderSystem*     m_renderSystem     = nullptr;

        struct ResourceCounts {
            size_t transforms = 0, meshes = 0, lights = 0, cameras = 0;
            size_t animations = 0, hierarchies = 0, names = 0;
            uint32_t animPlaying = 0, animPaused = 0;
            uint32_t lightsDir = 0, lightsPoint = 0, lightsSpot = 0, lightsDisabled = 0;
            float updateTimer = 0.0f;
        } m_resourceCounts;
};

} // namespace Engine
