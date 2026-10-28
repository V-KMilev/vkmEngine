#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "system/script/reflected_behavior.h"

#include "lab_state.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief A demolition range: each press of the LabSwitch beside it blows the charges under it.
 *
 * A charge is a child whose name starts with "Charge". At a press every charge standing goes up:
 * `blast` (a prefab of effects, its flash and bang too) where it stood. Every dynamic body within
 * `radius` is thrown out from it, less the farther it is: by the authority, and the charges, which
 * are each end's own (NetLocal), by every end. The authority knocks down a player caught in it; the
 * charges stand again after `resetSeconds`, where they were. Every end acts on the press count
 * alone, so the show is the same everywhere.
 */
class LabRange : public ReflectedBehavior<LabRange> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;
        void onFixedUpdate(float dt) override;

    public:
        PrefabRef blast;                ///< Prefab spawned where each charge was.
        float     radius       = 7.0f;  ///< Metres the blast throws bodies within.
        float     push         = 14.0f; ///< Metres a second given at the charge, none at the radius.
        float     lift         = 0.6f;  ///< Of the push, the share turned upward.
        float     resetSeconds = 6.0f;  ///< Until the charges stand again.

    private:
        /// A charge, and where it stands when the range is set.
        struct Charge {
            EntityId  entity;
            glm::vec3 position;
            glm::quat rotation;
        };

    private:
        /// Blow every charge standing, where it stands; true when one did.
        bool detonate();

        /// Throw what this end may throw around every charge; the authority knocks players down.
        void throwFromCharges();

        /// Put the charges this end simulates back where they stood, still.
        void reset();

    private:
        std::vector<Charge> m_charges;
        CountWatch m_presses;           ///< The switch's presses, to blow for each new one
        float      m_resetIn = 0.0f;    ///< Seconds until the charges stand again, 0 while they do
        bool       m_throw   = false;   ///< Charges blew this frame; the next tick throws
        bool       m_raise   = false;   ///< The charges were reset; the next frame stands them
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabRange)
    VKM_F(blast)
    VKM_F(radius)
    VKM_F(push)
    VKM_F(lift)
    VKM_F(resetSeconds)
VKM_REFLECT_END()
