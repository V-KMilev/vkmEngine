#pragma once

#include <cstdint>

namespace Engine {

/**
 * @brief Abstract interface for a rendering target (framebuffer).
 *
 * Provides a backend-agnostic way for render passes to output to either
 * the default framebuffer or an offscreen target (e.g., shadow maps,
 * post-processing buffers). Concrete implementations live in the backend layer.
 */
class RenderTarget {
    public:
        virtual ~RenderTarget() = default;

        /**
         * @brief Bind this render target for drawing.
         *
         * After calling bind(), all subsequent draw calls will render into this target.
         */
        virtual void bind() = 0;

        /**
         * @brief Unbind this render target, restoring the default framebuffer.
         */
        virtual void unbind() = 0;

        /**
         * @brief Resize the render target's backing storage.
         * @param width  New width in pixels.
         * @param height New height in pixels.
         */
        virtual void resize(uint32_t width, uint32_t height) = 0;

        virtual uint32_t getWidth() const = 0;
        virtual uint32_t getHeight() const = 0;
};

} // namespace Engine
