#include "core/clock.h"

#include <algorithm>

#include "core/engine_config.h"

namespace Vkm::Engine {

void Clock::beginFrame() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    m_deltaTime = m_started ? std::chrono::duration<float>(now - m_last).count() : 0.0f;
    m_started   = true;
    m_last      = now;

    // Only measured time reaches the accumulator, so the cap below can only
    // ever discard wall time. Steps that were asked for are drained as a count
    // by consumeFixedStep and cannot be lost to it.
    const float measured = m_paused ? 0.0f : m_deltaTime * m_timeScale;
    m_accumulator = std::min(m_accumulator + measured, Config::MAX_FRAME_ACCUMULATOR);

    // What a frame-rate reader is owed: the time the world is about to advance
    // by, which is the measured span plus whatever was commanded.
    m_simDelta = measured + static_cast<float>(m_pendingSteps) * m_fixedStep;
}

bool Clock::consumeFixedStep() {
    // Commanded steps first and exactly: they are a count, never a span, so no
    // rate can round one away.
    if (m_pendingSteps > 0)                --m_pendingSteps;
    else if (m_accumulator >= m_fixedStep) m_accumulator -= m_fixedStep;
    else return false;

    ++m_tick;
    return true;
}

} // namespace Vkm::Engine
