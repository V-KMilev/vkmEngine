#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "net/latency/command_packet.h"
#include "platform/input/input_command.h"

namespace Vkm::Engine {

/**
 * @brief The commands one player has sent that the server has not yet run.
 *
 * One command per tick, in the player's order, each exactly once: the client
 * predicted tick N with command N, so the server must run it as one whole tick.
 * Queued, not addressed by tick: the two ends' tick clocks are unrelated. A
 * duplicate is run once, or an edge fires twice.
 *
 * Empty, the tick still runs on the last command's axes with its edges
 * cleared. Past MAX_DEPTH the oldest are skipped in one tick, since a backlog
 * is latency. NetPacing keeps the queue just above empty, from lowWater.
 */
class NetCommandBuffer {
    public:
        static constexpr size_t MAX_DEPTH = 10;  ///< Commands the queue may hold before the oldest are
                                                 ///< skipped, all at once; a queue is input delay. Skipped
                                                 ///< edges are folded in, and the same number bounds how far
                                                 ///< repeats may stand in, so the two cannot drift apart.
                                                 ///< NetPacing sets the standing depth; this is for a burst
                                                 ///< after a stall, above the pacing cushion by its jitter
                                                 ///< again, so a jittering link is never trimmed.

        static constexpr size_t MAX_QUEUED = 256;  ///< Most commands held at once, however fast they arrive.
                                                   ///< Nothing else bounds what arrives between two take()s,
                                                   ///< and accept() is linear in it. Many packets' worth.

        static constexpr uint32_t REPEAT_TICKS = NET_COMMAND_REDUNDANCY * 2;
            ///< Ticks a repeat may stand in for before it stops moving the body: twice the redundancy window,
            ///< as that long with nothing means every copy of every recent command was lost, a link gone
            ///< rather than jittering.

    public:
        NetCommandBuffer() = default;
        ~NetCommandBuffer() = default;

        NetCommandBuffer(const NetCommandBuffer& other) = delete;
        NetCommandBuffer& operator=(const NetCommandBuffer& other) = delete;

        NetCommandBuffer(NetCommandBuffer && other) = default;
        NetCommandBuffer& operator=(NetCommandBuffer && other) = default;

    public:
        /// Take one command, ignoring a sequence already seen.
        void accept(const InputCommand& command);

        /**
         * @brief The command to run on the tick about to happen.
         *
         * A repeat claims the client tick it stood in for, so a snapshot's
         * confirmation matches its pose; the real command for that tick is
         * discarded when it turns up.
         *
         * @param tick The server's own tick, stamped on what comes back.
         * @return The oldest command not yet run, or the last one run with its
         *         edges cleared when none is waiting; zeroed before any arrive.
         */
        InputCommand take(uint32_t tick);

        /**
         * @brief The newest command tick actually run, in the client's numbering.
         *
         * Not the server's clock. A snapshot carries it so the client knows
         * which prediction to hold the snapshot against.
         *
         * @return The client tick; zero before any command has run.
         */
        uint32_t newestRunTick() const { return m_ranCommandTick; }

        /**
         * @brief The newest sequence this buffer has accepted.
         *
         * What arrived, ahead of newestRunTick() by the queue's depth. A
         * snapshot carries it so the client stops resending (see
         * firstCommandToSend).
         *
         * @return The sequence; zero until one arrives.
         */
        uint32_t newestSequence() const { return m_newestSequence; }

        /**
         * @brief How many commands are waiting.
         *
         * @return The queue's depth now.
         */
        size_t pending() const { return m_commands.size(); }

        /**
         * @brief The fewest commands waiting at any take() since restartLowWater().
         *
         * Counted before the take, so zero is a tick that ran on a repeat. The
         * lowest point, because the depth now depends on whether a packet just
         * landed.
         *
         * @return The low water; with no take since the restart, the depth now.
         */
        size_t lowWater() const {
            return m_lowWater == std::numeric_limits<size_t>::max() ? m_commands.size() : m_lowWater;
        }

        /// Start measuring lowWater() again, once it has been said.
        void restartLowWater() { m_lowWater = std::numeric_limits<size_t>::max(); }

        /**
         * @brief Ticks that ran on a repeat because nothing was waiting, since clear().
         *
         * Counted from the first command that ran.
         *
         * @return The count.
         */
        uint32_t repeatedTicks() const { return m_repeatedTicks; }

        /**
         * @brief Commands passed over unrun, since clear(): skipped down to
         *        MAX_DEPTH, or pushed out of a queue at MAX_QUEUED.
         *
         * @return The count.
         */
        uint32_t skippedCommands() const { return m_skippedCommands; }

        void clear();

    private:
        std::vector<InputCommand> m_commands;              ///< Ascending by sequence; oldest first.
        InputCommand              m_last;                  ///< Repeated when nothing is waiting.
        uint32_t                  m_newestSequence   = 0;  ///< Newest ever accepted.
        uint32_t                  m_newestTick       = 0;  ///< Its tick; how far a repeat may run ahead.
        uint32_t                  m_consumedSequence = 0;  ///< Newest already run.
        uint32_t                  m_ranCommandTick   = 0;  ///< Newest tick lived, in the sender's clock.
        uint32_t                  m_carriedPressed   = 0;  ///< Edges from moments skipped or stood in for.
        uint32_t                  m_carriedReleased  = 0;  ///< The same, for releases.
        uint32_t                  m_starved          = 0;  ///< Consecutive ticks run on a repeat.
        uint32_t                  m_repeatedTicks    = 0;  ///< Ticks run on a repeat, since clear().
        uint32_t                  m_skippedCommands  = 0;  ///< Commands never run, since clear().

        size_t                    m_lowWater = std::numeric_limits<size_t>::max();
            ///< Fewest waiting at a take since the last restart; the largest size_t for none yet.

        bool                      m_ranReal = false;  ///< Whether m_ranCommandTick means anything yet.
        bool                      m_started = false;
};

} // namespace Vkm::Engine
