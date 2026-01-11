#pragma once

#include <array>
#include <chrono>
#include <cstddef>

#include "frame_info.h"

/**
 * @class FrameTracker
 * @brief Tracks per-frame timing statistics (frametime, framerate, min/max frametime)
 *        over a sliding window of recent frames.
 *
 * Use this class to gather frame timing data for performance diagnostics, live statistics panels,
 * or adaptive systems. Maintains a running average, min, and max over the most recent SAMPLE_SIZE frames.
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
         * @brief Updates the timing statistics with the latest elapsed frametime.
         *
         * Call this once each frame, typically at the start or end of your game/render loop.
         * Computes new average, min, and max statistics using a rolling buffer.
         */
        void update();

        /**
         * @brief Resets all accumulated statistics and frame sample buffer.
         *
         * Useful to clear historical data, e.g., when pausing or restarting.
         */
        void reset();

        /**
         * @brief Gets the most recently computed frame rate info.
         * 
         * @return const reference to FrameRateInfo (contains average frameTime, frameRate, min/max times)
         */
        const FrameRateInfo& getFrameRateInfo() const { return m_frameRateInfo; }

    private:
        FrameRateInfo m_frameRateInfo;

        static constexpr size_t SAMPLE_SIZE = 60;
        std::array<float, SAMPLE_SIZE> m_frameTimes;

        size_t m_frameIndex = 0;
        size_t m_validSamples = 0;
        double m_runningSum = 0.0;

        std::chrono::high_resolution_clock::time_point m_lastFrameTime;
};
