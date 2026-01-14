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
 * @brief Configuration parameters for the OpenGL grid rendering pass.
 * 
 * - size: The overall extent of the grid in world units.
 * - scale: The scale of grid lines (spacing between lines).
 * - fadeStart: Distance from the camera at which the grid begins to fade.
 * - fadeEnd: Distance at which the grid is fully faded out/invisible.
 */
struct GridConfig {
    float size      = 10000.0f;    ///< World-space size of the grid area
    float scale     = 1.0f;        ///< Grid cell size (distance between major lines)
    float fadeStart = 10.0f;       ///< Distance where grid lines start to fade out
    float fadeEnd   = 150.0f;      ///< Distance where grid lines are fully invisible
};

/**
 * @brief An OpenGL render pass that draws a ground grid in the scene for orientation.
 * 
 * The grid is typically rendered in the XZ-plane. It can be used for editor or debugging views
 * to help with orientation and spatial awareness in the 3D scene.
 */
class GLGridPass : public RenderPass {
    public:
        GLGridPass() = delete;
        ~GLGridPass() = default;

        GLGridPass(const GLGridPass& other) = delete;
        GLGridPass& operator=(const GLGridPass& other) = delete;

        GLGridPass(GLGridPass && other) = delete;
        GLGridPass& operator=(GLGridPass && other) = delete;

        /**
         * @brief Construct the grid render pass using the specified shader.
         *
         * @param shader Reference to the OpenGL shader program used to render the grid.
         */
        GLGridPass(Core::Shader& shader);

    public:
        /**
         * @brief Respond to framebuffer or window resize.
         * 
         * Updates grid configuration or mesh data as needed.
         * @param backend The render backend
         * @param width   New framebuffer width
         * @param height  New framebuffer height
         */
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;

        /**
         * @brief Execute the grid rendering pass.
         * 
         * Draws the grid mesh using the configured shader.
         * @param backend   The render backend (should be OpenGL)
         * @param view      Render view/camera information
         * @param resources Resource manager for the scene
         */
        void execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) override;

    private:
        /**
         * @brief Initialize grid mesh and resources.
         * 
         * Sets up the grid mesh geometry and any GPU resources required.
         */
        void initialize();

    private:
        Core::Shader& m_shader;

        std::unique_ptr<GLMesh> m_mesh;

        GridConfig m_config;
};

} // namespace Engine

