#pragma once

#include <glm/glm.hpp>

#include "resource/asset/material_asset.h"

namespace Vkm::Engine {

/**
 * @brief Flattened projected decal for the frame: the material lands on surfaces inside the box.
 */
struct DecalData {
    glm::mat4      model;      ///< World transform of a unit cube centred on the entity.
    glm::mat4      invModel;   ///< World -> decal local, for the box test + UVs.
    MaterialHandle material;
    float          angleFade;  ///< Fade width as the surface turns away from the projector.
    float          opacity;
};

} // namespace Vkm::Engine
