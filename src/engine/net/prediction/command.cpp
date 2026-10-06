#include "net/prediction/command.h"

#include <algorithm>

#include "net/wire/quantize.h"

namespace Vkm::Engine {

namespace {

/// Bits of a packet's command count.
constexpr uint32_t COUNT_BITS = 5;
static_assert(
    NET_MAX_PACKET_COMMANDS < (1u << COUNT_BITS),
    "the count field has to be able to say how many commands are in the packet"
);
static_assert(
    NET_COMMAND_REDUNDANCY <= NET_MAX_PACKET_COMMANDS,
    "a packet has to hold the whole window a command is resent across"
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
    BitWriter& out,
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

    for (size_t i = first; i < first + count; ++i) {
        const InputCommand& command = commands[i];

        // An axis at rest is the common case, so each carries a presence bit
        // rather than the code for zero.
        for (uint32_t action = 0; action < actionCount; ++action) {
            const uint32_t code  = Quantize::toSigned(command.axis[action], 1.0f, NET_AXIS_BITS);
            const bool     moved = Quantize::fromSigned(code, 1.0f, NET_AXIS_BITS) != 0.0f;
            out.boolean(moved);
            if (moved) out.bits(code, NET_AXIS_BITS);
        }

        out.bits(command.pressed, actionCount);
        out.bits(command.released, actionCount);
        Quantize::writeRotation(out, command.view);
    }
    return count;
}

bool readCommands(BitReader& in, uint32_t actionCount, std::vector<InputCommand>& out) {
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
                ? Quantize::fromSigned(in.bits(NET_AXIS_BITS), 1.0f, NET_AXIS_BITS)
                : 0.0f;
        }
        command.pressed  = in.bits(actionCount);
        command.released = in.bits(actionCount);
        command.view     = Quantize::readRotation(in);

        if (in.failed()) return false;
        out.push_back(command);
    }
    return true;
}

void NetCommandBuffer::accept(const InputCommand& command) {
    if (m_started && command.sequence <= m_consumedSequence) return;

    // A moment already lived, run or stood in for. Its edges carry forward:
    // the repeat that covered it had none.
    if (m_ranReal && command.tick <= m_ranCommandTick) {
        m_carriedPressed  |= command.pressed;
        m_carriedReleased |= command.released;
        m_consumedSequence = std::max(m_consumedSequence, command.sequence);
        m_newestSequence   = std::max(m_newestSequence, command.sequence);
        return;
    }

    // Full: the oldest goes, its moment most certainly passed.
    if (m_commands.size() >= MAX_QUEUED) {
        ++m_skippedCommands;
        m_consumedSequence = m_commands.front().sequence;
        m_carriedPressed  |= m_commands.front().pressed;
        m_carriedReleased |= m_commands.front().released;
        m_commands.erase(m_commands.begin());
    }

    const auto found = std::find_if(
        m_commands.begin(),
        m_commands.end(),
        [&command](const InputCommand& held) { return held.sequence == command.sequence; }
    );
    if (found != m_commands.end()) return;

    const auto at = std::lower_bound(
        m_commands.begin(),
        m_commands.end(),
        command.sequence,
        [](const InputCommand& held, uint32_t sequence) { return held.sequence < sequence; }
    );
    m_commands.insert(at, command);
    m_newestSequence = std::max(m_newestSequence, command.sequence);
    m_newestTick     = std::max(m_newestTick, command.tick);
}

InputCommand NetCommandBuffer::take(uint32_t tick) {
    m_started  = true;
    m_lowWater = std::min(m_lowWater, m_commands.size());

    if (m_commands.empty()) {
        InputCommand repeated = m_last;
        repeated.tick     = tick;
        repeated.pressed  = 0;
        repeated.released = 0;

        ++m_starved;
        if (m_ranReal) ++m_repeatedTicks;
        if (m_starved > REPEAT_TICKS) {
            for (float& axis : repeated.axis) axis = 0.0f;
        }

        // Only as far dry as the buffer may run deep.
        if (m_ranReal && m_ranCommandTick < m_newestTick + MAX_DEPTH) {
            ++m_ranCommandTick;
        }
        return repeated;
    }

    // Skipping an axis is free; skipped edges are folded into the one that runs.
    while (m_commands.size() > MAX_DEPTH) {
        ++m_skippedCommands;
        m_carriedPressed  |= m_commands.front().pressed;
        m_carriedReleased |= m_commands.front().released;
        m_consumedSequence = m_commands.front().sequence;
        m_commands.erase(m_commands.begin());
    }

    InputCommand next = m_commands.front();
    next.pressed  |= m_carriedPressed;
    next.released |= m_carriedReleased;
    m_carriedPressed  = 0;
    m_carriedReleased = 0;
    m_commands.erase(m_commands.begin());

    m_starved          = 0;
    m_last             = next;
    m_consumedSequence = next.sequence;
    m_ranCommandTick   = std::max(m_ranCommandTick, next.tick);
    m_ranReal          = true;

    next.tick = tick;
    return next;
}

void NetCommandBuffer::clear() {
    m_commands.clear();
    m_last             = InputCommand{};
    m_newestSequence   = 0;
    m_newestTick       = 0;
    m_consumedSequence = 0;
    m_ranCommandTick   = 0;
    m_carriedPressed   = 0;
    m_carriedReleased  = 0;
    m_ranReal          = false;
    m_starved          = 0;
    m_repeatedTicks    = 0;
    m_skippedCommands  = 0;
    m_started          = false;
    restartLowWater();
}

} // namespace Vkm::Engine
