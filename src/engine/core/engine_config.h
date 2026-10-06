#pragma once

#include <cstdint>

namespace Vkm::Engine {

/**
 * @brief Engine-level configuration constants.
 *
 * Backend-agnostic limits and loop constants. A per-system tunable belongs in that
 * system's Settings struct; a backend knob in the backend's config (e.g.
 * src/backend/opengl/convention/gl_bindings.h).
 *
 * Most are written into every shader's prelude (GLBackend::shaderConstants);
 * never re-declare one in a shader. See docs/reference/lighting.md.
 */
namespace Config {

    // Lights uploaded per frame.
    constexpr uint32_t MAX_LIGHTS = 256;

    // Forward+ grid: screen tiles by exponential depth slices. Read the trade-off
    // in docs/reference/lighting.md before changing it.
    constexpr uint32_t CLUSTER_X = 32;
    constexpr uint32_t CLUSTER_Y = 18;
    constexpr uint32_t CLUSTER_Z = 24;
    constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 64;

    // Casters in the 2D atlas (directional + spot).
    constexpr uint32_t MAX_SHADOW_CASTERS_2D = 6;

    // Point-light casters, one depth cube each.
    constexpr uint32_t MAX_SHADOW_CASTERS_CUBE = 2;

    // Sun shadow cascades, each taking one of the MAX_SHADOW_CASTERS_2D atlas tiles; see GLShadowData.
    constexpr uint32_t NUM_CASCADES = 4;

    // Spot/point shadow near plane: small enough not to clip inside a small light,
    // large enough to keep depth resolution at typical occluder distances.
    constexpr float SHADOW_NEAR = 0.1f;

    // fixedUpdate ticks per second, and the wire's clock, when a project names none.
    constexpr uint32_t DEFAULT_TICK_RATE = 64;

    // Below four, a step is longer than MAX_FRAME_ACCUMULATOR can consume and
    // fixedUpdate silently never runs.
    constexpr uint32_t MIN_TICK_RATE = 4;
    constexpr uint32_t MAX_TICK_RATE = 480;

    /**
     * @brief What a project's requested tick rate is honoured as.
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

    // Caps the accumulator so a hitch cannot queue ticks faster than frames run
    // ("spiral of death"). A higher rate gets more ticks per hitch, not a longer stall.
    constexpr float MAX_FRAME_ACCUMULATOR = 0.25f;

    static_assert(
        1.0f / static_cast<float>(MIN_TICK_RATE) <= MAX_FRAME_ACCUMULATOR,
        "A step longer than the accumulator cap can never be consumed, "
        "so the slowest allowed tick rate would never tick at all"
    );

    // Seat cap: a host's per-tick work scales with it, and it comes from a
    // hand-authored project.json.
    constexpr uint32_t MAX_PLAYERS_PER_GAME = 64;

    /**
     * @brief What a project's requested seat count is honoured as.
     *
     * Zero is kept: the session then refuses to host.
     *
     * @param seats The count as asked for.
     * @return It, clamped to [0, MAX_PLAYERS_PER_GAME].
     */
    constexpr uint32_t clampSeats(uint32_t seats) {
        return seats > MAX_PLAYERS_PER_GAME ? MAX_PLAYERS_PER_GAME : seats;
    }

} // namespace Config

} // namespace Vkm::Engine
