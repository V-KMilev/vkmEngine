#pragma once

#include <memory>

#include "system/render/render_backend.h"

#include "gl_context.h"
#include "gl_view.h"
#include "gl_render_target.h"

namespace Engine {
    struct RenderView;
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
        GLBackend();
        ~GLBackend() = default;

        GLBackend(const GLBackend& other) = delete;
        GLBackend& operator=(const GLBackend& other) = delete;

        GLBackend(GLBackend && other) = delete;
        GLBackend& operator=(GLBackend && other) = delete;

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
        RenderTarget& getDefaultTarget() override { return m_defaultTarget; }
        void setWireframe(bool enabled) override;

        /**
         * @brief Get the OpenGL rendering context.
         *
         * Provides access to the low-level OpenGL context for state management and rendering operations.
         * Use this for advanced OpenGL operations not exposed by the high-level API.
         *
         * @return Reference to the OpenGL context.
         */
        Core::Context& getContext() { return m_context; }

        /**
         * @brief Get the OpenGL rendering context (const version).
         *
         * Provides read-only access to the OpenGL context for querying state.
         *
         * @return Const reference to the OpenGL context.
         */
        const Core::Context& getContext() const { return m_context; }

        /**
         * @brief Get the OpenGL view for resource management.
         *
         * The GLView manages all GPU-side resources (meshes, materials, textures, lights)
         * and handles synchronization with the CPU-side resource manager.
         *
         * This is the main interface for:
         * - Syncing resources to GPU
         * - Accessing GPU resources during rendering
         * - Managing resource lifecycle and cleanup
         *
         * @return Reference to the internal GLView.
         */
        GLView& getView() { return m_view; }

        /**
         * @brief Get the OpenGL view (const version).
         *
         * Provides read-only access to the GLView for querying GPU resources.
         *
         * @return Const reference to the internal GLView.
         */
        const GLView& getView() const { return m_view; }

    private:
        Core::Context m_context;
        GLView m_view;
        GLDefaultRenderTarget m_defaultTarget;
};

} // namespace Engine
