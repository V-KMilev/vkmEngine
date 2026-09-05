#pragma once

#include <glm/glm.hpp>

#include "core/reflect.h"

namespace Vkm::Engine {

/**
 * @brief The physics world's own parameters, read once per fixed step.
 *
 * Scene-global the way the Environment is, and deliberately not part of it:
 * what a world is lit by and what it falls at are unrelated, and a file called
 * environment.h that also held gravity was the kind of near-miss that makes a
 * reader look in the wrong place twice. Owned by Scene, beside the Environment.
 */
struct PhysicsSettings {
    glm::vec3 gravity          = {0.0f, -9.81f, 0.0f};  ///< World gravity (m/s^2).
    int       solverIterations = 8;                     ///< PGS solver passes per fixed step.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::PhysicsSettings)
    VKM_F(gravity),
    VKM_F(solverIterations)
VKM_REFLECT_END()
