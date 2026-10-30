#include "core/frame/seconds_ahead.h"

#include "core/clock.h"
#include "vkm_platform/window/window_manager.h"

namespace Vkm::Engine {

float secondsAhead(const Clock& clock, const Platform::WindowManager& window) {
    if (!window.isOpen() || clock.isSimulationStill()) return 0.0f;
    return clock.getFixedAlpha() * clock.getFixedStep();
}

} // namespace Vkm::Engine
