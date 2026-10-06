#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "net/wire/bit_stream.h"
#include "platform/input/input_command.h"

namespace Vkm::Engine {

/**
 * @brief How many ticks a command goes on being resent for.
 *
 * An axis recovers from a lost packet; an edge (`pressed`) is true for one
 * command only and would be lost for good. So a command rides every packet
 * until the server has it or this many newer exist. A retransmit would arrive
 * after its tick had passed.
 */
constexpr uint32_t NET_COMMAND_REDUNDANCY = 12;

/**
 * @brief The most commands one packet carries.
 *
 * More than the redundancy window, since one frame can run more ticks and
 * every one must go out. Sixteen of the widest commands still fit a datagram.
 */
constexpr uint32_t NET_MAX_PACKET_COMMANDS = 16;

/**
 * @brief Commands a client holds while waiting to hear they were run.
 *
 * A backstop: the server's acknowledgements normally empty the list. Also
 * bounds what a replay can correct: past this many ticks of round trip the
 * oldest needed command is gone and the prediction goes uncorrected.
 */
constexpr uint32_t NET_COMMAND_MEMORY = 64;

/**
 * @brief Bits an axis is carried in.
 *
 * 1/127 of full deflection either side of an exact centre (Quantize::toSigned),
 * finer than a stick reports.
 */
constexpr uint32_t NET_AXIS_BITS = 8;

/**
 * @brief Where the next command packet starts in @p unconfirmed.
 *
 * Every command of the newest NET_COMMAND_REDUNDANCY the server has not
 * heard, and before those any command no packet has carried yet, however old:
 * one is never skipped for age, or the server never runs it.
 *
 * The window moves with the newest command, not with replies: anchored on a
 * reply, a long round trip would starve the server of input. @p heard only
 * stops resending what has arrived.
 *
 * @param unconfirmed Every command the server has not yet run, oldest first.
 * @param heard       Newest sequence the server has said it received; zero
 *                    for none, which no command carries.
 * @param sent        Newest sequence a packet has carried; zero for none.
 * @return Index of the first command to write.
 */
size_t firstCommandToSend(const std::vector<InputCommand>& unconfirmed, uint32_t heard, uint32_t sent);

/**
 * @brief Write @p commands from @p first, oldest first, for @p actionCount
 *        defined actions.
 *
 * The action count comes from the project; a disagreement is refused at join.
 * Only an unbroken run goes (each one sequence and one tick after the last),
 * so only the first's numbers are written. It stops at the first gap or
 * NET_MAX_PACKET_COMMANDS; the rest goes in the next packet.
 *
 * @param out         Stream to write into.
 * @param commands    Oldest first.
 * @param first       Index of the first to write.
 * @param actionCount Action slots in use.
 * @return How many were written.
 */
size_t writeCommands(
    BitWriter& out,
    const std::vector<InputCommand>& commands,
    size_t first,
    uint32_t actionCount
);

/**
 * @brief Read back what writeCommands wrote.
 *
 * @param in          Stream to read from.
 * @param actionCount Action slots in use.
 * @param out         Filled with the commands, oldest first.
 * @return False when the packet is malformed.
 */
bool readCommands(BitReader& in, uint32_t actionCount, std::vector<InputCommand>& out);

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
        /**
         * @brief Commands the queue may hold before the oldest are skipped, all at once.
         *
         * A queue is input delay. Skipped edges are folded in; the same number
         * bounds how far repeats may stand in, so the two cannot drift apart.
         * NetPacing sets the standing depth; this is for a burst after a
         * stall, set above the pacing cushion by its jitter again so a
         * jittering link is never trimmed.
         */
        static constexpr size_t MAX_DEPTH = 10;

        /**
         * @brief Most commands held at once, however fast they arrive.
         *
         * Nothing else bounds what arrives between two take()s, and accept() is
         * linear in it. Many packets' worth.
         */
        static constexpr size_t MAX_QUEUED = 256;

        /**
         * @brief Ticks a repeat may stand in for before it stops moving the body.
         *
         * Twice the redundancy window: that long with nothing means every copy
         * of every recent command was lost, a link gone rather than jittering.
         */
        static constexpr uint32_t REPEAT_TICKS = NET_COMMAND_REDUNDANCY * 2;

    public:
        NetCommandBuffer() = default;
        ~NetCommandBuffer() = default;

        NetCommandBuffer(const NetCommandBuffer& other) = delete;
        NetCommandBuffer& operator=(const NetCommandBuffer& other) = delete;

        NetCommandBuffer(NetCommandBuffer && other) = delete;
        NetCommandBuffer& operator=(NetCommandBuffer && other) = delete;

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

        /// Fewest waiting at a take since the last restart; the largest size_t for none yet.
        size_t                    m_lowWater = std::numeric_limits<size_t>::max();

        bool                      m_ranReal = false;  ///< Whether m_ranCommandTick means anything yet.
        bool                      m_started = false;
};

} // namespace Vkm::Engine
