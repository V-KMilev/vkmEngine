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

} // namespace Vkm::Engine
