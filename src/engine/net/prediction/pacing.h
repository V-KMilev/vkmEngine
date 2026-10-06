#pragma once

#include <cstdint>

#include "net/prediction/command.h"

namespace Vkm::Engine {

/**
 * @brief Bits a snapshot spends saying how low its reader's command queue ran.
 *
 * Enough to say a depth past NetCommandBuffer::MAX_DEPTH; deeper is said as the largest.
 */
constexpr uint32_t NET_QUEUE_DEPTH_BITS = 4;

/// The largest depth a snapshot can say.
constexpr uint32_t NET_QUEUE_DEPTH_MAX = (1u << NET_QUEUE_DEPTH_BITS) - 1u;

static_assert(
    NET_QUEUE_DEPTH_MAX > NetCommandBuffer::MAX_DEPTH,
    "a queue deep enough to be trimmed has to be able to say so"
);

/**
 * @brief How fast a client turns wall time into ticks, so the server's queue of
 *        its commands holds a small cushion rather than running dry or deep.
 *
 * The two clocks drift: a fast client fills the queue until it is trimmed
 * (dropped input), a slow one drains it until a tick runs on a repeat (a
 * misprediction). So the client steers NetCommandBuffer::lowWater toward a
 * cushion of a command or two, widened by the reading's jitter.
 *
 * Proportional plus integral (the integral holds a steady drift). Both gains
 * scale with the loop's delay - a round trip - and the tick rate; fixed gains
 * swing between the bounds on a long link. Only ticks per wall second bend,
 * by at most MAX_DILATION; the tick step does not, so replays are unaffected.
 * Exactly one until a snapshot has said anything.
 */
class NetPacing {
    public:
        /**
         * @brief The most the pacing bends either way.
         *
         * Covers clock drift and a frame loop running a little short, under
         * what a player notices in their character's speed.
         */
        static constexpr float MAX_DILATION = 0.05f;

    public:
        NetPacing() = default;
        ~NetPacing() = default;

        NetPacing(const NetPacing& other) = default;
        NetPacing& operator=(const NetPacing& other) = default;

        NetPacing(NetPacing && other) = default;
        NetPacing& operator=(NetPacing && other) = default;

    public:
        /**
         * @brief Take one snapshot's reading of the queue.
         *
         * @param lowWater  Fewest commands waiting at any tick since the
         *                  previous snapshot; at most NET_QUEUE_DEPTH_MAX.
         * @param roundTrip Round trip now, in seconds; zero before one is measured.
         * @param tickRate  Ticks a second, which is commands a second.
         */
        void report(uint32_t lowWater, float roundTrip, float tickRate);

        /**
         * @brief How much faster than wall time to run ticks.
         *
         * @return Within one MAX_DILATION either side of one, and exactly one
         *         before anything has been reported.
         */
        float scale() const { return m_scale; }

        /// The low water as smoothed over the last few snapshots, in commands.
        float depth() const { return m_depth; }

        /// The low water this is steering toward, in commands.
        float target() const;

        void clear() { *this = NetPacing{}; }

    private:
        float m_depth  = 0.0f;  ///< Smoothed low water.
        float m_spread = 0.0f;  ///< Smoothed distance of a reading from it.
        float m_drift  = 0.0f;  ///< The integral: the steady bend a drifting clock needs.
        float m_scale  = 1.0f;
        bool  m_started = false;
};

} // namespace Vkm::Engine
