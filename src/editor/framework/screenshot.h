#pragma once

#include <cstddef>
#include <string>

namespace Engine {

class WindowManager;

/**
 * @brief One-shot viewport screenshot to disk.
 *
 * Captures the current default framebuffer with glReadPixels and writes a
 * timestamped PNG into APP_ROOT_DIR/screenshots/. Returns the absolute path
 * on success, empty string on failure.
 *
 * The whole GLFW window is captured (the engine renders to the default FB
 * sized to the window). This includes any ImGui overlays drawn after the
 * 3D pass when called post-Render, so callers should invoke this BEFORE
 * the ImGui draw pass if they want a clean image.
 */
std::string captureViewportScreenshot(WindowManager& window);

}  // namespace Engine
