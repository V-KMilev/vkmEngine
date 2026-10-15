#pragma once

#include <glm/glm.hpp>

#include "system/script/reflected_behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief The wind on a loose body: a banner's cloth, a flag, a sign on its chains.
 *
 * Each tick it pushes its own dynamic body along `direction`, by a gust that swells and dies by
 * the world's clock. Only where this end simulates the body; a banner is NetLocal, so every end
 * blows its own alike.
 */
class LabWind : public ReflectedBehavior<LabWind> {
    public:
        void onFixedUpdate(float dt) override;

    public:
        glm::vec3 direction{1.0f, 0.0f, 0.0f};  ///< Which way it blows; its length is the gust's push, m/s^2.
        float     gustSeconds = 4.0f;           ///< Seconds from one gust to the next.
        float     flutter     = 0.5f;           ///< How much quicker shakes ride on the gust, 0 to 1.
        float     phase       = 0.0f;           ///< Share of a gust it is ahead of its neighbours.
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabWind)
    VKM_F(direction)
    VKM_F(gustSeconds)
    VKM_F(flutter)
    VKM_F(phase)
VKM_REFLECT_END()
