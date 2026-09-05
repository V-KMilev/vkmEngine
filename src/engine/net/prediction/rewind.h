#pragma once

#include <cstdint>
#include <unordered_map>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "net/prediction/sample_track.h"

namespace Vkm::Engine {

class Scene;
class NetSession;

/**
 * @brief Where every player was, for as long as it takes a shot to arrive.
 *
 * A player aims at what they can see, which is a moment already past: their
 * screen is showing them the world as it was a round trip plus an interpolation
 * delay ago. By the time the server hears about the shot, everyone has moved.
 * Judging it against the present means a player who aimed correctly misses, and
 * the better their aim and the worse their connection, the more often - which
 * reads as the game being broken rather than as latency.
 *
 * So the server keeps the last few dozen ticks of every player's pose and puts
 * them back for exactly the length of one query.
 *
 * Only players. That is a real limit and not a simplification: a moving
 * platform, a swinging door, a thrown crate are judged against the present, so a
 * shot at one of those can miss for the reason this exists to remove. Players
 * are what players shoot at, they are bounded by the seat count, and extending
 * this to any body means keeping a ring for every body in the world.
 */
class NetRewind {
    public:
        NetRewind() = default;
        ~NetRewind() = default;

        NetRewind(const NetRewind& other) = delete;
        NetRewind& operator=(const NetRewind& other) = delete;

        NetRewind(NetRewind && other) = delete;
        NetRewind& operator=(NetRewind && other) = delete;

        /**
         * @brief Ticks of history kept per player.
         *
         * Half a second at 128 Hz, a second at the engine's default of 64. That
         * covers a round trip of four hundred milliseconds plus the
         * interpolation delay, which is worse than a game is playable on.
         *
         * In ticks rather than seconds, like every constant here - with the
         * consequence that the furthest a shot can reach back is a function of
         * a project's tick rate.
         */
        static constexpr size_t HISTORY_TICKS = 64;

    public:
        /// Remember where @p entity was on @p tick, in the server's own clock.
        void record(EntityId entity, uint32_t tick,
                    const glm::vec3& position, const glm::quat& rotation);

        /**
         * @brief Where @p entity was at @p tick, which may fall between two.
         *
         * A client draws a sub-tick moment - its render clock is a float - so
         * asking for one is not a rounding error but the question.
         *
         * @return False when nothing is remembered for that entity, or the
         *         moment is outside what is kept.
         */
        bool poseAt(EntityId entity, float tick,
                    glm::vec3& position, glm::quat& rotation) const;

        /**
         * @brief Forget @p entity, for a seat changing hands.
         *
         * A seat outlives the player in it, and a stale ring would rewind the next
         * player to the last one's positions.
         */
        void forget(EntityId entity);

        void clear() { m_tracks.clear(); }

        size_t tracked() const { return m_tracks.size(); }

    private:
        std::unordered_map<uint32_t, SampleTrack<HISTORY_TICKS>> m_tracks;
};

/**
 * @brief Put the world back to when a player fired, for one scope.
 *
 * The whole project-facing surface, and a scope rather than a wrapped query on
 * purpose: it serves raycast, spherecast and anything added later without a
 * wrapper each, and inside it a game writes the query it would have written
 * anyway. Offline and on a client it does nothing, so the same code is a plain
 * query in a single-player game.
 *
 * @code
 * {
 *     NetRewindScope rewound(scene, net, entity());
 *     RayHit hit;
 *     if (raycast(scene, origin, direction, range, hit, filter)) { ... }
 * }
 * @endcode
 *
 * Read inside the braces, act outside them: every rewound transform is restored
 * exactly on the way out, so a knockback written while the world is rewound is
 * thrown away.
 */
class NetRewindScope {
    public:
        NetRewindScope(Scene& scene, NetSession& net, EntityId shooter);
        ~NetRewindScope();

        NetRewindScope(const NetRewindScope& other) = delete;
        NetRewindScope& operator=(const NetRewindScope& other) = delete;

        NetRewindScope(NetRewindScope && other) = delete;
        NetRewindScope& operator=(NetRewindScope && other) = delete;

    private:
        Scene&      m_scene;
        NetSession& m_net;
};

} // namespace Vkm::Engine
