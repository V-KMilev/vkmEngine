#pragma once

#include <cstdint>

namespace Engine {
    class RenderView;
    class ResourceManager;
}

namespace Engine {

/**
 * @brief Enumeration of supported rendering backend types.
 */
enum class RenderBackendType {
    NONE    = 0,    ///< No backend specified or uninitialized.
    OpenGL  = 1,    ///< OpenGL-based rendering backend.
    Optix   = 2,    ///< NVIDIA Optix ray tracing rendering backend.
    CPU     = 3,    ///< CPU/software-based raytracing backend.
};

/**
 * @brief Abstract base class for all rendering backends.
 *
 * Provides the interface all concrete rendering backends (OpenGL, Optix, CPU, etc.)
 * must implement in order to integrate with the rendering system. Prevents
 * copying/moving of instances.
 */
class RenderBackend {
    public:
        /**
         * @brief Construct a RenderBackend with a specific backend type.
         * @param type The RenderBackendType associated with the backend.
         */
        RenderBackend(RenderBackendType type) : m_type(type) {}
        virtual ~RenderBackend() = default;

        RenderBackend(const RenderBackend& other) = delete;
        RenderBackend& operator=(const RenderBackend& other) = delete;

        RenderBackend(RenderBackend&& other) = delete;
        RenderBackend& operator=(RenderBackend&& other) = delete;

    public:
        /**
         * @brief Initialize rendering resources and backend state.
         */
        virtual void start() = 0;

        /**
         * @brief Clean up and release rendering resources.
         */
        virtual void stop() = 0;

        // TODO: Implement these methods
        // virtual void start() = 0;
        // virtual void stop() = 0;

        // virtual void pause() = 0;
        // virtual void resume() = 0;

        /**
         * @brief Resize the backend's render targets or framebuffers.
         * @param width New width in pixels.
         * @param height New height in pixels.
         */
        virtual void resize(uint32_t width, uint32_t height) = 0;

        /**
         * @brief Render a frame using the provided scene view and resources.
         * @param renderView Structured data describing camera and instances in the scene.
         * @param resourceManager Access to asset resources (meshes, materials, etc.).
         * @param width Current viewport width.
         * @param height Current viewport height.
         */
        virtual void render(
            const RenderView& renderView,
            const ResourceManager& resourceManager,
            uint32_t width,
            uint32_t height
        ) = 0;

    protected:
        RenderBackendType m_type;
};

} // namespace Engine