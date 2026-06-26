#pragma once

#include <cstdint>

namespace Engine {

/**
 * @brief Engine-level configuration constants.
 *
 * Backend-agnostic limits and defaults consumed by ECS systems and read by
 * any rendering backend. Backend-specific knobs (atlas resolutions, texture
 * slot assignments, UBO binding points) live in their backend's own config
 * (e.g. src/backend/opengl/convention/gl_bindings.h).
 *
 * Anything that has to stay in sync with a shader carries a comment naming
 * the shader file and the matching identifier.
 */
namespace Config {

    // The constants below are the single source of truth for both C++ and GLSL:
    // cmake/generate_shader_config.cmake mirrors them (under the same names) into
    // shaders/_generated/engine_config.glsl, which the forward shaders pull in
    // via a #include of "_generated/engine_config.glsl". vkmGL's GraphicsShaderSource
    // resolves those includes (preprocessShaderSource in
    // modules/vkmGL/src/shader/gl_shader.cpp). Do not re-define these in a
    // shader - include the generated file instead.

    // Maximum number of lights uploaded per frame.
    constexpr uint32_t MAX_LIGHTS = 32;

    // Shadow caster budget for the 2D atlas (directional + spot).
    constexpr uint32_t MAX_SHADOW_CASTERS_2D = 6;

    // Shadow caster budget for the cube atlas (point lights).
    constexpr uint32_t MAX_SHADOW_CASTERS_CUBE = 2;

    // Cascade count for the directional (sun) shadow. The first directional
    // shadow caster reserves this many consecutive 2D atlas layers; remaining
    // layers (MAX_SHADOW_CASTERS_2D - NUM_CASCADES) serve spot lights.
    constexpr uint32_t NUM_CASCADES = 4;

    // Near plane used when rasterising and sampling point-light cube shadows.
    // Pinned to a small but non-zero value so depth values keep resolution at
    // typical occluder distances without losing fragments inside very small
    // lights. Consumed on the CPU (gl_shadow_data.cpp builds the cube
    // projection); not mirrored to GLSL, since no shader reads it.
    constexpr float SHADOW_CUBE_NEAR = 0.1f;

    // Fixed simulation step (60 Hz). The cadence at which fixedUpdate runs.
    constexpr float FIXED_TIME_STEP = 1.0f / 60.0f;

    // Cap on the simulation-time accumulator. Prevents a frame hitch from
    // queuing enough fixedUpdate ticks to outpace the next frame ("spiral
    // of death"). 0.25s ~= 15 ticks max per render frame at FIXED_TIME_STEP.
    constexpr float MAX_FRAME_ACCUMULATOR = 0.25f;

    // Per-system tunables (cull distance, camera sensitivity, etc.) live as
    // nested Settings structs on the owning system - NOT here. This config
    // is reserved for cross-cutting compile-time limits and engine-loop
    // constants only.

} // namespace Config

} // namespace Engine
