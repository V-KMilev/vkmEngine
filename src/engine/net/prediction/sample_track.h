#pragma once

#include <algorithm>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Vkm::Engine {

/**
 * @brief A short history of where one entity was, kept by tick.
 *
 * Oldest first; answers "where was this at time t".
 *
 * @tparam N How many samples to keep.
 */
template <size_t N>
struct SampleTrack {
    struct Sample {
        uint32_t  tick = 0;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    Sample samples[N];  ///< Oldest first, newest last; the first count are held.
    size_t count = 0;   ///< Samples held.

    /**
     * @brief Remember @p position and @p rotation as of @p tick.
     *
     * A sample not newer than the newest held is dropped, keeping the order
     * poseAt relies on.
     *
     * @param tick     The moment the sample describes.
     * @param position Where the entity was then.
     * @param rotation How it was turned then.
     */
    void record(uint32_t tick, const glm::vec3& position, const glm::quat& rotation) {
        if (count > 0 && tick <= samples[count - 1].tick) return;

        if (count == N) {
            std::move(samples + 1, samples + N, samples);
            --count;
        }
        samples[count++] = {tick, position, rotation};
    }

    /**
     * @brief Where the entity was at @p tick, which may fall between two samples.
     *
     * Past either end the nearest moment is given, not extrapolated: an
     * overshoot yanked back reads worse than holding still.
     *
     * @param tick     The moment wanted; a double, since a tick count outgrows
     *                 a float's precision within hours.
     * @param position Filled with the pose's position.
     * @param rotation Filled with the pose's rotation.
     * @return False when nothing is remembered at all.
     */
    bool poseAt(double tick, glm::vec3& position, glm::quat& rotation) const {
        if (count == 0) return false;

        if (tick <= static_cast<double>(samples[0].tick)) {
            position = samples[0].position;
            rotation = samples[0].rotation;
            return true;
        }
        const Sample& newest = samples[count - 1];
        if (tick >= static_cast<double>(newest.tick)) {
            position = newest.position;
            rotation = newest.rotation;
            return true;
        }

        for (size_t i = 0; i + 1 < count; ++i) {
            const Sample& from = samples[i];
            const Sample& to   = samples[i + 1];
            if (tick < static_cast<double>(from.tick) || tick > static_cast<double>(to.tick)) continue;

            const double span = static_cast<double>(to.tick - from.tick);
            const float  at   = span > 0.0
                ? static_cast<float>((tick - static_cast<double>(from.tick)) / span)
                : 1.0f;
            position = glm::mix(from.position, to.position, at);
            // slerp: a linear blend speeds up through the middle of a turn.
            rotation = glm::slerp(from.rotation, to.rotation, at);
            return true;
        }
        return false;
    }

    /// The newest moment remembered, or zero when nothing is.
    uint32_t newest() const { return count > 0 ? samples[count - 1].tick : 0u; }
};

} // namespace Vkm::Engine
