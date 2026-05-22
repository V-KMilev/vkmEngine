#pragma once

#include <cstddef>
#include <string>

namespace Engine {

class WindowManager;
class RenderBackend;

/**
 * @brief One-shot viewport screenshot to disk.
 *
 * Asks the active render backend for the viewport rect's pixels and
 * writes a timestamped PNG into APP_ROOT_DIR/screenshots/. Returns the
 * absolute path on success, empty string on failure.
 *
 * The 3D pipeline renders into the viewport rect (gl_composite_pass sets
 * glViewport to it), so the result is a clean 3D image with no editor
 * chrome - as long as this is called BEFORE the editor's draw pass
 * submits its UI into the back buffer for this frame.
 */
std::string captureViewportScreenshot(WindowManager& window, RenderBackend& backend);

}  // namespace Engine
