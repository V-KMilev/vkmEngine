#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "vkm_opengl/buffer/readback_buffer.h"

namespace Vkm::Engine {

struct FramePixels;
struct RenderView;

/**
 * @brief Reads the window's frame back a frame or so after it is drawn, so neither the CPU nor
 *        the GPU waits on the other.
 *
 * Each read copies into a buffer of its own, handed back by take() in the order read once the GPU
 * has written it; a buffer taken serves the next read of its size.
 */
class GLFrameReader {
    public:
        GLFrameReader() = default;
        ~GLFrameReader() = default;

        GLFrameReader(const GLFrameReader& other) = delete;
        GLFrameReader& operator=(const GLFrameReader& other) = delete;

        GLFrameReader(GLFrameReader && other) = delete;
        GLFrameReader& operator=(GLFrameReader && other) = delete;

    public:
        /**
         * @brief Start reading the viewport of the frame just drawn into the default framebuffer.
         *
         * @param view The view the frame was drawn with; its viewport is the rect read.
         * @return False, reading nothing, when the viewport is empty.
         */
        bool read(const RenderView& view);

        /**
         * @brief Hand over the oldest read once it has landed.
         *
         * @param frame Replaced with its pixels, top row first.
         * @param wait  Wait for it rather than answer false while it is on its way.
         * @return False when none is waiting, or, without @p wait, the oldest has not landed.
         */
        bool take(FramePixels& frame, bool wait);

    private:
        struct Read {
            Vkm::OpenGL::ReadbackBuffer buffer;
            uint32_t                    width  = 0;
            uint32_t                    height = 0;
        };

    private:
        std::deque<Read>                         m_reads;  ///< In flight, oldest first.
        std::vector<Vkm::OpenGL::ReadbackBuffer> m_spare;  ///< Taken, each kept for a read of its size.
};

} // namespace Vkm::Engine
