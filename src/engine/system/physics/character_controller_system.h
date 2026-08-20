#pragma once

#include "core/system.h"

namespace Vkm::Engine {

/**
 * @brief Drives every CharacterController's Rigidbody from its moveInput, and
 *        resolves what it is standing on.
 *
 * Registered at SystemStage::Simulation immediately after PhysicsSystem, so it
 * reads THIS tick's freshly written Rigidbody::supported / supportNormal. Only
 * the velocity it writes is a tick late, and a tick of steering lag is
 * imperceptible; fresh grounding is the half that has to be exact, because
 * landing, stepping off a ledge and refusing a slope all turn on it.
 *
 * It writes velocity, never position: the solver owns the pose, so a character
 * cannot be teleported through a wall by its own controller, and everything the
 * solver already does - friction, restitution, penetration recovery, sleeping -
 * keeps working underneath it.
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
        void update(FrameContext& ctx) override {}
        void fixedUpdate(FrameContext& ctx) override;

        bool hasFixedUpdate() const override { return true; }

    private:
        // Edge latches, so each fault is named once per gap rather than once a
        // tick. Both are silent misbehaviours otherwise: a controller with no
        // capsule never grounds, and one that can rotate falls over.
        bool m_noCapsuleLogged = false;
        bool m_spinnableLogged = false;
};

} // namespace Vkm::Engine
