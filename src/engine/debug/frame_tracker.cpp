#include "debug/frame_tracker.h"

using namespace std::chrono;

namespace Engine {

FrameTracker::FrameTracker()
    : m_lastFrameTime(high_resolution_clock::now())
{
    m_frameTimes.fill(0.0f);
}

void FrameTracker::update() {
    auto currentTime = high_resolution_clock::now();

    duration<float, std::milli> deltaTime = currentTime - m_lastFrameTime;
    float frameTime = deltaTime.count();

    // Running sum: O(1) windowed average, not a full re-sum each frame.
    m_runningSum -= static_cast<double>(m_frameTimes[m_frameIndex]);
    m_frameTimes[m_frameIndex] = frameTime;
    m_runningSum += static_cast<double>(frameTime);

    m_frameIndex = (m_frameIndex + 1) % SAMPLE_SIZE;
    if (m_validSamples < SAMPLE_SIZE) {
        m_validSamples++;
    }

    m_lastFrameTime = currentTime;

    // Valid samples only - avoids inflated FPS on startup.
    m_frameRateInfo.frameTime = static_cast<float>(m_runningSum / static_cast<double>(m_validSamples));
    m_frameRateInfo.frameRate = 1000.0f / m_frameRateInfo.frameTime;
}

} // namespace Engine
