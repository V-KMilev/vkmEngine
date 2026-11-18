#pragma once

#include "ecs/entity.h"
#include "system/script/behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief A lamp that lights at dusk: its Light, and the mesh of its child named `Glow`, are on
 *        while the sun is lower than `below` degrees.
 */
class LabNightLight : public Behavior<LabNightLight> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

    public:
        float below = 8.0f;

    private:
        EntityId m_glow{};
};

} // namespace Lab

VKM_BEHAVIOR(::Lab::LabNightLight)
    VKM_F(below, "The sun's elevation under which it is lit, degrees.")
VKM_REFLECT_END()
