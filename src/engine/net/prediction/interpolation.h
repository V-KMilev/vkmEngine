#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "net/prediction/sample_track.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Draws entities this end is only told about, between what it was told.
 *
 * Most frames bring no snapshot, so drawing the newest would move in steps.
 * Instead a client draws a moment slightly behind the newest, with data on
 * both sides of it, at the cost of showing the world a little late. The
 * entity a client owns is predicted, never interpolated.
 */
class NetInterpolation {
    public:
        /**
         * @brief The least that is drawn behind the newest news, in snapshots.
         *
         * With one, a single lost packet leaves nothing to interpolate toward
         * and the world stalls. Jitter grows the delay past this (delayTicks()).
         * The snapshot rate sets what it costs in milliseconds.
         */
        static constexpr float SNAPSHOTS_BEHIND = 2.0f;

        /**
         * @brief Samples kept per entity.
         *
         * Enough to cross a lost packet or two; past that a body is better
         * snapped than guessed at.
         */
        static constexpr size_t HISTORY = 8;

        /**
         * @brief The most that is drawn behind the newest news, in snapshots,
         *        however badly the link jitters.
         *
         * Half of HISTORY, so samples either side of the moment drawn survive
         * a further loss or two.
         */
        static constexpr float MAX_SNAPSHOTS_BEHIND = static_cast<float>(HISTORY) / 2.0f;

        static_assert(
            SNAPSHOTS_BEHIND <= MAX_SNAPSHOTS_BEHIND,
            "the least delay has to fit the samples kept"
        );

    public:
        NetInterpolation() = default;
        ~NetInterpolation() = default;

        NetInterpolation(const NetInterpolation& other) = delete;
        NetInterpolation& operator=(const NetInterpolation& other) = delete;

        NetInterpolation(NetInterpolation && other) = delete;
        NetInterpolation& operator=(NetInterpolation && other) = delete;

    public:
        /**
         * @brief Record where the authority says @p entity is, as of @p tick.
         *
         * With the transform as the snapshot left it, before any smoothing.
         *
         * @param entity   The entity described; a new occupant of a tracked slot
         *                 starts a fresh track.
         * @param tick     The server tick the snapshot names.
         * @param position Where the snapshot put it.
         * @param rotation How the snapshot turned it.
         */
        void record(EntityId entity, uint32_t tick, const glm::vec3& position, const glm::quat& rotation);

        /**
         * @brief Note that a snapshot of @p tick has arrived, now.
         *
         * Once per snapshot. How far each arrival is off its due time is the
         * jitter the delay grows to ride out; a fixed delay would run the
         * clock into the newest sample and the world would pause.
         *
         * @param tick     The server tick the snapshot's header names.
         * @param tickRate Ticks a second, to turn ticks into time.
         */
        void heard(uint32_t tick, float tickRate);

        /**
         * @brief How far behind the newest news to draw, in ticks.
         *
         * One snapshot interval plus twice the measured jitter, clamped to
         * SNAPSHOTS_BEHIND..MAX_SNAPSHOTS_BEHIND.
         *
         * @param tickRate     Simulation ticks a second.
         * @param snapshotRate Snapshots a second.
         * @return The delay, in ticks.
         */
        float delayTicks(float tickRate, float snapshotRate) const;

        /**
         * @brief Put the confirmed transform back, for the entities that have one.
         *
         * Before a snapshot is applied: an absent component means "as last
         * time", and left smoothed, the error would compound every snapshot.
         *
         * @param scene World the snapshot is about to be read into.
         */
        void restoreConfirmed(Scene& scene);

        /**
         * @brief Move every tracked entity to where it was, a moment ago.
         *
         * @param scene        World to write into.
         * @param deltaTime    Real seconds since the last frame.
         * @param tickRate     Ticks a second.
         * @param snapshotRate Snapshots a second, the delay's unit.
         */
        void apply(Scene& scene, float deltaTime, float tickRate, float snapshotRate);

        /**
         * @brief Stop drawing @p entity late, without forgetting where it was.
         *
         * For a body this end now predicts; drawing it late too would fight
         * the prediction. The track is kept as the only record of the server's
         * last word, which may never be sent again: when the hold lifts, the
         * body slides back to it.
         *
         * @param entity The body this end has taken over or given back.
         * @param held   True to stop drawing it late, false to resume.
         */
        void hold(EntityId entity, bool held);

        /// Stop tracking @p entity, for one that left the world.
        void forget(EntityId entity);

        void clear();

        size_t tracked() const { return m_tracks.size(); }

        /**
         * @brief Where the clock that decides what is drawn currently stands, in ticks.
         *
         * A double: a float's spacing reaches half a tick at 2^22 ticks.
         *
         * @return The render clock, in server ticks.
         */
        double renderTick() const { return m_renderTick; }

        /**
         * @brief How far behind the newest news it is drawing, in ticks.
         *
         * Where the clock actually stands; it is eased toward the delay, not set.
         *
         * @return Ticks behind, or zero before anything has arrived.
         */
        float behindTicks() const {
            return m_started ? static_cast<float>(static_cast<double>(m_newestTick) - m_renderTick) : 0.0f;
        }

    private:
        /// One entity's samples, plus whether this end has taken over drawing it.
        struct Track {
            EntityId             entity;  ///< Which occupant of the slot these describe.
            SampleTrack<HISTORY> history;
            bool                 held = false;
        };

        /// Whether an inactive ragdoll poses the entity at @p slot, as last marked.
        bool isPosed(uint32_t slot) const { return slot < m_posed.size() && m_posed[slot]; }

    private:
        std::unordered_map<uint32_t, Track> m_tracks;

        /// Which slots inactive ragdolls pose, marked once per call.
        std::vector<bool> m_posed;

        double   m_renderTick = 0.0;
        uint32_t m_newestTick = 0;
        bool     m_started    = false;

        double   m_seconds    = 0.0;    ///< Real time apply() has advanced by, to stamp arrivals with.
        double   m_heardAt    = 0.0;    ///< When the last snapshot arrived.
        uint32_t m_heardTick  = 0;      ///< The tick it named.
        float    m_jitter     = 0.0f;   ///< Smoothed distance of an arrival from its due time, in seconds.
        bool     m_heardAny   = false;
};

} // namespace Vkm::Engine
