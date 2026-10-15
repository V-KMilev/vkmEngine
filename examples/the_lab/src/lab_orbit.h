#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "system/script/reflected_behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief Carries its entity round a circle by the world's clock: a gull, a lantern on the breeze,
 *        a wisp over the marsh.
 *
 * The circle is centred where the entity was authored, and it rises and falls as it goes. Moved by
 * NetSession::worldTime, so every end puts it in the same place; it belongs at or under a NetLocal.
 * It faces the way it flies, banked into the turn.
 */
class LabOrbit : public ReflectedBehavior<LabOrbit> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

    public:
        float radius     = 6.0f;   ///< Metres from the centre.
        float period     = 12.0f;  ///< Seconds a lap; negative flies the other way round.
        float phase      = 0.0f;   ///< Share of a lap it starts ahead.
        float bob        = 0.3f;   ///< Metres it rises and falls.
        float bobSeconds = 3.0f;   ///< Seconds it takes to rise and fall once.
        float bank       = 20.0f;  ///< Degrees it leans into the turn.

    private:
        glm::vec3 m_centre{0.0f};
        glm::quat m_facing{1.0f, 0.0f, 0.0f, 0.0f};  ///< Its authored turn, kept as it flies
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabOrbit)
    VKM_F(radius)
    VKM_F(period)
    VKM_F(phase)
    VKM_F(bob)
    VKM_F(bobSeconds)
    VKM_F(bank)
VKM_REFLECT_END()
