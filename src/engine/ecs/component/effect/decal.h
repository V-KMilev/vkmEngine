#pragma once

#include "base/reflect/reflect.h"

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
    float          angleFade = 0.5f;  ///< How wide the fade is as the surface turns from the projector; 0 is
                                      ///< a hard cut.
    float          opacity   = 1.0f;

    bool enabled = true;  ///< Whether this projector is in the frame at all; off costs nothing.
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Decal)
    VKM_F(material, "Its albedo, with alpha, is what lands on the surface")
    VKM_F(angleFade, 0.0f, 1.0f, "Fades it where the surface turns away from the projector")
    VKM_F(opacity, 0.0f, 1.0f, "How much of the material lands")
    VKM_F(enabled, "Off takes it out of the frame entirely")
VKM_REFLECT_END()
