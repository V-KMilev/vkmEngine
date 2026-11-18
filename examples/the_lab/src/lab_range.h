#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/entity.h"
#include "system/script/behavior.h"

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
class LabRange : public Behavior<LabRange> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;
        void onFixedUpdate(float dt) override;

    public:
        PrefabRef blast;
        float     radius       = 7.0f;
        float     push         = 14.0f;
        float     lift         = 0.6f;
        float     resetSeconds = 6.0f;

    private:
        /**
         * @brief A charge, and where it stands when the range is set.
         */
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
        CountWatch          m_presses;        ///< The switch's presses, to blow for each new one
        bool                m_throw = false;  ///< Charges blew this frame; the next tick throws
        bool                m_raise = false;  ///< The charges were reset; the next frame stands them
};

} // namespace Lab

VKM_BEHAVIOR(::Lab::LabRange)
    VKM_F(blast, "Prefab spawned where each charge was.")
    VKM_F(radius, "Meters the blast throws bodies within.")
    VKM_F(push, "Meters a second given at the charge, none at the radius.")
    VKM_F(lift, "Of the push, the share turned upward.")
    VKM_F(resetSeconds, "Until the charges stand again.")
VKM_REFLECT_END()
