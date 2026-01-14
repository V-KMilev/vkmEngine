#pragma once

#include "render_pass.h"
#include "gl_shader.h"
#include "gl_mesh.h"

#include <memory>

namespace Core {
    class Context;
}

namespace Engine {

/**
 * @brief Configuration parameters for the OpenGL navigation gizmo rendering pass.
 *
 * - size: The on-screen size of the gizmo, in pixels.
 * - x: The horizontal placement of the gizmo, as a normalized device coordinate (0–1, with 1 at the right edge).
 * - y: The vertical placement of the gizmo, as a normalized device coordinate (0–1, with 0 at the bottom and 1 at the top).
 */
struct NavigationGizmoConfig {
    float scale = 1.0f;     ///< Scale of the gizmo
    float size = 140.0f;    ///< Size of the gizmo in pixels
    float x = 0.96f;        ///< Normalized X screen position (1 = right edge)
    float y = 0.12f;        ///< Normalized Y screen position (0 = bottom, 1 = top)
};

/**
 * @brief An OpenGL render pass that draws a navigation (axis) gizmo for camera orientation.
 *
 * The navigation gizmo typically appears in a fixed corner of the screen and helps users
 * orient themselves by showing the current world axes (X/Y/Z). It renders colored axes and arrowheads.
 */
class GLNavigationGizmoPass : public RenderPass {
    public:
        GLNavigationGizmoPass() = delete;
        ~GLNavigationGizmoPass() = default;

        GLNavigationGizmoPass(const GLNavigationGizmoPass& other) = delete;
        GLNavigationGizmoPass& operator=(const GLNavigationGizmoPass& other) = delete;

        GLNavigationGizmoPass(GLNavigationGizmoPass && other) = delete;
        GLNavigationGizmoPass& operator=(GLNavigationGizmoPass && other) = delete;

        /**
         * @brief Construct the navigation gizmo render pass using the specified shader.
         * @param shader Reference to OpenGL shader program for rendering the gizmo.
         */
        GLNavigationGizmoPass(Core::Shader& shader);

    public:
        /**
         * @brief Respond to framebuffer or window resize.
         *
         * Optionally updates internal state or resources when the display size changes.
         * @param backend The render backend.
         * @param width   New width in pixels.
         * @param height  New height in pixels.
         */
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;

        /**
         * @brief Execute the navigation gizmo rendering pass for the current frame.
         *
         * Draws the axis indicator based on the current camera orientation.
         * @param backend   Reference to the current render backend (should be OpenGL).
         * @param view      Provides scene and camera information.
         * @param resources Provides access to mesh and material assets.
         */
        void execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) override;

    private:
        /**
         * @brief Initialize axis/arrow GPU mesh data and any resources needed by the gizmo.
         *
         * Creates simple meshes for the axis lines and arrowheads (cones).
         */
        void initialize();

    private:
        Core::Shader& m_shader;

        std::unique_ptr<GLMesh> m_navAxisMesh;
        std::unique_ptr<GLMesh> m_navArrowMesh;

        NavigationGizmoConfig m_config;
};

} // namespace Engine

