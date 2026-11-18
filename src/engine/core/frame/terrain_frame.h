#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "base/math/aabb.h"
#include "ecs/entity.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/terrain_asset.h"

namespace Vkm::Engine {

// How a terrain is cut into chunks, the shape TerrainFrame holds them in, and the level of one the
// eye sees; the rest of TerrainChunks is the terrain system's.
namespace TerrainChunks {

inline constexpr uint32_t CELLS = 32;  ///< Grid squares a chunk spans a side at its finest level.
inline constexpr uint32_t LEVELS = 4;  ///< Its levels, the k-th taking every 2^k-th vertex.
inline constexpr float REACH = 1.5f;  ///< Chunk widths from the eye within which a chunk takes its finest
                                      ///< level.

/**
 * @brief The level a chunk takes at @p distance from the eye.
 *
 * Each level holds twice as far as the one before: the finest within REACH chunk widths.
 *
 * @param distance   Meters from the eye to the chunk's box.
 * @param chunkWidth Meters a chunk spans.
 * @return The level, below LEVELS.
 */
uint32_t levelAt(float distance, float chunkWidth);

} // namespace TerrainChunks

/**
 * @brief One chunk of a terrain in the world: its levels' meshes and where it lies, for the cull
 *        to list beside the scene's meshes.
 */
struct TerrainChunk {
    EntityId                                      entity;              ///< The terrain's.
    std::array<MeshHandle, TerrainChunks::LEVELS> levels;              ///< Finest first.
    MaterialHandle                                material;            ///< The terrain's layered one.
    glm::mat4                                     model{1.0f};
    Math::AABB                                    bounds;              ///< World, its skirt in it.
    float                                         width       = 0.0f;  ///< Meters it spans.
    bool                                          castShadows = true;
};

/**
 * @brief One part of one plant a terrain grows, in the world.
 */
struct TerrainPlantDraw {
    EntityId       entity;              ///< The terrain's.
    MeshHandle     mesh;
    MaterialHandle material;
    glm::mat4      model{1.0f};
    Math::AABB     bounds;              ///< World.
    float          maxDistance = 0.0f;  ///< Meters from the eye past which it is not drawn.
    bool           castShadows = false;

    std::array<MeshHandle, PLANT_LEVELS> levels{};     ///< Its mesh's coarser levels (LOD::levelName), those
                                                       ///< it has first.
    std::array<float, PLANT_LEVELS>      levelFrom{};  ///< Meters from the eye past which each is drawn.

    /**
     * @brief The mesh it draws from @p distance away.
     *
     * @param distance Meters from the eye.
     * @return Its coarsest level past its reach, or its own mesh.
     */
    const MeshHandle& meshAt(float distance) const {
        for (size_t level = PLANT_LEVELS; level-- > 0;) {
            if (levels[level] && distance >= levelFrom[level]) return levels[level];
        }
        return mesh;
    }
};

/**
 * @brief The plants of one chunk, a run of TerrainFrame::plants, for the cull to pass over whole.
 */
struct TerrainPlantGroup {
    uint32_t   first       = 0;     ///< Into TerrainFrame::plants.
    uint32_t   count       = 0;
    Math::AABB bounds;              ///< World, every plant in it.
    float      maxDistance = 0.0f;  ///< The farthest any of them is drawn.
};

/**
 * @brief The scene's terrains as chunks and plants this frame (TerrainSystem): FrameProducts::terrain.
 *
 * The chunks are listed afresh each frame; the plants only when they change, which plantsRevision
 * says.
 */
struct TerrainFrame {
    std::vector<TerrainChunk>      chunks;
    std::vector<TerrainPlantDraw>  plants;              ///< Each terrain's one run, which the cull keys them
                                                        ///< by.
    std::vector<TerrainPlantGroup> plantGroups;         ///< Every plant in one, in order.
    uint64_t                       plantsRevision = 0;  ///< Moves whenever the plants do.
};

} // namespace Vkm::Engine
