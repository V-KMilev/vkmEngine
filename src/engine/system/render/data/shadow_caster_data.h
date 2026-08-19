#pragma once

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
};

} // namespace Vkm::Engine
