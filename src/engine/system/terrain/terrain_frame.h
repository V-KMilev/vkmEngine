#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/math/bounds.h"
#include "ecs/entity.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "system/terrain/terrain_chunks.h"

namespace Vkm::Engine {

/**
 * @brief One chunk of a terrain in the world: its levels' meshes and where it lies, for the cull
 *        to list beside the scene's meshes.
 */
struct TerrainChunk {
    EntityId                                          entity;  ///< The terrain's.
    std::array<MeshHandle, TerrainChunks::LEVELS>     levels;  ///< Finest first.
    MaterialHandle                                    material;  ///< The terrain's layered one.
    glm::mat4                                         model{1.0f};
    Math::AABB                                        bounds;  ///< World, its skirt in it.
    float                                             width = 0.0f;  ///< Metres it spans.
    bool                                              castShadows = true;
};

/**
 * @brief One part of one plant a terrain grows, in the world.
 */
struct TerrainPlantDraw {
    EntityId       entity;  ///< The terrain's.
    MeshHandle     mesh;
    MaterialHandle material;
    glm::mat4      model{1.0f};
    Math::AABB     bounds;              ///< World.
    float          maxDistance = 0.0f;  ///< Metres from the eye past which it is not drawn.
    bool           castShadows = false;
};

/**
 * @brief The plants of one chunk, a run of TerrainFrame::plants, for the cull to pass over whole.
 */
struct TerrainPlantGroup {
    uint32_t   first = 0;           ///< Into TerrainFrame::plants.
    uint32_t   count = 0;
    Math::AABB bounds;              ///< World, every plant in it.
    float      maxDistance = 0.0f;  ///< The farthest any of them is drawn.
};

/**
 * @brief The scene's terrains as chunks and plants this frame (TerrainSystem): FrameContext::terrain.
 *
 * The chunks are listed afresh each frame; the plants only when they change, which plantsRevision
 * says.
 */
struct TerrainFrame {
    std::vector<TerrainChunk>      chunks;
    std::vector<TerrainPlantDraw>  plants;
    std::vector<TerrainPlantGroup> plantGroups;     ///< Every plant in one, in order.
    uint64_t                       plantsRevision = 0;  ///< Moves whenever the plants do.
};

} // namespace Vkm::Engine
