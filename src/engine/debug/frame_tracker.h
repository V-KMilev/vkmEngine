#pragma once

#include <array>
#include <chrono>
#include <cstddef>

namespace Engine {

/**
 * @brief Frame timing snapshot: average / min / max over a sliding window.
 */
struct FrameRateInfo {
    float frameTime    = 0.0f;
    float frameRate    = 0.0f;
    float minFrameTime = 0.0f;
    float maxFrameTime = 0.0f;
};

/**
 * @brief Tracks per-frame timing (frametime, framerate, min/max) over a
 *        sliding window of recent frames.
 *
 * Engine owns one and updates it once per main-loop iteration. Drives
 * the FrameContext.deltaTime feed and the editor's FPS overlay. Separate
 * from the Tracy profiler (debug/profiler.h): this is the always-on,
 * release-build-cheap source of frame timing the engine itself needs.
 */
class FrameTracker {
    public:
        FrameTracker();
        ~FrameTracker() = default;

        FrameTracker(const FrameTracker& other) = delete;
        FrameTracker& operator=(const FrameTracker& other) = delete;

        FrameTracker(FrameTracker && other) = delete;
        FrameTracker& operator=(FrameTracker && other) = delete;

    public:
        /**
         * @brief Sample the wall clock and fold the new delta into the window.
         *
         * Call once per render frame, typically at end-of-loop.
         */
        void update();

        /** @brief Drop accumulated samples; reset min/max/avg to zero. */
        void reset();

        const FrameRateInfo& getFrameRateInfo() const { return m_frameRateInfo; }

    private:
        FrameRateInfo m_frameRateInfo;

        static constexpr size_t SAMPLE_SIZE = 60;
        std::array<float, SAMPLE_SIZE> m_frameTimes;

        size_t m_frameIndex   = 0;
        size_t m_validSamples = 0;
        double m_runningSum   = 0.0;

        std::chrono::high_resolution_clock::time_point m_lastFrameTime;
};

} // namespace Engine
