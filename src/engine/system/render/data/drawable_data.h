#pragma once

#include <glm/glm.hpp>

#include "resource/asset/mesh_asset.h"
#include "resource/asset/material_asset.h"

namespace Engine {

/**
 * @brief One visible object to draw.
 *
 * Pure handles plus a world matrix. The backend resolves the handles against
 * its own GPU mirror and decides how to sort / batch them - that is an
 * API-specific concern, so it lives below the interface, not here.
 */
struct DrawableData {
    MeshHandle     mesh;      ///< The mesh to render.
    MaterialHandle material;  ///< The material to render.
    glm::mat4      model;     ///< The model matrix to render the mesh with.

    bool castShadows;          ///< Whether the drawable casts shadows.
};

} // namespace Engine
