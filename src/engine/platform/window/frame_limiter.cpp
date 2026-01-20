#include "frame_limiter.h"

#include <thread>

using namespace std::chrono;

namespace Engine {

void FrameLimiter::beginFrame() {
    m_frameStart = high_resolution_clock::now();
}

void FrameLimiter::endFrame() {
    // Frame limiting is disabled (unlimited mode)
    if (m_targetFramerate == 0) {
        return;
    }

    // Target frame duration
    duration<double, std::milli> targetFrameTime(1000.0 / static_cast<double>(m_targetFramerate));
    auto targetEnd = m_frameStart + duration_cast<high_resolution_clock::duration>(targetFrameTime);

    auto now = high_resolution_clock::now();

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
    while (high_resolution_clock::now() < targetEnd) {
        std::this_thread::yield();
    }
}

} // namespace Engine