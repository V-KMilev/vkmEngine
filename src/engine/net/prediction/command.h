#pragma once

#include <cstdint>
#include <vector>

#include "net/wire/bit_stream.h"
#include "platform/input/input_command.h"

namespace Vkm::Engine {

/**
 * @brief How many past commands ride in every command packet.
 *
 * A command packet carries the oldest unacknowledged commands, not just the
 * one for this tick, and this is not a bandwidth compromise - it is the only
 * thing that makes an edge survive the network.
 *
 * An axis recovers on its own: a lost packet costs one tick of movement and
 * the next packet says where the stick is now. An edge does not. `pressed` is
 * true for exactly one command, so a single lost datagram loses that jump
 * permanently - the player pressed the key, the character did not jump, and
 * nothing anywhere reports an error. Carrying twelve means a command is sent
 * again in every packet until it is confirmed or the list outruns it, so an
 * edge survives every packet lost inside the 94 ms those twelve ticks cover at
 * 128 Hz - six consecutive packets at the default send rate, which produces two
 * new commands per packet.
 *
 * Stated as a window rather than as a packet count because that is the part
 * that does not move: raising the send rate buys more packets inside the same
 * window, not more protection.
 *
 * Retransmission would be the other answer and it is the wrong one: by the time
 * a retransmit arrived the tick it belonged to would be long past.
 */
constexpr uint32_t NET_COMMAND_REDUNDANCY = 12;

/**
 * @brief Commands a client holds while waiting to hear they were run.
 *
 * Larger than what one packet carries, and for a different reason. A packet
 * carries the newest few because that is what protects an edge; this bounds how
 * long the unconfirmed list may grow while nothing comes back. On a hundred
 * millisecond link at 128 Hz there are about thirteen commands in flight at any
 * moment - already more than one packet holds - so a bound set to a packet's
 * worth would be discarding commands as a matter of routine rather than as a
 * backstop.
 *
 * It is only a backstop. What normally empties this list is the server saying
 * which commands it ran; this is what stops the list growing without limit when
 * it never does.
 *
 * It also bounds how bad a link reconciliation can still correct. A replay
 * re-runs the ticks after the one the server confirmed, so it needs every
 * command since - and once the round trip exceeds this many ticks the oldest is
 * gone and the prediction is kept uncorrected instead. Sixty-four is half a
 * second at 128 Hz, well past what a game is playable on, but it is the number
 * to raise if that is ever wrong.
 */
constexpr uint32_t NET_COMMAND_MEMORY = 64;

/**
 * @brief Bits an axis is carried in.
 *
 * A hundredth of full deflection, which is finer than a stick reports and far finer
 * than a key, which is at an end anyway.
 */
constexpr uint32_t NET_AXIS_BITS = 8;

/**
 * @brief Write @p commands, oldest first, for @p actionCount defined actions.
 *
 * Both ends agree on the action count the same way they agree on the schema:
 * it comes from the project, and a disagreement is refused at join rather than
 * decoded into a player pressing something else.
 *
 * At most NET_COMMAND_REDUNDANCY of them go, and they are the OLDEST that fit.
 * A frame runs as many ticks as the time since the last one calls for - up to a
 * quarter of a second of them, thirty-two at 128 Hz - and one packet goes out
 * per frame. Taking the newest would leave the oldest behind, and every later
 * frame has newer ones still, so those would never be sent at all: the server
 * would never run ticks the client had already predicted, and the two would
 * disagree from there with nothing to close it. In steady state the choice
 * decides nothing, because everything unacknowledged fits.
 *
 * @param out         Stream to write into.
 * @param commands    Oldest first; at most NET_COMMAND_REDUNDANCY are written.
 * @param actionCount How many action slots are in use.
 */
void writeCommands(BitWriter& out,
                   const std::vector<InputCommand>& commands,
                   uint32_t actionCount);

/**
 * @brief Read back what writeCommands wrote.
 *
 * @param in          Stream to read from.
 * @param actionCount How many action slots are in use.
 * @param out         Filled with the commands, oldest first.
 * @return False when the packet is malformed.
 */
bool readCommands(BitReader& in, uint32_t actionCount, std::vector<InputCommand>& out);

/**
 * @brief The commands one player has sent that the server has not yet run.
 *
 * One command is run per tick, in the order the player made them, and never
 * more than once. That ordering is the whole contract: the client predicted
 * tick N by running command N, so the server must run command N as one whole
 * tick too, or the two ends compute different answers from the same input and
 * the client is corrected forever.
 *
 * They are **not** addressed by tick number. The two ends count ticks on
 * separate clocks - each started when its own process did - so a command
 * labelled with the client's tick 400 means nothing against the server's tick
 * 400. Matching them up needs the client to run far enough ahead that its
 * commands land just before the server wants them, and a control loop to keep
 * it there; run in order from a queue instead, the same property falls out with
 * no clock to synchronise and nothing to drift. What the client's tick number
 * is still good for is being handed back in a snapshot, so the client knows
 * which of its own predicted moments the server has now judged.
 *
 * The same command arrives many times, because every packet repeats the last
 * twelve. It is run once: an edge consumed twice is a double jump from one
 * keypress.
 *
 * When the queue is empty the tick still runs - the server cannot wait for a
 * packet. The previous command's axes are repeated, because a held key is still
 * held, and its edges are cleared, because a press already fired once and
 * repeating it fires the action again for every tick of the gap.
 *
 * When the queue grows past what a link's jitter needs, it is drained slightly
 * faster than it fills. A backlog is latency: every command behind it waits, so
 * a client that ran fast for a moment would otherwise add that moment to its
 * own input delay for the rest of the match.
 */
class NetCommandBuffer {
    public:
        NetCommandBuffer() = default;
        ~NetCommandBuffer() = default;

