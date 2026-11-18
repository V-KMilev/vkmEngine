#include "core/clock.h"

#include <algorithm>
#include <cmath>

#include "net/net_session.h"

namespace Vkm::Engine {

void Clock::beginFrame() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    // A host stepping a paused world frame by frame gets every frame alike, however long each took.
    const bool  stepped = m_started && m_paused && m_pendingSteps > 0;
    const float wall    = m_started ? std::chrono::duration<float>(now - m_last).count() : 0.0f;

    m_deltaTime = stepped ? static_cast<float>(m_pendingSteps) * m_fixedStep : wall;
    m_started   = true;
    m_last      = now;

    const float measured = std::min(
        m_paused || m_held || m_menuShown ? 0.0f : m_deltaTime * m_timeScale * m_pacing * m_hostScale,
        Config::MAX_FRAME_ACCUMULATOR
    );
    m_accumulator = std::min(m_accumulator + measured, Config::MAX_FRAME_ACCUMULATOR);

    m_simDelta = m_held ? 0.0f : measured + static_cast<float>(m_pendingSteps) * m_fixedStep;

    // A lag, not a window: the weight comes from the delta, so it holds at any frame rate.
    constexpr float SMOOTHING_SECONDS = 0.5f;
    const float weight = m_smoothedDelta > 0.0f
        ? 1.0f - std::exp(-m_deltaTime / SMOOTHING_SECONDS)
        : 1.0f;
    m_smoothedDelta += (m_deltaTime - m_smoothedDelta) * weight;
}

bool Clock::consumeFixedStep() {
    if (m_held)                            return false;
    if (m_pendingSteps > 0)                --m_pendingSteps;
    else if (m_accumulator >= m_fixedStep) m_accumulator -= m_fixedStep;
    else return false;

    ++m_tick;
    return true;
}

double worldTime(const NetSession& net, const Clock& clock) {
    return net.worldTime(clock.getTick(), clock.getFixedAlpha(), clock.getFixedStep());
}

} // namespace Vkm::Engine
