#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

/**
 * @brief One shadow-casting object: geometry, world placement, and world bounds.
 *
 * Collected scene-wide (not camera-culled) so off-screen occluders still cast
 * into the visible scene. The world AABB lets the shadow pass frustum-cull
 * casters per light / cascade before drawing their depth.
 */
struct ShadowCasterData {
    MeshHandle mesh;
    glm::mat4  model;
    glm::vec3  aabbMin;
    glm::vec3  aabbMax;

    /**
     * @brief This caster's bone palette inside RenderView::skinMatrices.
     *
     * Carried here and not only on DrawableData because the two lists are
     * gathered from different sets - the camera-visible entities and the
     * scene-wide casters. A character standing just off-screen and casting into
     * view appears only in this one, and posing it in the camera list alone
     * would leave its shadow frozen in bind pose.
     */
    uint32_t skinFirst = 0;
    uint32_t skinCount = 0;
};

} // namespace Vkm::Engine
