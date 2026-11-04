#include "net/latency/command_buffer.h"

#include <algorithm>

namespace Vkm::Engine {

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

    const auto at = std::lower_bound(
        m_commands.begin(),
        m_commands.end(),
        command.sequence,
        [](const InputCommand& held, uint32_t sequence) { return held.sequence < sequence; }
    );
    if (at != m_commands.end() && at->sequence == command.sequence) return;
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
    *this = NetCommandBuffer{};
}

} // namespace Vkm::Engine
