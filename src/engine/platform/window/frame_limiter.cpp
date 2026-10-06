#include "platform/window/frame_limiter.h"

#include <thread>

#include "platform/windows_api.h"

namespace Vkm::Engine {

FrameLimiter::FrameLimiter() {
#if defined(_WIN32)
    timeBeginPeriod(1);
    m_timer = CreateWaitableTimerExW(
        nullptr,
        nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_ALL_ACCESS
    );
#endif
}

FrameLimiter::~FrameLimiter() {
#if defined(_WIN32)
    if (m_timer) CloseHandle(m_timer);
    timeEndPeriod(1);
#endif
}

void FrameLimiter::beginFrame() {
    m_frameStart = std::chrono::steady_clock::now();
}

void FrameLimiter::endFrame() {
    using namespace std::chrono;

    if (m_targetFramerate == 0) {
        m_deadline = {};
        return;
    }

    const auto period = duration_cast<steady_clock::duration>(
        duration<double>(1.0 / static_cast<double>(m_targetFramerate))
    );
    // From the last deadline, not this frame's start, or the present and events between frames
    // add to every period and the cadence runs slow.
    const steady_clock::time_point first{};
    const auto targetEnd = m_deadline == first ? m_frameStart + period : m_deadline + period;

    const auto now = steady_clock::now();

    // An overrun restarts the pace from now: a stall is not paid back with short frames.
    if (now >= targetEnd) {
        m_deadline = now;
        return;
    }
    m_deadline = targetEnd;

    // Sleep most of the remaining time; the last ~1ms is spun, as a sleep is imprecise.
    auto remaining = targetEnd - now;
    auto sleepDuration = remaining - milliseconds(1);

    if (sleepDuration > microseconds(100)) {
        sleepFor(sleepDuration);
    }

    while (std::chrono::steady_clock::now() < targetEnd) {
        std::this_thread::yield();
    }
}

void FrameLimiter::sleepFor(std::chrono::steady_clock::duration span) {
#if defined(_WIN32)
    if (m_timer) {
        // Negative is relative, in 100 ns units.
        LARGE_INTEGER due;
        due.QuadPart = -std::chrono::duration_cast<std::chrono::nanoseconds>(span).count() / 100;
        if (SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(m_timer, INFINITE);
            return;
        }
    }
#endif
    std::this_thread::sleep_for(span);
}

} // namespace Vkm::Engine
