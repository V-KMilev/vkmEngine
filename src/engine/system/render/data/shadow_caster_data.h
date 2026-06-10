#pragma once

#include <glm/glm.hpp>

#include "resource/asset/mesh_asset.h"

namespace Engine {

/**
 * @brief One shadow-casting object: geometry, world placement, and world bounds.
 *
 * Collected scene-wide (not camera-culled) so off-screen occluders still cast
 * into the visible scene. The world AABB lets the shadow pass frustum-cull
 * casters per light / cascade before drawing their depth.
 */
struct ShadowCasterData {
    MeshHandle mesh;       ///< The mesh to cast shadows.
    glm::mat4  model;      ///< The model matrix to cast shadows.
    glm::vec3  aabbMin;    ///< The world-space AABB minimum of the shadow caster.
    glm::vec3  aabbMax;    ///< The world-space AABB of the shadow caster.
};

} // namespace Engine
