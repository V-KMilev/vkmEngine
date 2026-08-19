#include "platform/window/frame_limiter.h"

#include <thread>

namespace Vkm::Engine {

void FrameLimiter::beginFrame() {
    // steady_clock like Clock: monotonic, so a wall-clock adjustment mid-frame
    // can never stall the limiter (high_resolution_clock has no such guarantee).
    m_frameStart = std::chrono::steady_clock::now();
}

void FrameLimiter::endFrame() {
    using namespace std::chrono;

    if (m_targetFramerate == 0) {
        return;
    }

    duration<double, std::milli> targetFrameTime(1000.0 / static_cast<double>(m_targetFramerate));
    auto targetEnd = m_frameStart + duration_cast<steady_clock::duration>(targetFrameTime);

    auto now = steady_clock::now();

    if (now >= targetEnd) {
        return;
    }

    // Sleep most of the remaining time; the last ~1ms is spun out below,
    // because a sleep is not precise enough on its own.
    auto remaining = targetEnd - now;
    auto sleepDuration = remaining - milliseconds(1);

    if (sleepDuration > microseconds(100)) {
        std::this_thread::sleep_for(sleepDuration);
    }

    while (std::chrono::steady_clock::now() < targetEnd) {
        std::this_thread::yield();
    }
}

} // namespace Vkm::Engine
