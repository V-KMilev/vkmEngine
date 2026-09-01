#pragma once

#include <algorithm>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Vkm::Engine {

/**
 * @brief A short history of where one entity was, kept by tick.
 *
 * Two things in this subsystem remember the same thing for opposite reasons: a
 * client keeps the last few positions a server gave so it can draw a moment it
 * has data either side of, and a server keeps the last few of every player so a
 * shot can be judged against what the player who fired it could see. Both want
 * a fixed ring, ordered by tick, that answers "where was this at time t".
 *
 * The ring is a plain array rather than a deque: it is small, its size is a
 * compile-time property of what it is for, and the search walks it in order.
 *
 * @tparam N How many samples to keep. The two users pick different numbers for
 *           different reasons, which is why it is a parameter and not a
 *           constant.
 */
template <size_t N>
struct SampleTrack {
    struct Sample {
        uint32_t  tick = 0;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    };

    Sample samples[N];
    size_t count = 0;   ///< Newest last.

    /**
     * @brief Remember @p position and @p rotation as of @p tick.
     *
     * A sample older than the newest held is dropped: the newer one already
     * describes a moment past it, and inserting it would break the ordering
     * poseAt relies on. Out-of-order arrival is normal on a link that reorders.
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
     * Past either end the nearest remembered moment is given rather than an
     * extrapolation: a guess past what is known overshoots and is yanked back,
     * which reads worse than holding still.
     *
     * @param tick     The moment wanted; a float, because a client draws
     *                 between ticks.
     * @param position Filled with the pose's position.
     * @param rotation Filled with the pose's rotation.
     * @return False when nothing is remembered at all.
     */
    bool poseAt(float tick, glm::vec3& position, glm::quat& rotation) const {
        if (count == 0) return false;

        if (tick <= static_cast<float>(samples[0].tick)) {
            position = samples[0].position;
            rotation = samples[0].rotation;
            return true;
        }
        const Sample& newest = samples[count - 1];
        if (tick >= static_cast<float>(newest.tick)) {
            position = newest.position;
            rotation = newest.rotation;
            return true;
        }

        for (size_t i = 0; i + 1 < count; ++i) {
            const Sample& from = samples[i];
            const Sample& to   = samples[i + 1];
            if (tick < static_cast<float>(from.tick) || tick > static_cast<float>(to.tick)) continue;

            const float span = static_cast<float>(to.tick - from.tick);
            const float at   = span > 0.0f ? (tick - static_cast<float>(from.tick)) / span : 1.0f;
            position = glm::mix(from.position, to.position, at);
            // slerp rather than mix: a linear blend of two quaternions is not a
            // rotation at a constant rate, and shows as a body speeding up
            // through the middle of every turn.
            rotation = glm::slerp(from.rotation, to.rotation, at);
            return true;
        }
        return false;
    }

    /// The newest moment remembered, or zero when nothing is.
    uint32_t newest() const { return count > 0 ? samples[count - 1].tick : 0u; }
};

} // namespace Vkm::Engine
