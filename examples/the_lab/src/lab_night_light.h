#pragma once

#include "ecs/entity.h"
#include "system/script/reflected_behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief A lamp that lights at dusk: its Light, and the mesh of its child named `Glow`, are on
 *        while the sun is lower than `below` degrees.
 */
class LabNightLight : public ReflectedBehavior<LabNightLight> {
    public:
        void onStart() override;
        void onUpdate(float dt) override;

    public:
        float below = 8.0f;  ///< The sun's elevation under which it is lit, degrees.

    private:
        EntityId m_glow{};
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabNightLight)
    VKM_F(below)
VKM_REFLECT_END()
