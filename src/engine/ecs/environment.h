#pragma once

#include <string>

#include <glm/glm.hpp>

namespace Engine {

/**
 * @brief The scene's global settings: the lighting environment (equirectangular
 *        HDR baked into the IBL product set and drawn as the skybox, plus
 *        brightness + visibility) together with the physics-world parameters.
 *
 * Scene-global state, NOT an entity/component - one per Scene, always present
 * (owned by Scene::environment()), and it round-trips with the scene. The
 * backend re-bakes the IBL whenever hdrPath changes; the skybox samples that
 * baked product, so the visible background follows the swap automatically. The
 * physics fields are read once per fixed step by PhysicsSystem.
 */
struct Environment {
    // Lighting (skybox + image-based lighting)
    std::string hdrPath    = "assets/envs/environment.hdr";  ///< Equirect HDR baked into IBL + skybox.
    float       intensity  = 1.0f;                           ///< Indirect-lighting + skybox brightness multiplier.
    bool        showSkybox = true;                           ///< Draw the skybox background; the IBL still lights the scene when off.

    // Physics world
    glm::vec3 gravity          = {0.0f, -9.81f, 0.0f};       ///< World gravity (m/s^2).
    int       solverIterations = 8;                          ///< PGS solver passes per fixed step.
};

} // namespace Engine
