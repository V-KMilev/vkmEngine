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

    // Fixed simulation step (60 Hz). The cadence at which fixedUpdate runs.
    constexpr float FIXED_TIME_STEP = 1.0f / 60.0f;

    // Cap on the simulation-time accumulator. Prevents a frame hitch from
    // queuing enough fixedUpdate ticks to outpace the next frame ("spiral
    // of death"). 0.25s ~= 15 ticks max per render frame at FIXED_TIME_STEP.
    constexpr float MAX_FRAME_ACCUMULATOR = 0.25f;

} // namespace Config

} // namespace Vkm::Engine
