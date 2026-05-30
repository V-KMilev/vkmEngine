#pragma once

#include "resource/shader_asset.h"
#include "gl_render_pass.h"
#include "resource/gl_mesh.h"

#include <memory>

namespace Core {
    class Context;
}

namespace Engine {

/**
 * @brief An OpenGL render pass that draws a ground grid in the scene for orientation.
 * 
 * The grid is typically rendered in the XZ-plane. It can be used for editor or debugging views
 * to help with orientation and spatial awareness in the 3D scene.
 */
class GLGridPass : public GLRenderPass {
    public:
        GLGridPass() = delete;
        ~GLGridPass() override = default;

        GLGridPass(const GLGridPass& other) = delete;
        GLGridPass& operator=(const GLGridPass& other) = delete;

        GLGridPass(GLGridPass && other) = delete;
        GLGridPass& operator=(GLGridPass && other) = delete;

        /**
         * @brief Construct the grid render pass using the specified shader.
         *
         * @param shader Reference to the OpenGL shader program used to render the grid.
         */
        explicit GLGridPass(ShaderHandle shader);

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
        void executeGL(GLBackend& gl, RenderGraphContext& ctx) override;

        void declareResources(RenderGraphBuilder& builder) const override {
            // Writes the HDR FBO overlay attachment, not SceneHDR colour;
            // overlay pixels skip the tonemap chain in the composite pass.
            builder.write(RGResource::Overlay);
        }

        bool enabledForView(const RenderView& view) const override;

    private:
        /**
         * @brief Initialize grid mesh and resources.
         * 
         * Sets up the grid mesh geometry and any GPU resources required.
         */
        void initialize();

    private:
        ShaderHandle m_shader;

        std::unique_ptr<GLMesh> m_mesh;
};

} // namespace Engine

