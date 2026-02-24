#pragma once

#include <cstdint>

namespace Engine {
    class RenderTarget;
    struct RenderView;
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
 * @brief Convert a ComponentType enum value to its string representation.
 *
 * @param type The RenderBackendType value to convert.
 * @return const char* String representation of the RenderBackendType.
 */
 constexpr const char* toString(RenderBackendType type) {
    switch (type) {
        case RenderBackendType::NONE:    return "NONE";
        case RenderBackendType::OpenGL:  return "OpenGL";
        case RenderBackendType::Optix:   return "Optix";
        case RenderBackendType::CPU:     return "CPU";
        default: return "UNKNOWN";
    }
}

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

        RenderBackend(RenderBackend && other) = delete;
        RenderBackend& operator=(RenderBackend && other) = delete;

    public:
        /**
         * @brief Get the type of the backend.
         * @return The RenderBackendType of the backend.
         */
        RenderBackendType getType() const { return m_type; }

        /**
         * @brief Resize the backend's render targets or framebuffers.
         * @param width New width in pixels.
         * @param height New height in pixels.
         */
        virtual void resize(uint32_t width, uint32_t height) = 0;

        /**
         * @brief Get the default render target (typically the screen framebuffer).
         * @return Reference to the default RenderTarget.
         */
        virtual RenderTarget& getDefaultTarget() = 0;

    protected:
        RenderBackendType m_type;
};

} // namespace Engine