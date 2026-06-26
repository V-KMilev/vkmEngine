#include "core/clock.h"

#include <algorithm>

#include "core/engine_config.h"

namespace Engine {

void Clock::beginFrame() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    m_deltaTime = m_started ? std::chrono::duration<float>(now - m_last).count() : 0.0f;
    m_started   = true;
    m_last      = now;

    m_simDelta    = simDeltaFor(m_deltaTime);
    m_accumulator = std::min(m_accumulator + m_simDelta, Config::MAX_FRAME_ACCUMULATOR);
}

bool Clock::consumeFixedStep() {
    if (m_accumulator < Config::FIXED_TIME_STEP) {
        return false;
    }
    m_accumulator -= Config::FIXED_TIME_STEP;
    return true;
}

float Clock::simDeltaFor(float realDelta) {
    if (!m_paused) {
        return realDelta * m_timeScale;
    }
    if (m_pendingSteps > 0) {
        const float stepped = static_cast<float>(m_pendingSteps) * Config::FIXED_TIME_STEP;
        m_pendingSteps = 0;
        return stepped;
    }
    return 0.0f;
}

} // namespace Engine
