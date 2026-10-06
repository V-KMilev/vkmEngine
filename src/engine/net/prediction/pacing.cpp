#include "net/prediction/pacing.h"

#include <algorithm>
#include <cmath>

namespace Vkm::Engine {

namespace {

/**
 * @brief How much of each reading the smoothed low water and its spread take.
 *
 * Settles slower than one late packet, faster than a round trip of steering.
 */
constexpr float SMOOTHING = 0.1f;

/**
 * @brief The low water held on a link that does not jitter, in commands.
 *
 * Counted before a take, so two leaves one behind the command that runs: a
 * packet may be a little late for nothing.
 */
constexpr float TARGET_FLOOR = 2.0f;

/**
 * @brief How many times the readings' spread the target adds above the floor.
 *
 * A reading rarely lands further than twice its mean distance below the mean.
 */
constexpr float SPREAD_MARGIN = 2.0f;

/**
 * @brief The deepest low water ever aimed for, in commands.
 *
 * Half NetCommandBuffer::MAX_DEPTH, leaving room to swing without being trimmed.
 */
constexpr float TARGET_CEILING = static_cast<float>(NetCommandBuffer::MAX_DEPTH) / 2.0f;

/**
 * @brief The least time the loop is asked to close an error in, in seconds,
 *        on top of two round trips.
 *
 * A bend shows a round trip later, plus the smoothing; a loop asked to close
 * faster than that overshoots and swings between the bounds.
 */
constexpr float SETTLE_SECONDS = 0.25f;

/**
 * @brief What share of the proportional gain the integral takes, per snapshot.
 *
 * Small: the integral finds a drift over a few seconds; faster, it would
 * overshoot with the loop's delay.
 */
constexpr float DRIFT_SHARE = 0.0125f;

} // namespace

void NetPacing::report(uint32_t lowWater, float roundTrip, float tickRate) {
    const float reading = static_cast<float>(std::min(lowWater, NET_QUEUE_DEPTH_MAX));

    if (!m_started) {
        m_started = true;
        m_depth   = reading;
    }
    m_depth  += (reading - m_depth) * SMOOTHING;
    m_spread += (std::abs(reading - m_depth) - m_spread) * SMOOTHING;

    // Positive is too deep: this end is ahead, and slows down. The gain that
    // closes a command of error over the loop's delay is one over the commands
    // that delay spans.
    const float error = m_depth - target();
    const float gain  = 1.0f / (std::max(tickRate, 1.0f) * (2.0f * roundTrip + SETTLE_SECONDS));
    m_drift = std::clamp(m_drift - error * gain * DRIFT_SHARE, -MAX_DILATION, MAX_DILATION);
    m_scale = 1.0f + std::clamp(m_drift - error * gain, -MAX_DILATION, MAX_DILATION);
}

float NetPacing::target() const {
    return std::min(TARGET_FLOOR + SPREAD_MARGIN * m_spread, TARGET_CEILING);
}

} // namespace Vkm::Engine
