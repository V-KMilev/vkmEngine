#pragma once

#include <cstdint>
#include <unordered_map>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "net/prediction/sample_track.h"

namespace Vkm::Engine {

/**
 * @brief Where every player was, for as long as it takes a shot to arrive.
 *
 * A player aims at a moment already past, so the server keeps recent player
 * poses and puts them back for one query. Only players: anything else is
 * judged against the present and can miss for that reason.
 */
class NetRewind {
    public:
        /**
         * @brief Ticks of history kept per player.
         *
         * A second at Config::DEFAULT_TICK_RATE, covering a 400 ms round trip
         * plus the interpolation delay.
         */
        static constexpr size_t HISTORY_TICKS = 64;

    public:
        NetRewind() = default;
        ~NetRewind() = default;

        NetRewind(const NetRewind& other) = delete;
        NetRewind& operator=(const NetRewind& other) = delete;

        NetRewind(NetRewind && other) = delete;
        NetRewind& operator=(NetRewind && other) = delete;

    public:
        /// Remember where @p entity was on @p tick, in the server's own clock.
        void record(EntityId entity, uint32_t tick, const glm::vec3& position, const glm::quat& rotation);

        /**
         * @brief Where @p entity was at @p tick, which may fall between two.
         *
         * Fractional: a client's render clock draws between ticks.
         *
         * @param entity   The player asked about.
         * @param tick     The moment wanted, in the server's own clock.
         * @param position Filled with where it was.
         * @param rotation Filled with how it was turned.
         * @return False when nothing is remembered for that entity; a moment
         *         outside what is kept gets the nearest one kept.
         */
        bool poseAt(EntityId entity, double tick, glm::vec3& position, glm::quat& rotation) const;

        /**
         * @brief Forget @p entity, for a seat changing hands.
         *
         * A stale ring would rewind the next player to the last one's positions.
         *
         * @param entity The departed player's entity.
         */
        void forget(EntityId entity);

        void clear() { m_tracks.clear(); }

        size_t tracked() const { return m_tracks.size(); }

    private:
        std::unordered_map<uint32_t, SampleTrack<HISTORY_TICKS>> m_tracks;
};

} // namespace Vkm::Engine
