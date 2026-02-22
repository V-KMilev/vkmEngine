#include "debug/frame_tracker.h"

#include <algorithm>

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

    // O(1) running sum update: subtract old value, add new value
    m_runningSum -= static_cast<double>(m_frameTimes[m_frameIndex]);
    m_frameTimes[m_frameIndex] = frameTime;
    m_runningSum += static_cast<double>(frameTime);

    m_frameIndex = (m_frameIndex + 1) % SAMPLE_SIZE;
    if (m_validSamples < SAMPLE_SIZE) {
        m_validSamples++;
    }

    m_lastFrameTime = currentTime;

    // Calculate average using only valid samples (fixes inflated FPS on startup)
    m_frameRateInfo.frameTime = static_cast<float>(m_runningSum / static_cast<double>(m_validSamples));
    m_frameRateInfo.frameRate = 1000.0f / m_frameRateInfo.frameTime;

    // Calculate min/max over valid samples
    auto validEnd = m_frameTimes.begin() + static_cast<std::ptrdiff_t>(m_validSamples);
    auto [minIt, maxIt] = std::minmax_element(m_frameTimes.begin(), validEnd);
    m_frameRateInfo.minFrameTime = *minIt;
    m_frameRateInfo.maxFrameTime = *maxIt;
}

void FrameTracker::reset() {
    m_frameRateInfo.frameTime = 0.0f;
    m_frameRateInfo.frameRate = 0.0f;
    m_frameRateInfo.minFrameTime = 0.0f;
    m_frameRateInfo.maxFrameTime = 0.0f;

    m_frameIndex = 0;
    m_validSamples = 0;
    m_runningSum = 0.0;
    m_frameTimes.fill(0.0f);
    m_lastFrameTime = high_resolution_clock::now();
}

} // namespace Engine