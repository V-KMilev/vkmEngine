#pragma once

#include "resource/asset/material_asset.h"

namespace Vkm::Engine {

/**
 * @brief A projected decal - bullet holes, blood, scorch marks.
 *
 * Projects its material's albedo onto whatever scene geometry falls inside the
 * entity's box (the Transform's position/rotation/scale define a unit cube), along
 * the entity's forward. Surfaces facing away from the projector fade out, so a
 * decal never smears across a perpendicular wall.
 *
 * Pure data - the depth-reconstructed projection and the blend live in the render
 * backend, like Mesh.
 */
struct Decal {
    MaterialHandle material;          ///< Decal material; its albedo (with alpha) is projected.
    float          angleFade = 0.5f;  ///< Fade width where the surface normal turns away from the projector (0 = hard cut).
    float          opacity   = 1.0f;  ///< Overall blend strength.
};

} // namespace Vkm::Engine
