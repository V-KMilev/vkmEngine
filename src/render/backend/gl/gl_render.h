#pragma once

#include <memory>

#include "render_backend.h"

#include "gl_view.h"

namespace Core {
    class Context;
    class Shader;
}

namespace Engine {
    class RenderView;
    class ResourceManager;
    class RenderPipeline;
}

namespace Engine {

/**
 * @brief OpenGL implementation of the RenderBackend interface.
 *
 * GLRender bridges the high-level render system with the OpenGL API.
 * It manages drawing the current frame using OpenGL, including delegating mesh 
 * management to GLView, and coordinating shader usage and the rendering context.
 * Prevents copy/move semantics to ensure unique association with GL resources.
 */
class GLRender : public RenderBackend {
    public:
        GLRender() = delete;
        ~GLRender() = default;

        GLRender(const GLRender& other) = delete;
        GLRender& operator=(const GLRender& other) = delete;

        GLRender(GLRender && other) = delete;
        GLRender& operator=(GLRender && other) = delete;

        /**
         * @brief Construct a GLRender backend with associated OpenGL context and main shader.
         * @param context Reference to the OpenGL context.
         * @param shader Reference to the shader program used for rendering.
         */
        GLRender(Core::Context& context, Core::Shader& shader);

    public:
        /**
         * @brief Resize the render target or OpenGL viewport.
         * @param width New width in pixels.
         * @param height New height in pixels.
         */
        void resize(uint32_t width, uint32_t height) override;

        /**
         * @brief Render the given scene view using OpenGL.
         * 
         * Updates GPU-side meshes via GLView, binds the configured shader, sets up
         * scene uniforms, and issues draw calls for all visible instances in the RenderView.
         *
         * @param renderView The RenderView describing scene contents this frame.
         * @param resourceManager Accessor to mesh, texture, and material assets.
         * @param width Current width of the rendering surface.
         * @param height Current height of the rendering surface.
         */
        void render(
            const RenderView& renderView,
            const ResourceManager& resourceManager,
            uint32_t width,
            uint32_t height
        ) override;

    private:
        Core::Context& m_context;
        Core::Shader& m_shader;
        GLView m_view;
};

} // namespace Engine