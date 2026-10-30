#pragma once

namespace Vkm::Platform {
class WindowManager;
} // namespace Vkm::Platform

namespace Vkm::Engine {

class Clock;

/**
 * @brief How far past its last tick the frame about to be drawn falls.
 *
 * @param clock  The frame's clock.
 * @param window The host's window.
 * @return Seconds; zero while time stands still, and in a host that draws nothing.
 */
float secondsAhead(const Clock& clock, const Platform::WindowManager& window);

} // namespace Vkm::Engine
