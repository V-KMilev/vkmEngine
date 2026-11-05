#include "frame/gl_frame_reader.h"

#include <algorithm>
#include <utility>

#include <GL/glew.h>

#include "system/render/render_backend.h"
#include "system/render/render_view.h"

namespace Vkm::Engine {

bool GLFrameReader::read(const RenderView& view) {
    const uint32_t width  = view.viewportWidth;
    const uint32_t height = view.viewportHeight;
    if (width == 0 || height == 0) return false;

    // Rows packed tight (the backend's pixel alignment). A spare of another size is a viewport
    // since resized: it goes.
    const uint32_t bytes = width * height * 3;
    m_spare.erase(
        std::remove_if(
            m_spare.begin(),
            m_spare.end(),
            [&](const auto& spare) { return spare.size() != bytes; }
        ),
        m_spare.end()
    );
    if (m_spare.empty()) m_spare.emplace_back(bytes);
    m_reads.push_back({std::move(m_spare.back()), width, height});
    m_spare.pop_back();

    // The default framebuffer's own read buffer: a window's back buffer, or a pbuffer's only one.
    // GL counts rows from the bottom.
    const int32_t glY = static_cast<int32_t>(view.surfaceHeight) - static_cast<int32_t>(view.viewportY)
        - static_cast<int32_t>(height);
    m_reads.back().buffer.captureDefault(
        static_cast<int32_t>(view.viewportX),
        glY,
        static_cast<int32_t>(width),
        static_cast<int32_t>(height),
        GL_RGB,
        GL_UNSIGNED_BYTE
    );
    return true;
}

bool GLFrameReader::take(FramePixels& frame, bool wait) {
    if (m_reads.empty()) return false;
    Read& oldest = m_reads.front();
    if (wait) {
        oldest.buffer.wait();
    } else if (!oldest.buffer.arrived()) {
        return false;
    }

    // GL reads bottom row first.
    const size_t   stride = static_cast<size_t>(oldest.width) * 3;
    const uint8_t* rows   = static_cast<const uint8_t*>(oldest.buffer.data());
    frame.width  = oldest.width;
    frame.height = oldest.height;
    frame.rgb.resize(stride * oldest.height);
    for (uint32_t y = 0; y < oldest.height; ++y) {
        std::copy_n(
            rows + static_cast<size_t>(oldest.height - 1 - y) * stride,
            stride,
            frame.rgb.data() + y * stride
        );
    }

    m_spare.push_back(std::move(oldest.buffer));
    m_reads.pop_front();
    return true;
}

} // namespace Vkm::Engine
