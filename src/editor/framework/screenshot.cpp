#define VKM_LOG_CATEGORY "EDITOR"

#include "framework/screenshot.h"

#include <chrono>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "logger.h"

#include "platform/window/window_manager.h"
#include "system/render/render_backend.h"

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

std::string captureViewportScreenshot(WindowManager& window, RenderBackend& backend) {
    const uint32_t winH = static_cast<uint32_t>(window.getHeight());

    // Capture just the editor's viewport rect - the 3D pipeline now
    // renders only into that rect on the backbuffer. Falling back to
    // the full window gives garbage outside.
    uint32_t vpX = window.sceneViewportX();
    uint32_t vpY = window.sceneViewportY();
    uint32_t w   = window.sceneViewportWidth();
    uint32_t h   = window.sceneViewportHeight();
    if (w == 0 || h == 0) {
        vpX = 0;
        vpY = 0;
        w = static_cast<uint32_t>(window.getWidth());
        h = winH;
    }
    if (w == 0 || h == 0) return {};

    std::vector<uint8_t> pixels;
    if (!backend.readbackPixels(vpX, vpY, w, h, winH, pixels)) {
        LOG_ERROR("Screenshot: backend readback failed (rect %ux%u at %u,%u)", w, h, vpX, vpY);
        return {};
    }

    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::path(APP_ROOT_DIR) / "screenshots";
    std::filesystem::create_directories(dir, ec);
    const std::string filename = "screenshot-" + timestamp() + ".png";
    const std::filesystem::path full = dir / filename;

    const int stride = static_cast<int>(w) * 3;
    if (!stbi_write_png(full.string().c_str(),
                        static_cast<int>(w), static_cast<int>(h), 3,
                        pixels.data(), stride)) {
        LOG_ERROR("Screenshot: stbi_write_png failed for '%s'", full.string().c_str());
        return {};
    }
    LOG_INFO("Screenshot saved to '%s' (%ux%u)", full.string().c_str(), w, h);
    return full.string();
}

}  // namespace Engine
