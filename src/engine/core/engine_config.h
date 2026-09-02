#pragma once

#include <cstdint>

namespace Vkm::Engine {

/**
 * @brief Engine-level configuration constants.
 *
 * Cross-cutting compile-time limits and engine-loop constants, backend-agnostic
 * and read by any rendering backend. Two kinds of constant deliberately do not
 * live here: a per-system tunable (cull distance, camera sensitivity) belongs on
 * its own system as a nested Settings struct, and a backend knob (atlas
 * resolution, texture slot, UBO binding point) belongs in that backend's config
 * (e.g. src/backend/opengl/convention/gl_bindings.h).
 *
 * Most of these are mirrored into GLSL at configure time, so C++ and the shaders
 * share one source of truth; never re-declare one in a shader. The mirror, the
 * per-constant table of what reads each value, and the reasoning behind the
 * cluster grid are in docs/reference/system/lighting.md.
 */
namespace Config {

    // Maximum number of lights uploaded per frame. The list lives in an SSBO and
    // is culled into clusters, so the forward pass only ever shades a cluster's
    // handful - the cap can be generous.
    constexpr uint32_t MAX_LIGHTS = 256;

    // Forward+ cluster grid: screen tiles by exponential depth slices. The split
    // is measured, not arbitrary - docs/reference/system/lighting.md says against
    // what, before you change it.
    constexpr uint32_t CLUSTER_X = 32;
    constexpr uint32_t CLUSTER_Y = 18;
    constexpr uint32_t CLUSTER_Z = 24;
    constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 64;

    // Shadow caster budget for the 2D atlas (directional + spot).
    constexpr uint32_t MAX_SHADOW_CASTERS_2D = 6;

    // Shadow caster budget for the point lights' individual depth cubes.
    constexpr uint32_t MAX_SHADOW_CASTERS_CUBE = 2;

    // Cascade count for the directional (sun) shadow. The first directional
    // shadow caster reserves this many consecutive 2D atlas tiles; remaining
    // tiles (MAX_SHADOW_CASTERS_2D - NUM_CASCADES) serve spot lights.
    constexpr uint32_t NUM_CASCADES = 4;

    // Near plane used when rasterising and sampling point-light cube shadows.
    // Small but non-zero, so depth keeps its resolution at typical occluder
    // distances without clipping fragments inside a very small light.
    constexpr float SHADOW_CUBE_NEAR = 0.1f;

    // Simulation ticks per second when a project does not name its own. The
    // cadence fixedUpdate runs at, and for a networked game the rate the wire
    // is clocked by.
    constexpr uint32_t DEFAULT_TICK_RATE = 64;

    // Bounds on that, so a hand-edited project.json cannot ask for a step of
    // zero (an infinite tick loop) or one so slow the simulation is unusable.
    // Four, not one: the accumulator is capped at MAX_FRAME_ACCUMULATOR below,
    // and a step longer than that cap can never be consumed - so a rate of 1, 2
    // or 3 is a simulation whose fixedUpdate silently never runs. The
    // static_assert after the cap keeps the two from drifting apart.
    constexpr uint32_t MIN_TICK_RATE = 4;
    constexpr uint32_t MAX_TICK_RATE = 480;

    /**
     * @brief What a project's requested tick rate is honoured as.
     *
     * A project.json is hand-authored, so the number in it is whatever was
     * typed. Anything that paces itself off that rate has to agree with the
     * clock about what it actually became, or a headless host spins at a rate
     * the simulation is not running at.
     *
     * @param ticksPerSecond The rate as asked for.
     * @return It, brought inside [MIN_TICK_RATE, MAX_TICK_RATE].
     */
    constexpr uint32_t clampTickRate(uint32_t ticksPerSecond) {
        return ticksPerSecond < MIN_TICK_RATE ? MIN_TICK_RATE
             : ticksPerSecond > MAX_TICK_RATE ? MAX_TICK_RATE
                                              : ticksPerSecond;
    }

    // Cap on the simulation-time accumulator. Prevents a frame hitch from
    // queuing enough fixedUpdate ticks to outpace the next frame ("spiral
    // of death"). A quarter second is 16 ticks at the default rate; a project
    // that raises its rate gets more ticks per hitch, not a longer stall.
    constexpr float MAX_FRAME_ACCUMULATOR = 0.25f;

    static_assert(1.0f / static_cast<float>(MIN_TICK_RATE) <= MAX_FRAME_ACCUMULATOR,
                  "A step longer than the accumulator cap can never be consumed, "
                  "so the slowest allowed tick rate would never tick at all");

} // namespace Config

} // namespace Vkm::Engine
