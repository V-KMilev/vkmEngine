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
 * A server sends the world thirty to sixty times a second and a client draws it
 * a hundred and forty-four times a second, so most frames have no news. Drawing
 * the newest thing heard makes everything a client does not own move in steps -
 * which is what a player calls stuttering, and no amount of bandwidth fixes it,
 * because the frames in between never had anything to say.
 *
 * So a client deliberately draws the past. It keeps the last few positions the
 * server gave for each entity, picks a moment slightly behind the newest one it
 * has heard, and draws where things were at that moment - which is a moment it
 * has real data on both sides of. The cost is that everything except the
 * player's own character is shown a fraction of a second late; the benefit is
 * that it moves smoothly and truthfully. Every game that feels good does this.
 *
 * The delay is measured in ticks rather than seconds because a snapshot is
 * identified by the tick it describes, and because a project that raises its
 * tick rate should not have to re-tune this.
 *
 * The entity a client owns is never interpolated. It is predicted instead - the
 * player would notice their own character running late, and nobody notices a
 * crate doing it.
 */
class NetInterpolation {
    public:
        NetInterpolation() = default;
        ~NetInterpolation() = default;

        NetInterpolation(const NetInterpolation& other) = delete;
        NetInterpolation& operator=(const NetInterpolation& other) = delete;

        NetInterpolation(NetInterpolation && other) = delete;
        NetInterpolation& operator=(NetInterpolation && other) = delete;

        /**
         * @brief How far behind the newest news to draw, in snapshots.
         *
         * Two. One is not enough: a single lost or late packet then leaves
         * nothing to interpolate toward and the world stalls until the next
         * arrives. Three would be smoother still and further behind, which is
         * the trade.
         *
         * What it costs in milliseconds is set by the snapshot rate, not here:
         * two snapshots is 62 ms at 32 a second and 31 ms at 64. That is why
         * the rate is the lever for how late another player looks, and this
         * number is the lever for how much loss it takes to stall them.
         *
         * Counted in snapshots rather than in ticks, because a snapshot
         * interval is what the delay has to cover and that is a duration. Four
         * ticks is two snapshots at the engine's default of 64 ticks against 32
         * snapshots a second, and one snapshot at the 128 a project is free to
         * ask for - so a project that raised its tick rate for a crisper
         * simulation silently halved the cushion its smoothing had.
         */
        static constexpr float SNAPSHOTS_BEHIND = 2.0f;

        /**
         * @brief That delay in ticks, for the rates this game actually runs at.
         *
         * @param tickRate     Simulation ticks a second.
         * @param snapshotRate Snapshots a second.
         */
        static constexpr float delayTicks(float tickRate, float snapshotRate) {
            return SNAPSHOTS_BEHIND * (tickRate / snapshotRate);
        }

        /**
         * @brief Samples kept per entity.
         *
         * Enough to interpolate across a lost packet or two; past that a body is
         * better snapped than guessed at. Counted in samples, so what it covers
         * in milliseconds falls as the snapshot rate rises - eight at 64 a
         * second is the 125 ms four covered at 32, which is the tolerance this
         * is actually chosen for.
         */
        static constexpr size_t HISTORY = 8;

    public:
        /**
         * @brief Record where the authority says @p entity is, as of @p tick.
         *
         * Called for every entity a snapshot spoke about, with the transform as
         * the snapshot left it - before anything smooths it, because what is
         * remembered has to be what was said.
         */
        void record(EntityId entity, uint32_t tick,
                    const glm::vec3& position, const glm::quat& rotation);

        /**
         * @brief Put the confirmed transform back, for the entities that have one.
         *
         * Called before a snapshot is applied. Presence in a snapshot is
         * measured against what the receiver was last told, so an absent
         * component means "the same as last time" - and if the smoothed value
         * is sitting in the component when the next snapshot lands, "the same
         * as last time" silently means "the same as the smoothed value", and
         * the error compounds every snapshot for as long as the body is still.
         */
        void restoreConfirmed(Scene& scene) const;

        /**
         * @brief Move every tracked entity to where it was, a moment ago.
         *
         * @param scene        World to write into.
         * @param deltaTime    Real seconds since the last frame.
         * @param tickRate     Ticks a second, to turn one into the other.
         * @param snapshotRate Snapshots a second, which is what the delay is
         *                     measured in.
         */
        void apply(Scene& scene, float deltaTime, float tickRate, float snapshotRate);

        /**
         * @brief Stop drawing @p entity late, without forgetting where it was.
         *
         * For a body this end has started simulating itself - one its own
         * character is pushing. Drawing it a moment behind while also
         * predicting it forward would be two answers fighting over one
         * transform, at the frame rate.
         *
         * Held rather than forgotten, because the track is this end's only
         * record of what the server last *said* about that body. A client that
         * forgot it and then predicted a push the server did not agree with
         * would hold a displaced body with nothing anywhere to correct it: the
         * server, seeing no change of its own, writes it into no further
         * snapshot at all. Kept, the moment the hold lifts is the moment the
         * body slides back to the server's last word, needing no new mechanism.
         */
        void hold(EntityId entity, bool held);

        /// Stop tracking @p entity, for one that left the world.
        void forget(EntityId entity);

        /// Stop tracking everything.
        void clear();

        size_t tracked() const { return m_tracks.size(); }

        /**
         * @brief Where the clock that decides what is drawn currently stands, in ticks.
         *
         * For the panel that shows how far behind a client is running.
         */
        float renderTick() const { return m_renderTick; }

        /**
         * @brief How far behind the newest news it is drawing, in ticks.
         *
         * Measured rather than assumed. The clock is rate-corrected toward the
         * delay above rather than set to it, so where it actually stands is the
         * honest number to report.
         *
         * @return Ticks behind, or zero before anything has arrived.
         */
        float behindTicks() const {
            return m_started ? static_cast<float>(m_newestTick) - m_renderTick : 0.0f;
        }

    private:
        /// One entity's samples, plus whether this end has taken over drawing it.
        struct Track {
            EntityId             entity;  ///< Which occupant of the slot these describe.
            SampleTrack<HISTORY> history;
            bool                 held = false;
        };

    private:
        std::unordered_map<uint32_t, Track> m_tracks;
        float    m_renderTick = 0.0f;
        uint32_t m_newestTick = 0;
        bool     m_started    = false;
};

} // namespace Vkm::Engine
