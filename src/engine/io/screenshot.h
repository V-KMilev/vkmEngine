#pragma once

#include <cstdint>
#include <string>

namespace Engine {

class WindowManager;
class RenderBackend;

namespace Screenshot {

struct Rect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

/**
 * @brief Capture a rectangle of the backbuffer and write it to a PNG file.
 *
 * @param backend       Active render backend (used for pixel readback).
 * @param rect          Rectangle in backbuffer coordinates (origin top-left).
 * @param windowHeight  Window height, needed by the backend to flip rows.
 * @param outPath       Destination PNG path. Parent directories are created.
 * @return The absolute path written on success, empty string on failure.
 */
std::string captureRect(
    RenderBackend& backend,
    const Rect& rect,
    uint32_t windowHeight,
    const std::string& outPath
);

/**
 * @brief Capture the window's scene viewport rect into APP_ROOT_DIR/screenshots.
 *
 * Reads WindowManager::sceneViewport* for the source rect, falling back to
 * the entire window if no viewport sub-rect is set. Must be called before
 * any UI is submitted on top of the backbuffer this frame; otherwise the
 * captured image includes that UI.
 *
 * @return The absolute path written on success, empty string on failure.
 */
std::string captureViewport(WindowManager& window, RenderBackend& backend);

} // namespace Screenshot

} // namespace Engine
