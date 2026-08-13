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

    // Maximum number of lights uploaded per frame. The list lives in an SSBO and
    // is culled into clusters, so the forward pass only ever shades a cluster's
    // handful - the cap can be generous.
    constexpr uint32_t MAX_LIGHTS = 256;

    // Forward+ clustered lighting: the view frustum is diced into
    // CLUSTER_X x CLUSTER_Y screen tiles by CLUSTER_Z exponential depth slices,
    // and a compute pass culls the lights into each cluster's list (capped at
    // MAX_LIGHTS_PER_CLUSTER). The forward pass then shades only its cluster.
    //
    // 32x18 puts a tile at roughly 60px on a 1080p-class viewport. Coarser tiles
    // make each pixel iterate lights that only clip a far corner of its tile: at
    // 16x9 (120px tiles) that measured 0.35 ms of extra forward shading in a
    // 220-light scene. Finer than this stops paying - 48x27 measured identical
    // to 32x18 while costing 2.3x the grid memory - because the cull is already
    // tight enough that the remaining per-pixel lights genuinely overlap it.
    // The cull pass itself is insensitive to the split (0.10 -> 0.11 ms).
    constexpr uint32_t CLUSTER_X = 32;
    constexpr uint32_t CLUSTER_Y = 18;
    constexpr uint32_t CLUSTER_Z = 24;
    constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 64;

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
