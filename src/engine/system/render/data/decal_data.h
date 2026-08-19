#pragma once

#include <glm/glm.hpp>

#include "resource/asset/material_asset.h"

namespace Vkm::Engine {

/**
 * @brief Flattened projected decal for the frame.
 *
 * Snapshotted from the scene's Decal components so the backend never searches the
 * scene. The backend draws the decal's box, reconstructs the surface under it from
 * depth, and blends the projected material where the surface falls inside the box.
 */
struct DecalData {
    glm::mat4      model;      ///< Decal box world transform (a unit cube centred on the entity).
    glm::mat4      invModel;   ///< World -> decal local space, for the inside-the-box test + UVs.
    MaterialHandle material;   ///< Projected material.
    float          angleFade;  ///< Fade width where the surface normal turns away from the projector.
    float          opacity;    ///< Overall blend strength.
};

} // namespace Vkm::Engine