        NetCommandBuffer(const NetCommandBuffer& other) = delete;
        NetCommandBuffer& operator=(const NetCommandBuffer& other) = delete;

        NetCommandBuffer(NetCommandBuffer && other) = delete;
        NetCommandBuffer& operator=(NetCommandBuffer && other) = delete;

        /**
         * @brief Commands worth holding before running them.
         *
         * A cushion against jitter: with none, one late packet is a tick run on
         * repeated input, and a jump lost. Each one held is a tick of input
         * delay, so this is small deliberately - two at 128 Hz is 16 ms.
         */
        static constexpr size_t TARGET_DEPTH = 2;

        /**
         * @brief How deep the queue may get before it is drained faster than it fills.
         *
         * Three times the cushion: a band wide enough that ordinary jitter never
         * touches it, and narrow enough that a client which ran fast for a
         * moment does not keep that moment as input delay for the rest of the
         * match. The same number bounds how far a run of repeats may stand in
         * for commands that have not come, so the two cannot drift apart.
         */
        static constexpr size_t MAX_DEPTH = TARGET_DEPTH * 3;

        /**
         * @brief Most commands held at once, however fast they arrive.
         *
         * The backlog is drained by take(), which runs once a tick; nothing
         * bounds what arrives between two ticks. A peer that sends faster than
         * it is consumed would otherwise grow this without end, and both
         * searches in accept() are linear in it. Generous against any real
         * sender - twelve are repeated in every packet, and this is many
         * packets' worth.
         */
        static constexpr size_t MAX_QUEUED = 256;

        /**
         * @brief Ticks a repeat may stand in for before it stops moving the body.
         *
         * A held key really is still held across a gap of a few ticks, which is
         * why a starved tick repeats the last axes at all. Across a long one it
         * is a guess, and the guess is that the player is still pushing forward -
         * so a two-second outage runs their character off whatever it was
         * walking towards, and they come back somewhere they never chose.
         *
         * Sized by the redundancy window: every command is sent
         * NET_COMMAND_REDUNDANCY times, so nothing arriving for longer than that
         * window means every copy of every recent command was lost. That is a
         * link that has gone, not one that is jittering, and the honest answer
         * to what the player is holding is that nobody knows.
         */
        static constexpr uint32_t REPEAT_TICKS = NET_COMMAND_REDUNDANCY * 2;

    public:
        /// Take one command, ignoring a sequence already seen.
        void accept(const InputCommand& command);

        /**
         * @brief The command to run on the tick about to happen.
         *
         * @param tick The server's own tick, stamped on what comes back so a
         *             caller reads a command belonging to the tick it is
         *             running.
         * @return The oldest command not yet run, or the last one run with its
         *         edges cleared when none is waiting. A zeroed command before
         *         any arrive, which reads as nothing held.
         *
         * A repeat claims the client tick it stood in for, so what a snapshot
         * confirms always describes the pose the snapshot carries. The command
         * bearing that tick is discarded when it does turn up, or the same
         * moment would be lived twice.
         */
        InputCommand take(uint32_t tick);

        /**
         * @brief The newest command tick actually run, in the client's numbering.
         *
         * Not the server's own tick, which is a different clock: each end
         * started counting when its process did, and the only thing they share
         * is that a command carries the tick the client built it for. A
         * snapshot carries this so the client knows which of its own predicted
         * moments to hold the snapshot against - against the server's tick it
         * would be comparing two unrelated numbers, and would find no match at
         * all or, worse, the wrong one.
         */
        uint32_t newestRunTick() const { return m_ranCommandTick; }

        /**
         * @brief The newest sequence this buffer has accepted.
         *
         * What arrived, which is not what a snapshot reports: that is
         * newestRunTick(), naming the tick the server actually ran. A command
         * can be held here for ticks before it is run, so the two differ by the
         * depth of the queue.
         */
        uint32_t newestSequence() const { return m_newestSequence; }

        /**
         * @brief How many commands are waiting.
         *
         * The server's own measure of whether a client is sending fast enough: empty
         * and ticks run on repeated input, deep and the player is behind for no reason.
         */
        size_t pending() const { return m_commands.size(); }

        void clear();

    private:
        std::vector<InputCommand> m_commands;   ///< Ascending by sequence; oldest first.
        InputCommand              m_last;       ///< Repeated when nothing is waiting.
        uint32_t                  m_newestSequence   = 0;  ///< Newest ever accepted.
        uint32_t                  m_newestTick       = 0;  ///< Its tick; how far a repeat may run ahead.
        uint32_t                  m_consumedSequence = 0;  ///< Newest already run.
        uint32_t                  m_ranCommandTick   = 0;  ///< Newest tick lived, in the sender's clock.
        uint32_t                  m_carriedPressed   = 0;  ///< Edges from moments skipped or stood in for.
        uint32_t                  m_carriedReleased  = 0;  ///< The same, for releases.
        uint32_t                  m_starved          = 0;  ///< Consecutive ticks run on a repeat.

        bool                      m_ranReal = false;  ///< Whether m_ranCommandTick means anything yet.
        bool                      m_started = false;
};

} // namespace Vkm::Engine
