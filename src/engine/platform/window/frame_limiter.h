#pragma once

#include <chrono>

namespace Vkm::Engine {

/**
 * @brief Utility class for limiting frame rate in a render loop.
 *
 * Uses a combination of sleeping and spin-waiting to achieve a target framerate.
 * If the target framerate is set to 0, the limiter is effectively disabled (unlimited mode).
 */
class FrameLimiter {
    public:
        FrameLimiter() = default;
        ~FrameLimiter() = default;

        FrameLimiter(const FrameLimiter& other) = delete;
        FrameLimiter& operator=(const FrameLimiter& other) = delete;

        FrameLimiter(FrameLimiter && other) = delete;
        FrameLimiter& operator=(FrameLimiter && other) = delete;

    public:
        /**
         * @brief Marks the start of a frame for limiting.
         *
         * Should be called at the beginning of each frame before any work is done.
         */
        void beginFrame();

        /**
         * @brief Marks the end of a frame and waits as necessary to match the target framerate.
         *
         * Should be called after a frame is rendered to enforce the frame rate limit.
         */
        void endFrame();

        /**
         * @brief Set the desired target framerate (frames per second).
         *
         * If framerate is less than or equal to 0, disables the limiter (unlimited mode).
         *
         * @param framerate Desired framerate in FPS.
         */
        void setTargetFramerate(int framerate) { m_targetFramerate = framerate > 0 ? framerate : 0; }

    private:
        int m_targetFramerate = 0;
        std::chrono::steady_clock::time_point m_frameStart;  ///< Monotonic - immune to wall-clock jumps.
};

} // namespace Vkm::Engine
