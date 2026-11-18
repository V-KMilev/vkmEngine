#include "system/render/exposure.h"

#include <algorithm>
#include <cmath>

#include "io/project/render_settings.h"

namespace Vkm::Engine::Exposure {

std::optional<float> targetStops(const uint32_t* bins, const RenderSettings& settings) {
    uint64_t lit = 0;
    for (int bin = 1; bin < BINS; ++bin) lit += bins[bin];
    if (lit == 0) return std::nullopt;

    // Each bin's count clipped to the part of it inside the window, weighted at its center.
    const double low  = static_cast<double>(lit) * LOW_SHARE;
    const double high = static_cast<double>(lit) * HIGH_SHARE;
    double below  = 0.0;
    double weight = 0.0;
    double sum    = 0.0;
    for (int bin = 1; bin < BINS; ++bin) {
        const double count  = bins[bin];
        const double inside = std::min(below + count, high) - std::max(below, low);
        below += count;
        if (inside <= 0.0) continue;
        weight += inside;
        sum    += inside * binCenter(bin);
    }
    if (weight <= 0.0) return std::nullopt;

    const float stops = std::log2(KEY) - static_cast<float>(sum / weight);
    const float most  = std::max(settings.exposureMinimum, settings.exposureMaximum);
    return std::clamp(stops, settings.exposureMinimum, most);
}

float adaptStops(float current, float target, float seconds) {
    const float rate  = target > current ? ADAPT_BRIGHTER : ADAPT_DARKER;
    const float share = 1.0f - std::exp(-rate * std::max(seconds, 0.0f));
    return current + (target - current) * share;
}

float Adaptation::update(const uint32_t* newest, float seconds, const RenderSettings& settings) {
    if (!settings.autoExposure) {
        m_state = State::Unread;
        return settings.exposure;
    }

    const std::optional<float> target = newest ? targetStops(newest, settings) : std::nullopt;
    if (target) m_target = *target;
    if (target && m_state != State::Adapting) {
        m_stops = m_target;
        m_state = State::Adapting;
    } else if (m_state == State::Adapting) {
        m_stops = adaptStops(m_stops, m_target, seconds);
    }
    return m_state == State::Unread ? settings.exposure : m_stops + settings.exposure;
}

} // namespace Vkm::Engine::Exposure
