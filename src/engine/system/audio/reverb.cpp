#include "system/audio/reverb.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Vkm::Engine {

namespace {

/// Each channel's combs and all-passes, Freeverb's, in samples at 44.1 kHz.
constexpr std::array<uint32_t, 4> COMB_SAMPLES    = {1116, 1188, 1277, 1356};
constexpr std::array<uint32_t, 2> ALLPASS_SAMPLES = {556, 441};
constexpr double                  TUNED_RATE      = 44100.0;

/// How much longer each further channel's lines are, for width.
constexpr uint32_t SPREAD_SAMPLES = 23;

/// The comb's low-pass in its feedback: higher dulls the tail sooner, as soft walls do.
constexpr float DAMPING = 0.25f;

/// The all-passes' feedback, Freeverb's.
constexpr float ALLPASS_FEEDBACK = 0.5f;

/// What the input is scaled by on its way into the combs, which sum their echoes of it.
constexpr float INPUT_GAIN = 0.12f;

/// Decay is held between these, in seconds.
constexpr float SHORTEST = 0.1f;
constexpr float LONGEST  = 10.0f;

/// A line of @p samples at TUNED_RATE, as long in time at @p rate.
uint32_t scaled(uint32_t samples, uint32_t rate) {
    const double length = std::round(static_cast<double>(samples) * static_cast<double>(rate) / TUNED_RATE);
    return std::max<uint32_t>(1, static_cast<uint32_t>(length));
}

} // namespace

void ReverbTank::prepare(uint32_t sampleRate, uint32_t channels) {
    m_rate     = std::max<uint32_t>(sampleRate, 1);
    m_channels = channels;
    m_combs.assign(static_cast<size_t>(channels) * COMB_SAMPLES.size(), Line{});
    m_allpasses.assign(static_cast<size_t>(channels) * ALLPASS_SAMPLES.size(), Line{});
    for (uint32_t c = 0; c < channels; ++c) {
        const uint32_t spread = c * SPREAD_SAMPLES;
        for (size_t i = 0; i < COMB_SAMPLES.size(); ++i) {
            Line& line = m_combs[c * COMB_SAMPLES.size() + i];
            line.buffer.assign(scaled(COMB_SAMPLES[i] + spread, m_rate), 0.0f);
        }
        for (size_t i = 0; i < ALLPASS_SAMPLES.size(); ++i) {
            Line& line = m_allpasses[c * ALLPASS_SAMPLES.size() + i];
            line.buffer.assign(scaled(ALLPASS_SAMPLES[i] + spread, m_rate), 0.0f);
            line.feedback = ALLPASS_FEEDBACK;
        }
    }
    m_tunedDecay = -1.0f;
    retune();
}

void ReverbTank::setDecay(float seconds) {
    const float held = std::isfinite(seconds) ? std::clamp(seconds, SHORTEST, LONGEST) : SHORTEST;
    m_decay.store(held, std::memory_order_relaxed);
}

void ReverbTank::setWet(float wet) {
    m_wet.store(std::isfinite(wet) ? std::max(0.0f, wet) : 0.0f, std::memory_order_relaxed);
}

void ReverbTank::retune() {
    const float decay = m_decay.load(std::memory_order_relaxed);
    if (decay == m_tunedDecay) return;
    m_tunedDecay = decay;
    // A comb of d samples loses 60 dB in decay seconds when it keeps 10^(-3 d / (decay * rate))
    // of each pass.
    for (Line& comb : m_combs) {
        const float d = static_cast<float>(comb.buffer.size());
        comb.feedback = std::pow(10.0f, -3.0f * d / (decay * static_cast<float>(m_rate)));
    }
}

void ReverbTank::process(const float* in, float* out, uint32_t frames) {
    if (m_channels == 0 || out == nullptr) return;
    retune();
    const float  wet       = m_wet.load(std::memory_order_relaxed);
    const bool   silencing = m_silencing.exchange(false, std::memory_order_relaxed);
    const size_t combs     = COMB_SAMPLES.size();
    const size_t alls      = ALLPASS_SAMPLES.size();
    for (uint32_t f = 0; f < frames; ++f) {
        // Let go over the block rather than cut, which would click.
        const float fade = silencing ? 1.0f - static_cast<float>(f + 1) / static_cast<float>(frames) : 1.0f;
        // No input is silence going in; the tail still rings out.
        float mono = 0.0f;
        if (in != nullptr) {
            for (uint32_t c = 0; c < m_channels; ++c) mono += in[f * m_channels + c];
        }
        // One bad sample would ring in the lines for good, every shaped voice's tail with it.
        mono = std::isfinite(mono) ? mono * INPUT_GAIN : 0.0f;
        for (uint32_t c = 0; c < m_channels; ++c) {
            float sum = 0.0f;
            for (size_t i = 0; i < combs; ++i) {
                Line& comb = m_combs[c * combs + i];
                const float echo = comb.buffer[comb.at];
                comb.damped = echo * (1.0f - DAMPING) + comb.damped * DAMPING;
                comb.buffer[comb.at] = mono + comb.damped * comb.feedback;
                comb.at = (comb.at + 1) % static_cast<uint32_t>(comb.buffer.size());
                sum += echo;
            }
            for (size_t i = 0; i < alls; ++i) {
                Line& pass = m_allpasses[c * alls + i];
                const float echo = pass.buffer[pass.at];
                pass.buffer[pass.at] = sum + echo * pass.feedback;
                pass.at = (pass.at + 1) % static_cast<uint32_t>(pass.buffer.size());
                sum = echo - sum;
            }
            out[f * m_channels + c] = sum * wet * fade;
        }
    }
    if (!silencing) return;
    for (Line& comb : m_combs) {
        std::fill(comb.buffer.begin(), comb.buffer.end(), 0.0f);
        comb.damped = 0.0f;
    }
    for (Line& pass : m_allpasses) std::fill(pass.buffer.begin(), pass.buffer.end(), 0.0f);
}

} // namespace Vkm::Engine
