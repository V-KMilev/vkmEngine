#include "platform/window/frame_limiter.h"

#include <thread>

namespace Engine {

void FrameLimiter::beginFrame() {
    // steady_clock like Clock: monotonic, so a wall-clock adjustment mid-frame
    // can never stall the limiter (high_resolution_clock has no such guarantee).
    m_frameStart = std::chrono::steady_clock::now();
}

void FrameLimiter::endFrame() {
    using namespace std::chrono;

    // Frame limiting is disabled (unlimited mode)
    if (m_targetFramerate == 0) {
        return;
    }

    duration<double, std::milli> targetFrameTime(1000.0 / static_cast<double>(m_targetFramerate));
    auto targetEnd = m_frameStart + duration_cast<steady_clock::duration>(targetFrameTime);

    auto now = steady_clock::now();

    // Already past target, no sleep needed
    if (now >= targetEnd) {
        return;
    }

    // Sleep for most of the remaining time (leave ~1ms for spin-wait precision)
    auto remaining = targetEnd - now;
    auto sleepDuration = remaining - milliseconds(1);

    if (sleepDuration > microseconds(100)) {
        std::this_thread::sleep_for(sleepDuration);
    }

    // Spin-wait for precise timing
    while (std::chrono::steady_clock::now() < targetEnd) {
        std::this_thread::yield();
    }
}

} // namespace Engine
