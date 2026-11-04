#include "net/latency/command_packet.h"

#include <algorithm>

#include "vkm_network/connection.h"
#include "vkm_network/quantize.h"
#include "vkm_network/reliable_messages.h"

#include "net/wire/protocol.h"
#include "net/wire/quantize.h"

namespace Vkm::Engine {

namespace {

constexpr uint32_t COUNT_BITS = 5;  ///< Bits of a packet's command count.

constexpr uint32_t RUN_START_BITS = 32 + 32;  ///< Bits of a run's start: its first command's sequence and
                                              ///< tick.

constexpr uint32_t PAYLOAD_SIZE_BITS = 5;  ///< Bits of a command's payload length.
static_assert(MAX_COMMAND_PAYLOAD < (1u << PAYLOAD_SIZE_BITS), "the payload's length has to fit its field");
static_assert(NET_MAX_PACKET_COMMANDS < (
    1u << COUNT_BITS
), "the count field has to be able to say how many commands are in the packet");
static_assert(
    NET_COMMAND_REDUNDANCY <= NET_MAX_PACKET_COMMANDS,
    "a packet has to hold the whole window a command is resent across"
);

constexpr uint32_t WIDEST_COMMAND_BITS = MAX_INPUT_ACTIONS * (1 + NET_AXIS_BITS) + 1 + 2 * MAX_INPUT_ACTIONS
                                       + 1 + 2 + 3 * Quantize::ROTATION_BITS
                                       + 1 + PAYLOAD_SIZE_BITS + 8 * MAX_COMMAND_PAYLOAD;
    ///< The widest command: every action moved, an edge, a new view and a new full payload.
// After the commands a packet says whether messages follow, in what room is left.
static_assert(
    NET_MESSAGE_HEADER_BITS + NET_COMMAND_PREAMBLE_BITS + COUNT_BITS + RUN_START_BITS
            + NET_MAX_PACKET_COMMANDS * WIDEST_COMMAND_BITS + Network::Outbox::NONE_BITS
        <= 8 * Network::Connection::MAX_PAYLOAD,
    "a packet of the widest commands, and the flag after them, has to fit a datagram"
);

} // namespace

size_t firstCommandToSend(const std::vector<InputCommand>& unconfirmed, uint32_t heard, uint32_t sent) {
    const size_t size = unconfirmed.size();

    size_t unheard = 0;
    while (unheard < size && unconfirmed[unheard].sequence <= heard) ++unheard;
    size_t unsent = unheard;
    while (unsent < size && unconfirmed[unsent].sequence <= sent) ++unsent;

    const size_t window = size > NET_COMMAND_REDUNDANCY ? size - NET_COMMAND_REDUNDANCY : 0;
    return std::max(unheard, std::min(unsent, window));
}

size_t writeCommands(
    Network::BitWriter& out,
    const std::vector<InputCommand>& commands,
    size_t first,
    uint32_t actionCount
) {
    if (actionCount > MAX_INPUT_ACTIONS) return 0;

    // The reader numbers each command one on from the last, so a gap would
    // give every command after it the wrong tick.
    size_t count = first < commands.size() ? 1 : 0;
    while (count < NET_MAX_PACKET_COMMANDS && first + count < commands.size()) {
        const InputCommand& previous = commands[first + count - 1];
        const InputCommand& next     = commands[first + count];
        if (next.sequence != previous.sequence + 1 || next.tick != previous.tick + 1) break;
        ++count;
    }

    out.bits(static_cast<uint32_t>(count), COUNT_BITS);
    if (count == 0) return 0;

    out.u32(commands[first].sequence);
    out.u32(commands[first].tick);

    // Each command says in a bit whether its view or its payload is the one before it - never, for
    // the first: a player holds both far longer than a tick, and a command rides several packets.
    const InputCommand* previous = nullptr;
    for (size_t i = first; i < first + count; ++i) {
        const InputCommand& command = commands[i];

        // An axis at rest is the common case, so each carries a presence bit
        // rather than the code for zero.
        for (uint32_t action = 0; action < actionCount; ++action) {
            const uint32_t code  = Network::toSigned(command.axis[action], 1.0f, NET_AXIS_BITS);
            const bool     moved = Network::fromSigned(code, 1.0f, NET_AXIS_BITS) != 0.0f;
            out.boolean(moved);
            if (moved) out.bits(code, NET_AXIS_BITS);
        }

        // An edge is rarer still: one bit says whether either mask follows.
        const bool edges = (command.pressed | command.released) != 0;
        out.boolean(edges);
        if (edges) {
            out.bits(command.pressed, actionCount);
            out.bits(command.released, actionCount);
        }

        const bool sameView = previous && previous->view == command.view;
        out.boolean(sameView);
        if (!sameView) Quantize::writeRotation(out, command.view);

        const uint32_t size    = std::min<uint32_t>(command.payloadSize, MAX_COMMAND_PAYLOAD);
        const bool samePayload = previous && previous->payloadSize == size
            && std::equal(command.payload.begin(), command.payload.begin() + size, previous->payload.begin());
        out.boolean(samePayload);
        if (!samePayload) {
            out.bits(size, PAYLOAD_SIZE_BITS);
            for (uint32_t b = 0; b < size; ++b) out.u8(command.payload[b]);
        }
        previous = &command;
    }
    return count;
}

bool readCommands(Network::BitReader& in, uint32_t actionCount, std::vector<InputCommand>& out) {
    out.clear();

    // The count arrives from the network; bound it before it indexes anything.
    if (actionCount > MAX_INPUT_ACTIONS) return false;

    const uint32_t count = in.bits(COUNT_BITS);
    if (in.failed()) return false;
    if (count == 0) return true;
    if (count > NET_MAX_PACKET_COMMANDS) return false;

    const uint32_t sequence = in.u32();
    const uint32_t tick     = in.u32();
    if (in.failed()) return false;

    for (uint32_t i = 0; i < count; ++i) {
        InputCommand command;
        command.sequence = sequence + i;
        command.tick     = tick + i;

        for (uint32_t action = 0; action < actionCount; ++action) {
            command.axis[action] = in.boolean()
                ? Network::fromSigned(in.bits(NET_AXIS_BITS), 1.0f, NET_AXIS_BITS)
                : 0.0f;
        }
        if (in.boolean()) {
            command.pressed  = in.bits(actionCount);
            command.released = in.bits(actionCount);
        }

        // "As the one before" names nothing on the first command.
        const bool sameView = in.boolean();
        if (sameView && out.empty()) return false;
        command.view = sameView ? out.back().view : Quantize::readRotation(in);

        const bool samePayload = in.boolean();
        if (samePayload && out.empty()) return false;
        if (samePayload) {
            command.payload     = out.back().payload;
            command.payloadSize = out.back().payloadSize;
        } else {
            // The length arrives from the network; bound it before it indexes anything.
            const uint32_t size = in.bits(PAYLOAD_SIZE_BITS);
            if (in.failed() || size > MAX_COMMAND_PAYLOAD) return false;
            command.payloadSize = static_cast<uint8_t>(size);
            for (uint32_t b = 0; b < size; ++b) command.payload[b] = in.u8();
        }

        if (in.failed()) return false;
        out.push_back(command);
    }
    return true;
}

} // namespace Vkm::Engine
