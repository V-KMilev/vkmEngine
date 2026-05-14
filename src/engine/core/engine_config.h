#pragma once

#include <cstdint>

namespace Engine {

/**
 * @brief Engine-level configuration constants.
 *
 * Backend-agnostic limits and defaults consumed by ECS systems and read by
 * any rendering backend. Backend-specific knobs (atlas resolutions, texture
 * slot assignments, UBO binding points) live in their backend's own config
 * (e.g. src/backend/opengl/config/gl_config.h).
 *
 * Anything that has to stay in sync with a shader carries a comment naming
 * the shader file and the matching identifier.
 */
namespace Config {

    // ---- Rendering budgets ------------------------------------------------

    /// Maximum number of lights uploaded per frame.
    /// MUST match MAX_LIGHTS in shaders/pbr/fragmentShader.shader.
    constexpr uint32_t MaxLights = 32;

    /// Shadow caster budget for the 2D atlas (directional + spot).
    /// MUST match SHADOW_MAX_CASTERS_2D in shaders/pbr/fragmentShader.shader.
    constexpr uint32_t MaxShadowCasters2D = 6;

    /// Shadow caster budget for the cube atlas (point lights).
    /// MUST match SHADOW_MAX_CASTERS_CUBE in shaders/pbr/fragmentShader.shader.
    constexpr uint32_t MaxShadowCastersCube = 2;

    // ---- Simulation timing ------------------------------------------------

    /// Fixed simulation step (60 Hz). Cadence at which fixedUpdate runs.
    constexpr float FixedTimeStep = 1.0f / 60.0f;

    /// Cap on the simulation-time accumulator. Prevents a frame hitch from
    /// queuing enough fixedUpdate ticks to outpace the next frame ("spiral
    /// of death"). 0.25s ≈ 15 ticks max per render frame at FixedTimeStep.
    constexpr float MaxFrameAccumulator = 0.25f;

    // Per-system tunables (cull distance, camera sensitivity, etc.) live as
    // nested Settings structs on the owning system — NOT here. This config
    // is reserved for cross-cutting compile-time limits and engine-loop
    // constants only.

} // namespace Config

} // namespace Engine
