#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace Engine {

/**
 * @brief Flattened reflection probe for the frame.
 *
 * Snapshotted from the scene's ReflectionProbe components so the backend never
 * searches the scene. The backend bakes a GPU cube set per probe, then blends
 * the nearest one over the global IBL inside its influence box.
 */
struct ProbeData {
    glm::vec3 position;     ///< World-space probe centre (from the entity's Transform).
    glm::vec3 halfExtents;  ///< Influence box half-size, for parallax correction + falloff.
    float     falloff;      ///< Fraction of the half-extent over which influence fades to the global IBL.
    float     intensity;    ///< Linear-HDR multiplier on the probe's contribution.
    uint32_t  bakeVersion;  ///< Re-bake trigger (snapshot of the component's bakeVersion).
};

} // namespace Engine
