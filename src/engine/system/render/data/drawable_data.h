#pragma once

#include <glm/glm.hpp>

#include "resource/mesh_asset.h"      // MeshHandle
#include "resource/material_asset.h"  // MaterialHandle

namespace Engine {

/**
 * @brief One visible object to draw.
 *
 * Pure handles plus a world matrix. The backend resolves the handles against
 * its own GPU mirror and decides how to sort / batch them - that is an
 * API-specific concern, so it lives below the interface, not here.
 */
struct DrawableData {
    MeshHandle     mesh;
    MaterialHandle material;
    glm::mat4      model;
};

} // namespace Engine
