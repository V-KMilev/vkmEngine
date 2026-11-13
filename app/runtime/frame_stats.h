#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <numeric>
#include <vector>

namespace Vkm::App {

/**
 * @brief How long a run's frames took, in milliseconds. The tails are here because an average hides
 *        a hitch.
 */
struct FrameStats {
    size_t frames    = 0;
    float  averageMs = 0.0f;
    float  medianMs  = 0.0f;
    float  low1Ms    = 0.0f;  ///< The slowest 1% of frames, averaged.
    float  low01Ms   = 0.0f;  ///< The slowest 0.1%, averaged.
    float  worstMs   = 0.0f;
    size_t spikes    = 0;     ///< Frames over twice the median.
};

/**
 * @brief Summarize a run of frame times.
 *
 * @param ms Each frame's time, in milliseconds, in any order.
 * @return The summary; all zero for no frames.
 */
inline FrameStats summarizeFrames(std::vector<float> ms) {
    FrameStats stats;
    if (ms.empty()) return stats;

    std::sort(ms.begin(), ms.end(), std::greater<>());
    const auto averageOfSlowest = [&ms](size_t count) {
        count = std::clamp<size_t>(count, 1, ms.size());
        const double sum = std::accumulate(ms.begin(), ms.begin() + static_cast<std::ptrdiff_t>(count), 0.0);
        return static_cast<float>(sum / static_cast<double>(count));
    };

    stats.frames    = ms.size();
    stats.averageMs = averageOfSlowest(ms.size());
    stats.medianMs  = ms[ms.size() / 2];
    stats.low1Ms    = averageOfSlowest(ms.size() / 100);
    stats.low01Ms   = averageOfSlowest(ms.size() / 1000);
    stats.worstMs   = ms.front();
    stats.spikes    = static_cast<size_t>(std::count_if(ms.begin(), ms.end(), [&stats](float frame) {
        return frame > 2.0f * stats.medianMs;
    }));
    return stats;
}

} // namespace Vkm::App
