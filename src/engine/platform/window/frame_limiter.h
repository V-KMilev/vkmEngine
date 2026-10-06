#pragma once

#include <chrono>

namespace Vkm::Engine {

/**
 * @brief Limits a render loop's frame rate by sleeping, then spin-waiting.
 *
 * A target of 0 disables it.
 */
class FrameLimiter {
    public:
        /**
         * @brief Ask Windows for a 1 ms scheduler tick for as long as the limiter lives.
         *
         * Otherwise a sleep wakes on the default 15.6 ms tick. A no-op on Linux.
         */
        FrameLimiter();
        ~FrameLimiter();

        FrameLimiter(const FrameLimiter& other) = delete;
        FrameLimiter& operator=(const FrameLimiter& other) = delete;

        FrameLimiter(FrameLimiter && other) = delete;
        FrameLimiter& operator=(FrameLimiter && other) = delete;

    public:
        /**
         * @brief Marks the start of a frame, before any work is done.
         */
        void beginFrame();

        /**
         * @brief Marks the end of a frame and waits as necessary to match the target framerate.
         */
        void endFrame();

        /**
         * @brief Set the desired target framerate (frames per second).
         *
         * @param framerate Desired framerate in FPS; 0 or less disables the limiter.
         */
        void setTargetFramerate(int framerate) { m_targetFramerate = framerate > 0 ? framerate : 0; }

        /**
         * @brief The cap in effect, in frames per second.
         *
         * @return The target framerate, or 0 when the limiter is disabled.
         */
        int targetFramerate() const { return m_targetFramerate; }

    private:
        /**
         * @brief Sleep for @p span, as closely as the platform allows.
         *
         * On Windows this waits on a high-resolution timer: a plain sleep wakes on the scheduler
         * tick, which stays at 15.6 ms on machines that ignore a request for a finer one.
         *
         * @param span How long to sleep.
         */
        void sleepFor(std::chrono::steady_clock::duration span);

    private:
        int m_targetFramerate = 0;
        std::chrono::steady_clock::time_point m_frameStart;

        /// The last frame's end, which the next is paced from; empty before the first.
        std::chrono::steady_clock::time_point m_deadline;

        void* m_timer = nullptr;  ///< Windows' high-resolution timer; null elsewhere, or where it is refused
};

} // namespace Vkm::Engine
