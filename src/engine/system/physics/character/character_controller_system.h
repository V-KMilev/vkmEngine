#pragma once

#include "core/system.h"
#include "debug/fault_latch.h"

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
        FaultLatch m_noCapsule;  ///< A controller with no enabled capsule Collider.
        FaultLatch m_spinnable;  ///< A controller's Rigidbody with freezeRotation off.
};

} // namespace Vkm::Engine
