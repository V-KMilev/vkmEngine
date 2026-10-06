#pragma once

#include "core/system.h"

namespace Vkm::Engine {

/**
 * @brief Drives every CharacterController's Rigidbody from its moveInput, and resolves what it stands on.
 *
 * Runs on the tick, after PhysicsSystem, so it reads THIS tick's Rigidbody::supported / supportNormal /
 * blockNormal. Writes velocity, never position. A target aimed into a wall is deflected along it here,
 * or Coulomb friction would decide whether a character glides or snags.
 */
class CharacterControllerSystem : public System {
    public:
        CharacterControllerSystem() = default;
        ~CharacterControllerSystem() override = default;

        CharacterControllerSystem(const CharacterControllerSystem& other) = delete;
        CharacterControllerSystem& operator=(const CharacterControllerSystem& other) = delete;

        CharacterControllerSystem(CharacterControllerSystem && other) = delete;
        CharacterControllerSystem& operator=(CharacterControllerSystem && other) = delete;

    public:
        void fixedUpdate(FrameContext& ctx) override;

        /**
         * @brief Re-run during a replay: it moves the world from state and command, deterministically.
         *
         * @return Always true.
         */
        bool isReplayed() const override { return true; }

    private:
        // Edge latches: each fault is named once per gap, not once a tick.
        bool m_noCapsuleLogged = false;
        bool m_spinnableLogged = false;
};

} // namespace Vkm::Engine
