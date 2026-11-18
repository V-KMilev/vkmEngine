#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "system/script/behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief Carries its entity round a circle by the world's clock: a gull, a lantern on the breeze,
 *        a wisp over the marsh.
 *
 * The circle is centered where the entity was authored, and it rises and falls as it goes. Moved by
 * NetSession::worldTime, so every end puts it in the same place; it belongs at or under a NetLocal.
 * It faces the way it flies, banked into the turn.
 */
class LabOrbit : public Behavior<LabOrbit> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

    public:
        float radius     = 6.0f;
        float period     = 12.0f;
        float phase      = 0.0f;
        float bob        = 0.3f;
        float bobSeconds = 3.0f;
        float bank       = 20.0f;

    private:
        glm::vec3 m_center{0.0f};
        glm::quat m_facing{1.0f, 0.0f, 0.0f, 0.0f};  ///< Its authored turn, kept as it flies
};

} // namespace Lab

VKM_BEHAVIOR(::Lab::LabOrbit)
    VKM_F(radius, "Meters from the center.")
    VKM_F(period, "Seconds a lap; negative flies the other way round.")
    VKM_F(phase, "Share of a lap it starts ahead.")
    VKM_F(bob, "Meters it rises and falls.")
    VKM_F(bobSeconds, "Seconds it takes to rise and fall once.")
    VKM_F(bank, "Degrees it leans into the turn.")
VKM_REFLECT_END()
