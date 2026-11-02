#pragma once

#include <glm/glm.hpp>

#include "base/reflect/reflect.h"

namespace Vkm::Engine {

/**
 * @brief The physics world's own parameters, read once per fixed step.
 *
 * Scene-global, owned by Scene beside the Environment.
 */
struct PhysicsSettings {
    glm::vec3 gravity          = {0.0f, -9.81f, 0.0f};  ///< World gravity (m/s^2).
    int       solverIterations = 8;                     ///< PGS solver passes per fixed step: more holds
                                                        ///< stacks stiffer. Saved with the scene; the
                                                        ///< step holds it to the range below.

    static constexpr int MIN_SOLVER_ITERATIONS = 1;   ///< None pushes nothing out of what it sinks into.
    static constexpr int MAX_SOLVER_ITERATIONS = 32;  ///< A file's billion would hang the tick.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::PhysicsSettings)
    VKM_F(gravity, -50.0f, 50.0f, "The world's gravity")
    VKM_F(solverIterations, PhysicsSettings::MIN_SOLVER_ITERATIONS, PhysicsSettings::MAX_SOLVER_ITERATIONS, "Solver passes a step: more holds stacks stiffer")
VKM_REFLECT_END()
