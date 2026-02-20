#pragma once

#include "render/render_pass.h"

namespace Core {
    class Shader;
}

namespace Engine {

/**
 * @brief Render pass for forward rendering using OpenGL.
 * 
 * The GLForwardPass executes forward rendering for visible scene geometry using a supplied
 * OpenGL shader. This pass is typically responsible for drawing meshes with basic lighting 
 * and material support, outputting directly to the main framebuffer.
 *
 * NOTE: The m_shader member is slated for removal in future refactors.
 */
class GLForwardPass : public RenderPass {
    public:
        GLForwardPass() = delete;
        ~GLForwardPass() = default;

        GLForwardPass(const GLForwardPass& other) = delete;
        GLForwardPass& operator=(const GLForwardPass& other) = delete;

        GLForwardPass(GLForwardPass && other) = delete;
        GLForwardPass& operator=(GLForwardPass && other) = delete;

        /**
         * @brief Construct a GLForwardPass that renders geometry with a given shader.
         * @param shader Reference to an OpenGL shader to be used for rendering all geometry in this pass.
         */
         GLForwardPass(Core::Shader& shader);

    public:
        /**
         * @brief Respond to a framebuffer or window resize.
         * 
         * Updates internal state or resources as necessary when the output framebuffer changes size.
         * @param backend Reference to the render backend.
         * @param width   New width in pixels.
         * @param height  New height in pixels.
         */
        void onResize(RenderBackend& backend, uint32_t width, uint32_t height) override;

        /**
         * @brief Execute the forward rendering pass for the current frame.
         * 
         * Binds the shader and draws all visible meshes using geometry and materials found  
         * in the resource manager and render view.
         * @param backend   Reference to the current render backend (should be OpenGL).
         * @param view      Provides scene and camera information.
         * @param resources Access to GPU mesh and material handles for this frame.
         */
        void execute(RenderBackend& backend, const RenderView& view, const ResourceManager& resources) override;

    private:
         // TODO(vkm): Remove the shader as a member and refactor API.
        Core::Shader& m_shader;
};

} // namespace Engine
