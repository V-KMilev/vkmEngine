#pragma once

#include <string>

#include <glm/glm.hpp>

#include "system/script/reflected_behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief One area of the lab, as the map and the HUD know it: a name over a rectangle of ground.
 *
 * Data alone; LabHud draws the map from these and says which one the player stands in. The
 * rectangle is centred on the entity, `size` across X and Z.
 */
class LabArea : public ReflectedBehavior<LabArea> {
    public:
        std::string title;
        std::string blurb;                          ///< One line on what there is to do.
        glm::vec2   size{20.0f, 20.0f};             ///< Metres across X and Z.
        glm::vec4   color{0.3f, 0.5f, 0.8f, 0.9f};  ///< Its patch on the map.
};

} // namespace Lab

VKM_REFLECT_BEGIN(::Lab::LabArea)
    VKM_F(title)
    VKM_F(blurb)
    VKM_F(size)
    VKM_F(color)
VKM_REFLECT_END()
