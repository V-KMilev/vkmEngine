#pragma once

#include <string>

namespace Engine {

/**
 * @brief The scene's lighting environment: the equirectangular HDR baked into
 *        the IBL product set and drawn as the skybox, plus brightness + visibility.
 *
 * Scene-global state, NOT an entity/component - one per Scene, always present
 * (owned by Scene::environment()), and it round-trips with the scene. The
 * backend re-bakes the IBL whenever hdrPath changes; the skybox samples that
 * baked product, so the visible background follows the swap automatically.
 */
struct Environment {
    std::string hdrPath    = "assets/envs/environment.hdr";  ///< Equirect HDR baked into IBL + skybox.
    float       intensity  = 1.0f;                           ///< Indirect-lighting + skybox brightness multiplier.
    bool        showSkybox = true;                           ///< Draw the skybox background; the IBL still lights the scene when off.
};

} // namespace Engine
