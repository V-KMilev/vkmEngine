#include "net/prediction/command.h"

#include <algorithm>
#include <cmath>

#include "net/wire/quantize.h"

namespace Vkm::Engine {

namespace {

/**
 * @brief Bits for how far a command's sequence and tick are from the one before it.
 *
 * One, almost always; the width covers a client whose frame ran long enough to skip a
 * few ticks without falling back to a full field.
 */
constexpr uint32_t STEP_BITS = 6;
constexpr uint32_t MAX_STEP  = (1u << STEP_BITS) - 1u;

/**
 * @brief Commands in one packet.
 *
 * Five bits holds thirty-one, which is more than the redundancy asks for.
 */
constexpr uint32_t COUNT_BITS = 5;
static_assert(NET_COMMAND_REDUNDANCY < (1u << COUNT_BITS),
              "the count field has to be able to say how many commands are in the packet");

/**
 * @brief Below this an axis is written as absent rather than as a value.
 *
 * A quarter of one quantisation step: an axis is NET_AXIS_BITS over its full
 * deflection, so anything under 2/255 encodes as zero anyway and the presence
 * bit says the same for one bit instead of nine. Well under that step rather
 * than at it, so a stick resting slightly off centre still reads as resting
 * while one barely pushed still travels.
 */
constexpr float AXIS_AT_REST = 0.002f;

uint32_t axisToBits(float value) {
    const float clamped = std::max(-1.0f, std::min(1.0f, value));
    const float scaled  = (clamped + 1.0f) * 0.5f * static_cast<float>((1u << NET_AXIS_BITS) - 1u);
    return static_cast<uint32_t>(std::lround(scaled));
}

float axisFromBits(uint32_t raw) {
    return (static_cast<float>(raw) / static_cast<float>((1u << NET_AXIS_BITS) - 1u)) * 2.0f - 1.0f;
}

} // namespace

void writeCommands(BitWriter& out,
                   const std::vector<InputCommand>& commands,
                   uint32_t actionCount) {
    if (actionCount > MAX_INPUT_ACTIONS) return;

    // The oldest that fit: after a stutter those are the ones the server wants
    // next, and the newest would strand them forever.
    const size_t count = std::min<size_t>(commands.size(), NET_COMMAND_REDUNDANCY);

    out.bits(static_cast<uint32_t>(count), COUNT_BITS);
    if (count == 0) return;

    out.u32(commands[0].sequence);
    out.u32(commands[0].tick);

    for (size_t i = 0; i < count; ++i) {
        const InputCommand& command = commands[i];

        if (i > 0) {
            // Deltas from the command before, one in every ordinary frame. A
            // gap past the field is clamped rather than widening every command
            // to carry it; the server repeats the last input across a gap.
            const uint32_t sequenceStep = command.sequence - commands[i - 1].sequence;
            const uint32_t tickStep     = command.tick - commands[i - 1].tick;
            out.bits(std::min(sequenceStep, MAX_STEP), STEP_BITS);
            out.bits(std::min(tickStep, MAX_STEP), STEP_BITS);
        }

        // An axis at rest is the common case - a player holds two of six
        // actions - so each carries a presence bit rather than eight bits of
        // zero.
        for (uint32_t action = 0; action < actionCount; ++action) {
            const float value = command.axis[action];
            const bool  moved = std::abs(value) > AXIS_AT_REST;
            out.boolean(moved);
            if (moved) out.bits(axisToBits(value), NET_AXIS_BITS);
        }

        out.bits(command.pressed, actionCount);
        out.bits(command.released, actionCount);
        Quantize::writeRotation(out, command.view);
    }
}

bool readCommands(BitReader& in, uint32_t actionCount, std::vector<InputCommand>& out) {
    out.clear();

    // Bounded before it indexes anything: the count arrives from the network
    // and a command holds a fixed number of slots. Refused here rather than by
    // the caller, because this is the side that does the writing.
    if (actionCount > MAX_INPUT_ACTIONS) return false;

    const uint32_t count = in.bits(COUNT_BITS);
    if (in.failed()) return false;
    if (count == 0) return true;
    if (count > NET_COMMAND_REDUNDANCY) return false;

    uint32_t sequence = in.u32();
    uint32_t tick     = in.u32();
    if (in.failed()) return false;

    for (uint32_t i = 0; i < count; ++i) {
        if (i > 0) {
            sequence += in.bits(STEP_BITS);
            tick     += in.bits(STEP_BITS);
        }

        InputCommand command;
        command.sequence = sequence;
        command.tick     = tick;

        for (uint32_t action = 0; action < actionCount; ++action) {
            command.axis[action] = in.boolean() ? axisFromBits(in.bits(NET_AXIS_BITS)) : 0.0f;
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
    // Every packet repeats the last twelve commands, so most of what arrives
    // here has already been seen. Running one twice is a double jump from a
    // single keypress.
    if (m_started && command.sequence <= m_consumedSequence) return;

    // A moment already lived, run or stood in for. Its edges carry forward
    // rather than being dropped: a press is true for one command only, and the
    // repeat that covered it had none to give.
    if (m_ranReal && command.tick <= m_ranCommandTick) {
        m_carriedPressed  |= command.pressed;
        m_carriedReleased |= command.released;
        m_consumedSequence = std::max(m_consumedSequence, command.sequence);
        m_newestSequence   = std::max(m_newestSequence, command.sequence);
        return;
    }

    // Full - see MAX_QUEUED. The oldest goes, its moment being the one that
    // has most certainly passed.
    if (m_commands.size() >= MAX_QUEUED) {
        m_consumedSequence = m_commands.front().sequence;
        m_carriedPressed  |= m_commands.front().pressed;
        m_carriedReleased |= m_commands.front().released;
        m_commands.erase(m_commands.begin());
    }

    const auto found = std::find_if(m_commands.begin(), m_commands.end(),
                                    [&command](const InputCommand& held) {
                                        return held.sequence == command.sequence;
                                    });
    if (found != m_commands.end()) return;

    // Held in the order the player made them, which is the order they must run
    // in: the client predicted each tick from one of these, in this order.
    const auto at = std::lower_bound(m_commands.begin(), m_commands.end(), command.sequence,
                                     [](const InputCommand& held, uint32_t sequence) {
                                         return held.sequence < sequence;
                                     });
    m_commands.insert(at, command);
    m_newestSequence = std::max(m_newestSequence, command.sequence);
    m_newestTick     = std::max(m_newestTick, command.tick);
}

InputCommand NetCommandBuffer::take(uint32_t tick) {
    m_started = true;

    if (m_commands.empty()) {
        // Nothing waiting, and the tick has to run anyway. Axes repeat because
        // a held key is still held; edges do not, because a press that already
        // fired would fire again on every tick of the gap.
        InputCommand repeated = m_last;
        repeated.tick     = tick;
        repeated.pressed  = 0;
        repeated.released = 0;

        // Past the redundancy window nothing is being guessed any more: every
        // copy of every recent command has been lost, so the body stops rather
        // than being walked somewhere the player did not choose.
        ++m_starved;
        if (m_starved > REPEAT_TICKS) {
            for (float& axis : repeated.axis) axis = 0.0f;
        }

        // The repeat claims the client tick it stood in for, so that what a
        // snapshot confirms describes the pose it carries. Only as far dry as
        // the buffer is allowed to run deep - see networking.md.
        if (m_ranReal && m_ranCommandTick < m_newestTick + MAX_DEPTH) {
            ++m_ranCommandTick;
        }
        return repeated;
    }

    // A backlog is latency, so it is drained faster than it fills. Skipping an
    // axis is free - the one that runs says where the stick is now - but an
    // edge is true for one command only, so skipped edges are folded into it.
    while (m_commands.size() > MAX_DEPTH) {
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

    // Stamped with the tick it is actually running on, so a system reading it
    // sees the tick it is in rather than the one the sender was in.
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
    m_started          = false;
}

} // namespace Vkm::Engine
