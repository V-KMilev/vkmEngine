#pragma once

#include <glm/glm.hpp>

#include "ecs/entity.h"
#include "system/script/reflected_behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief Floats a dynamic body on the scene's Ocean: it bobs, rolls with the waves, and drifts.
 *
 * Each tick it asks the sea (OceanSurface::sampleAt) for the water's height and slope under the
 * body. It is lifted by how much of a box of `halfExtents` lies under the water, and turned so the
 * face nearest up lies along the swell. Only where this end simulates the body; the sea is the same
 * at every end.
 */
class LabFloat : public ReflectedBehavior<LabFloat> {
    public:
        void onStart() override;
        void onFixedUpdate(float dt) override;

    public:
        glm::vec3 halfExtents{0.4f, 0.4f, 0.4f};  ///< The floating box, metres.
        float     buoyancy  = 2.2f;  ///< Lift when wholly under, in multiples of gravity.
        float     waterDrag = 1.6f;  ///< Velocity lost a second when wholly under.
        float     righting  = 6.0f;  ///< How fast it turns to lie along the swell, per second.

    private:
        EntityId m_ocean{};
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabFloat)
    VKM_F(halfExtents)
    VKM_F(buoyancy)
    VKM_F(waterDrag)
    VKM_F(righting)
VKM_REFLECT_END()
