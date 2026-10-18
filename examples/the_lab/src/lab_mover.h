#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "system/script/reflected_behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief Moves a kinematic body to and fro along `travel`, and turns it, by the world's clock.
 *
 * Driven by NetSession::worldTime on each tick, so every end that moves it - each one for a NetLocal
 * platform, the server for a body it replicates - puts it in the same place. It writes the body's
 * velocities too, which is how a character standing on it is carried.
 */
class LabMover : public ReflectedBehavior<LabMover> {
    public:
        void onStart() override;
        void onFixedUpdate(float dt) override;

    public:
        glm::vec3 travel{0.0f, 0.0f, 4.0f};  ///< From the authored place to the far end, metres.
        float     period = 6.0f;             ///< Seconds there and back; 0 holds it still.
        float     phase  = 0.0f;             ///< Share of a period it starts ahead.
        float     spin   = 0.0f;             ///< Degrees a second it turns about up.

    private:
        glm::vec3 m_origin{0.0f};
        glm::quat m_facing{1.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabMover)
    VKM_F(travel)
    VKM_F(period)
    VKM_F(phase)
    VKM_F(spin)
VKM_REFLECT_END()
