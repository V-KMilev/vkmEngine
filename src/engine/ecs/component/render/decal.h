#pragma once

#include "core/reflect.h"

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

    /**
     * @brief Whether this projector is in the frame at all.
     *
     * The same flag Light and ParticleEmitter carry, for the same reason: a
     * decal turned off is not one drawn at zero opacity - it is one the pass
     * never sees, so it costs nothing. Without it the only way to take a
     * projector out was to remove the component and add it back, which loses
     * everything authored on it.
     */
    bool enabled = true;
};

} // namespace Vkm::Engine

VKM_REFLECT_BEGIN(::Vkm::Engine::Decal)
    VKM_F(material),
    VKM_F(angleFade),
    VKM_F(opacity),
    VKM_F(enabled)
VKM_REFLECT_END()
