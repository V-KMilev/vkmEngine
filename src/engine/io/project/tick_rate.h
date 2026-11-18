#pragma once

#include <cstdint>

namespace Vkm::Engine {

// The range of project.json's `tickRate`: the rate fixedUpdate runs at and the wire's clock.
namespace Config {

    // fixedUpdate ticks per second, and the wire's clock, when a project names none.
    constexpr uint32_t DEFAULT_TICK_RATE = 64;

    // Below four, a step is longer than MAX_FRAME_ACCUMULATOR can consume and
    // fixedUpdate silently never runs.
    constexpr uint32_t MIN_TICK_RATE = 4;
    constexpr uint32_t MAX_TICK_RATE = 480;

    /**
     * @brief What a project's requested tick rate is honored as.
     *
     * Anything pacing off the rate must use this, or a headless host spins at a
     * rate the simulation is not running at.
     *
     * @param ticksPerSecond The rate as asked for.
     * @return It, clamped to [MIN_TICK_RATE, MAX_TICK_RATE].
     */
    constexpr uint32_t clampTickRate(uint32_t ticksPerSecond) {
        return ticksPerSecond < MIN_TICK_RATE ? MIN_TICK_RATE
            : ticksPerSecond > MAX_TICK_RATE ? MAX_TICK_RATE
            : ticksPerSecond;
    }

} // namespace Config

} // namespace Vkm::Engine
