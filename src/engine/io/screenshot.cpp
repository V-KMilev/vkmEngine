#define VKM_LOG_CATEGORY "ENGINE"

#include "io/screenshot.h"

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
#include "io/project_paths.h"

namespace Engine::Screenshot {

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

} // namespace

std::string captureRect(
    RenderBackend& backend,
    const Rect& rect,
    uint32_t windowHeight,
    const std::string& outPath
) {
    if (rect.width == 0 || rect.height == 0) return {};

    // Pixel readback isn't part of the minimal RenderBackend interface yet, so
    // screenshots are disabled until the backend exposes a readback again.
    (void)backend;
    (void)windowHeight;
    (void)outPath;
    LOG_WARNING("Screenshot: pixel readback not supported by the current backend yet");
    return {};
}

std::string captureViewport(WindowManager& window, RenderBackend& backend) {
    const uint32_t winH = static_cast<uint32_t>(window.getHeight());

    Rect rect;
    rect.x      = window.sceneViewportX();
    rect.y      = window.sceneViewportY();
    rect.width  = window.sceneViewportWidth();
    rect.height = window.sceneViewportHeight();
    if (rect.width == 0 || rect.height == 0) {
        rect.x = 0;
        rect.y = 0;
        rect.width  = static_cast<uint32_t>(window.getWidth());
        rect.height = winH;
    }

    const std::filesystem::path dir = ProjectPaths::screenshots();
    const std::filesystem::path full = dir / ("screenshot-" + timestamp() + ".png");
    return captureRect(backend, rect, winH, full.string());
}

} // namespace Engine::Screenshot
