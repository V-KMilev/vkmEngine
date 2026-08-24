#include "proc_audio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/gtc/constants.hpp>

#include "core/math/random.h"

namespace Vkm::Engine {

namespace {

constexpr uint32_t FOOTSTEP_RATE    = 44100;
constexpr float    FOOTSTEP_SECONDS = 0.13f;

// Ramps at both ends. The head is short enough to keep the impact sharp, the
// tail long enough that the decay is already near zero when it lands on it.
constexpr float ATTACK_SECONDS = 0.002f;
constexpr float RELEASE_SECONDS = 0.020f;

// The body: a sine dropping from a heel-strike thwack toward a floor thud.
constexpr float BODY_START_HZ = 170.0f;
constexpr float BODY_END_HZ   = 55.0f;
constexpr float BODY_SWEEP    = 0.025f;  ///< Seconds the drop takes, as a time constant.
constexpr float BODY_DECAY    = 0.030f;
constexpr float BODY_GAIN     = 0.62f;

// The scuff over it: noise, lowpassed so it reads as grit rather than hiss.
constexpr float SCUFF_DECAY   = 0.018f;
constexpr float SCUFF_GAIN    = 0.45f;
constexpr float SCUFF_CUTOFF  = 2500.0f;

// Any seed does; a fixed one is what makes every launch of the game hear the
// same footstep instead of a different one each time.
constexpr uint64_t NOISE_SEED = 0x5F3759DFu;

constexpr uint32_t CHIME_RATE    = 44100;
constexpr float    CHIME_SECONDS = 0.18f;

// The chime's two partials: a fundamental and the fifth above it, the upper one
// decaying faster so the strike is bright and the tail is not.
constexpr float CHIME_LOW_HZ     = 1180.0f;
constexpr float CHIME_HIGH_HZ    = CHIME_LOW_HZ * 1.5f;
constexpr float CHIME_LOW_DECAY  = 0.075f;
constexpr float CHIME_HIGH_DECAY = 0.035f;
constexpr float CHIME_LOW_GAIN   = 0.38f;
constexpr float CHIME_HIGH_GAIN  = 0.24f;

// A breath of noise under the first few milliseconds: the sound of the strike
// itself, without which two sines start too cleanly to read as a struck object.
constexpr float CHIME_STRIKE_DECAY = 0.006f;
constexpr float CHIME_STRIKE_GAIN  = 0.20f;

constexpr float CHIME_ATTACK_SECONDS  = 0.001f;
constexpr float CHIME_RELEASE_SECONDS = 0.035f;

// A different stream of the same generator, so the chime's noise and the
// footstep's are uncorrelated rather than the same numbers twice.
constexpr uint64_t CHIME_SEED = 0x2545F491u;

} // namespace

AudioClipAsset makeFootstepSound() {
    const auto frames = static_cast<size_t>(FOOTSTEP_SECONDS * static_cast<float>(FOOTSTEP_RATE));
    const float step  = 1.0f / static_cast<float>(FOOTSTEP_RATE);

    auto samples = std::make_shared<std::vector<int16_t>>();
    samples->reserve(frames);

    Math::Rng rng(NOISE_SEED);
    // One-pole lowpass, as a per-sample blend toward the new noise value.
    const float scuffBlend = 1.0f - std::exp(-glm::two_pi<float>() * SCUFF_CUTOFF * step);
    float scuff = 0.0f;
    float phase = 0.0f;

    for (size_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) * step;

        const float hz = BODY_END_HZ + (BODY_START_HZ - BODY_END_HZ) * std::exp(-t / BODY_SWEEP);
        phase += glm::two_pi<float>() * hz * step;
        const float body = std::sin(phase) * std::exp(-t / BODY_DECAY) * BODY_GAIN;

        scuff += (rng.nextFloat() * 2.0f - 1.0f - scuff) * scuffBlend;
        const float grit = scuff * std::exp(-t / SCUFF_DECAY) * SCUFF_GAIN;

        const float remaining = FOOTSTEP_SECONDS - t;
        const float envelope  = std::min(1.0f, std::min(t / ATTACK_SECONDS,
                                                        remaining / RELEASE_SECONDS));
        const float value = std::clamp((body + grit) * envelope, -1.0f, 1.0f);
        samples->push_back(static_cast<int16_t>(value * 32767.0f));
    }

    AudioClipAsset clip;
    clip.sampleRate = FOOTSTEP_RATE;
    clip.channels   = 1;
    clip.samples    = std::move(samples);
    return clip;
}

AudioClipAsset makeCoinChime() {
    const auto frames = static_cast<size_t>(CHIME_SECONDS * static_cast<float>(CHIME_RATE));
    const float step  = 1.0f / static_cast<float>(CHIME_RATE);

    auto samples = std::make_shared<std::vector<int16_t>>();
    samples->reserve(frames);

    Math::Rng rng(CHIME_SEED);

    for (size_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) * step;

        const float low  = std::sin(glm::two_pi<float>() * CHIME_LOW_HZ * t)
                         * std::exp(-t / CHIME_LOW_DECAY) * CHIME_LOW_GAIN;
        const float high = std::sin(glm::two_pi<float>() * CHIME_HIGH_HZ * t)
                         * std::exp(-t / CHIME_HIGH_DECAY) * CHIME_HIGH_GAIN;
        const float strike = (rng.nextFloat() * 2.0f - 1.0f)
                           * std::exp(-t / CHIME_STRIKE_DECAY) * CHIME_STRIKE_GAIN;

        const float remaining = CHIME_SECONDS - t;
        const float envelope  = std::min(1.0f, std::min(t / CHIME_ATTACK_SECONDS,
                                                        remaining / CHIME_RELEASE_SECONDS));
        const float value = std::clamp((low + high + strike) * envelope, -1.0f, 1.0f);
        samples->push_back(static_cast<int16_t>(value * 32767.0f));
    }

    AudioClipAsset clip;
    clip.sampleRate = CHIME_RATE;
    clip.channels   = 1;
    clip.samples    = std::move(samples);
    return clip;
}

} // namespace Vkm::Engine
