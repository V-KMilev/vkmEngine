#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "vkm_network/bit_stream.h"

#include "platform/input/input_command.h"

namespace Vkm::Engine {

constexpr uint32_t NET_COMMAND_REDUNDANCY = 12;  ///< How many ticks a command goes on being resent for. An
                                                 ///< axis recovers from a lost packet, but an edge
                                                 ///< (`pressed`) is true for one command only and would be
                                                 ///< lost for good; so a command rides every packet until the
                                                 ///< server has it or this many newer exist. A retransmit
                                                 ///< would arrive after its tick had passed.

constexpr uint32_t NET_MAX_PACKET_COMMANDS = 16;  ///< The most commands one packet carries: more than the
                                                  ///< redundancy window, as one frame can run more ticks and
                                                  ///< every one must go out. Sixteen of the widest commands
                                                  ///< still fit a datagram, as command_packet.cpp asserts.

constexpr uint32_t NET_COMMAND_MEMORY = 64;  ///< Commands a client holds while waiting to hear they were run:
                                             ///< a backstop, as the server's acknowledgements empty the list.
                                             ///< Also bounds what a replay can correct: past this many ticks
                                             ///< of round trip the oldest needed command is gone and the
                                             ///< prediction goes uncorrected.

constexpr uint32_t NET_DRAWN_FRACTION_BITS = 8;  ///< Bits of a command packet's share of a tick past the
                                                 ///< server tick it draws (CommandPreamble), in parts of
                                                 ///< NET_DRAWN_FRACTION_MAX: when the client saw what it
                                                 ///< aimed at, for the server's rewind.

constexpr uint32_t NET_DRAWN_FRACTION_MAX = (1u << NET_DRAWN_FRACTION_BITS) - 1;
    ///< The largest fraction NET_DRAWN_FRACTION_BITS say, and the parts a tick is said in.

constexpr uint32_t NET_COMMAND_PREAMBLE_BITS = 8 + 32 + NET_DRAWN_FRACTION_BITS;
    ///< What a command packet opens with (CommandPreamble): its world generation, then the drawn tick
    ///< whole and its fraction.

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
    Network::BitWriter& out,
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
bool readCommands(Network::BitReader& in, uint32_t actionCount, std::vector<InputCommand>& out);

} // namespace Vkm::Engine
