#pragma once

#include <glm/glm.hpp>

namespace Engine {

/**
 * @brief Per-scene physics-world settings (singleton component).
 *
 * Holds parameters that belong to the whole simulation rather than any one
 * body: the gravity vector and the solver's iteration count. PhysicsSystem
 * reads these each tick and falls back to these same defaults when no
 * PhysicsWorld exists. Lives on a singleton entity and round-trips with the
 * scene, so gravity persists and can differ between scenes.
 */
struct PhysicsWorld {
    glm::vec3 gravity          = {0.0f, -9.81f, 0.0f};  ///< World gravity (m/s^2)
    int       solverIterations = 8;                     ///< PGS solver passes per tick
};

} // namespace Engine
