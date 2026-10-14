#pragma once

#include <cstdint>
#include <optional>

namespace Vkm::Engine {

struct RenderSettings;

/**
 * @brief How auto-exposure (RenderSettings::autoExposure) measures a frame and adapts to it.
 *
 * A backend counts the frame's pixels into a histogram of log2 luminance and hands the counts
 * here; everything after the counting is decided here, so every backend adapts alike.
 */
namespace Exposure {

/**
 * @brief Histogram bins, laid out by binOf (which a backend's counting mirrors).
 *
 * The first counts what is darker than MIN_LOG, which no exposure is set by; the rest split
 * MIN_LOG..MAX_LOG evenly in log2, the last also counting what is brighter.
 */
constexpr int BINS = 128;
/// log2 luminance at the floor of the second bin, and at the ceiling of the last.
constexpr float MIN_LOG = -14.0f;
constexpr float MAX_LOG = 10.0f;
/// The log2 luminance one bin spans.
constexpr float BIN_STEP = (MAX_LOG - MIN_LOG) / (BINS - 1);
/// A backend counts a pixel every STRIDE each way.
constexpr int STRIDE = 2;

/// The bin a pixel of log2 luminance @p log2Luminance counts in.
constexpr int binOf(float log2Luminance) {
    if (!(log2Luminance > MIN_LOG)) return 0;
    if (!(log2Luminance < MAX_LOG)) return BINS - 1;
    const int bin = 1 + static_cast<int>((log2Luminance - MIN_LOG) / BIN_STEP);
    return bin < BINS - 1 ? bin : BINS - 1;
}

/// The log2 luminance at the middle of @p bin, one past the first.
constexpr float binCentre(int bin) {
    return MIN_LOG + (static_cast<float>(bin) - 0.5f) * BIN_STEP;
}

/// The darkest share of what is lit, left out of the average: shade does not set the exposure.
constexpr float LOW_SHARE = 0.4f;
/// The share past which the brightest is left out: the sun and its glints do not either.
constexpr float HIGH_SHARE = 0.95f;

/// Where the average lands before RenderSettings::exposure is added.
constexpr float KEY = 0.09f;

/**
 * @brief The stops that bring a histogram's average to KEY, within the settings' range.
 *
 * The average is of log2 luminance, over what lies between LOW_SHARE and HIGH_SHARE of the lit
 * pixels (every bin but the first).
 *
 * @param bins     BINS counts.
 * @param settings Supplies exposureMinimum and exposureMaximum.
 * @return The stops, or none for a frame with nothing lit.
 */
std::optional<float> targetStops(const uint32_t* bins, const RenderSettings& settings);

/**
 * @brief @p current moved toward @p target over @p seconds, as an eye adapts.
 *
 * Closes the share 1 - e^(-rate * seconds) of the gap: at adaptDarker toward fewer stops, at
 * adaptBrighter toward more.
 *
 * @param current  The stops the last frame was exposed by.
 * @param target   What the newest histogram asks for (targetStops).
 * @param seconds  Wall-clock time since the last frame.
 * @param settings Supplies the rates.
 */
float adaptStops(float current, float target, float seconds, const RenderSettings& settings);

/**
 * @brief The adapted stops across frames: a backend feeds it each histogram it reads back.
 *
 * Takes the first read whole, so a view does not fade in from the authored exposure. A frame
 * read with nothing lit keeps the last target.
 */
class Adaptation {
    public:
        Adaptation() = default;
        ~Adaptation() = default;

        Adaptation(const Adaptation& other) = default;
        Adaptation& operator=(const Adaptation& other) = default;

        Adaptation(Adaptation && other) = default;
        Adaptation& operator=(Adaptation && other) = default;

    public:
        /**
         * @brief The stops this frame is exposed by, RenderSettings::exposure included.
         *
         * With autoExposure off, exposure alone, and the next read is taken whole again.
         *
         * @param newest   The newest histogram read back since the last frame; none if none was.
         * @param seconds  Wall-clock time since the last frame.
         * @param settings The frame's.
         */
        float update(const uint32_t* newest, float seconds, const RenderSettings& settings);

        /// Take the next read whole, as for a replaced world; the stops hold until it comes.
        void forget() { m_snap = true; }

    private:
        float m_target  = 0.0f;   ///< What the newest lit read asks for.
        float m_stops   = 0.0f;   ///< Adapted toward m_target, RenderSettings::exposure aside.
        bool  m_adapted = false;  ///< m_stops holds a read.
        bool  m_snap    = true;   ///< The next read is taken whole.
};

} // namespace Exposure

} // namespace Vkm::Engine
