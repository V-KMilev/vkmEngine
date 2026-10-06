#include "core/clock.h"

#include <algorithm>
#include <cmath>

#include "core/engine_config.h"

namespace Vkm::Engine {

void Clock::beginFrame() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    m_deltaTime = m_started ? std::chrono::duration<float>(now - m_last).count() : 0.0f;
    m_started   = true;
    m_last      = now;

    const float measured = std::min(
        m_paused ? 0.0f : m_deltaTime * m_timeScale * m_pacing,
        Config::MAX_FRAME_ACCUMULATOR
    );
    m_accumulator = std::min(m_accumulator + measured, Config::MAX_FRAME_ACCUMULATOR);

    m_simDelta = measured + static_cast<float>(m_pendingSteps) * m_fixedStep;

    // A lag, not a window: the weight comes from the delta, so it holds at any frame rate.
    constexpr float SMOOTHING_SECONDS = 0.5f;
    const float weight = m_smoothedDelta > 0.0f
        ? 1.0f - std::exp(-m_deltaTime / SMOOTHING_SECONDS)
        : 1.0f;
    m_smoothedDelta += (m_deltaTime - m_smoothedDelta) * weight;
}

bool Clock::consumeFixedStep() {
    if (m_pendingSteps > 0)                --m_pendingSteps;
    else if (m_accumulator >= m_fixedStep) m_accumulator -= m_fixedStep;
    else return false;

    ++m_tick;
    return true;
}

} // namespace Vkm::Engine
