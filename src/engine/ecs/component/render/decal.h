#pragma once

#include "core/reflect.h"

#include "resource/asset/material_asset.h"

namespace Vkm::Engine {

/**
 * @brief A projected decal - bullet holes, blood, scorch marks.
 *
 * Projects its material's albedo along the entity's forward onto geometry inside
 * the Transform's unit cube. Surfaces facing away fade out, so a decal never
 * smears across a perpendicular wall.
 */
struct Decal {
    MaterialHandle material;          ///< Its albedo (with alpha) is projected.
    /// Fade width as the surface turns away from the projector (0 = hard cut).
    float          angleFade = 0.5f;
    float          opacity   = 1.0f;

    /**
     * @brief Whether this projector is in the frame at all; off costs nothing.
     */
    bool enabled = true;
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Decal)
    VKM_F(material)
    VKM_F(angleFade)
    VKM_F(opacity)
    VKM_F(enabled)
VKM_REFLECT_END()
