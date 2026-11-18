#pragma once

#include <string>

#include <glm/glm.hpp>

#include "system/script/behavior.h"

namespace Lab {

using namespace Vkm::Engine;

/**
 * @brief One area of the lab, as the map and the HUD know it: a name over a rectangle of ground.
 *
 * Data alone; LabHud draws the map from these and says which one the player stands in. The
 * rectangle is centered on the entity, `size` across X and Z.
 */
class LabArea : public Behavior<LabArea> {
    public:
        std::string title;
        std::string blurb;
        glm::vec2   size{20.0f, 20.0f};
        glm::vec4   color{0.3f, 0.5f, 0.8f, 0.9f};
};

} // namespace Lab

VKM_BEHAVIOR(::Lab::LabArea)
    VKM_F(title)
    VKM_F(blurb, "One line on what there is to do.")
    VKM_F(size, "Meters across X and Z.")
    VKM_F(color, "Its patch on the map.")
VKM_REFLECT_END()
