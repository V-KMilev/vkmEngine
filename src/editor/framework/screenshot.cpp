#include "framework/screenshot.h"

#include <GL/glew.h>

#include <chrono>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "platform/window/window_manager.h"

namespace Engine {

namespace {
    std::string timestamp() {
        const auto t  = std::chrono::system_clock::now();
        const auto tt = std::chrono::system_clock::to_time_t(t);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
        return buf;
    }
}

std::string captureViewportScreenshot(WindowManager& window) {
    const int w = static_cast<int>(window.getWidth());
    const int h = static_cast<int>(window.getHeight());
    if (w <= 0 || h <= 0) return {};

    std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 3);

    // Read from the front buffer (already swapped on screen) so the image
    // matches what the user just saw. Default unpack alignment of 4 can leave
    // gaps when width*3 isn't 4-aligned; set tight packing.
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_FRONT);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    // Flip vertically (OpenGL origin = bottom-left, PNG = top-left).
    const size_t stride = static_cast<size_t>(w) * 3;
    for (int y = 0; y < h / 2; ++y) {
        unsigned char* a = pixels.data() + y * stride;
        unsigned char* b = pixels.data() + (h - 1 - y) * stride;
        for (size_t i = 0; i < stride; ++i) std::swap(a[i], b[i]);
    }

    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::path(APP_ROOT_DIR) / "screenshots";
    std::filesystem::create_directories(dir, ec);
    const std::string filename = "screenshot-" + timestamp() + ".png";
    const std::filesystem::path full = dir / filename;

    if (!stbi_write_png(full.string().c_str(), w, h, 3, pixels.data(),
                        static_cast<int>(stride))) {
        return {};
    }
    return full.string();
}

}  // namespace Engine
