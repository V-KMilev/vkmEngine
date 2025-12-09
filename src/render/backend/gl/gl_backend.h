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
 * GLBackend bridges the high-level render system with the OpenGL API.
 * It manages the overall rendering workflow, including viewport resizing, 
 * context handling, and mesh-resource management.
 * 
 * This class prevents copy and move operations to guarantee a unique association
 * with OpenGL resources, which cannot be safely duplicated or transferred.
 */
class GLBackend : public RenderBackend {
    public:
        GLBackend() = delete;
        ~GLBackend() = default;

        GLBackend(const GLBackend& other) = delete;
        GLBackend& operator=(const GLBackend& other) = delete;

        GLBackend(GLBackend && other) = delete;
        GLBackend& operator=(GLBackend && other) = delete;

        /**
         * @brief Construct a GLBackend with an associated OpenGL context.
         * @param context Reference to the OpenGL context; must remain valid for the backend's lifetime.
         */
        GLBackend(Core::Context& context);

    public:
        /**
         * @brief Resize the render target or OpenGL viewport.
         * 
         * Adjusts the OpenGL viewport and related render targets to match the provided dimensions.
         * Typically called in response to a window or framebuffer resize event.
         * 
         * @param width  New width in pixels.
         * @param height New height in pixels.
         */
        void resize(uint32_t width, uint32_t height) override;

        /**
         * @brief Get the OpenGL rendering context.
         * 
         * Provides access to the low-level OpenGL context for advanced operations or integration.
         * 
         * @return Reference to the OpenGL context.
         */
        Core::Context& getContext() { return m_context; }

        /**
         * @brief Get the OpenGL rendering context (const version).
         * 
         * Provides read-only access to the OpenGL context.
         * 
         * @return Const reference to the OpenGL context.
         */
        const Core::Context& getContext() const { return m_context; }

        /**
         * @brief Get the OpenGL view, which manages all GPU mesh resources and synchronization.
         * 
         * The GLView provides GPU-side access to mesh data and manages updates in response
         * to scene changes and resource manager events.
         * 
         * @return Reference to the internal GLView.
         */
        GLView& getView() { return m_view; }

        /**
         * @brief Get the OpenGL view (const version).
         * 
         * Provides read-only access to the GLView for mesh querying and state inspection.
         * 
         * @return Const reference to the internal GLView.
         */
        const GLView& getView() const { return m_view; }

    private:
        Core::Context& m_context;
        GLView m_view;
};

} // namespace Engine