#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/system.h"
#include "ecs/entity.h"
#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/terrain_asset.h"
#include "resource/asset/texture_asset.h"
#include "system/terrain/terrain_frame.h"
#include "system/terrain/terrain_plants.h"

namespace Vkm::Engine {

class Scene;
struct Transform;

/**
 * @brief Where a terrain lies: its entity's world position and turn, unscaled, as the
 *        solver places its collider.
 *
 * @param scene  Holds the entity.
 * @param entity The terrain's entity.
 * @param local  Its Transform.
 * @return The terrain's model matrix.
 */
glm::mat4 terrainModel(const Scene& scene, EntityId entity, const Transform& local);

/**
 * @brief Makes each Terrain's meshes, layer weights and layered material from its asset, scatters
 *        its plants, and publishes where its chunks and plants lie as FrameContext::terrain, for the
 *        cull to list.
 *
 * What it makes are private assets (ResourceManager::addPrivate), derived and never saved: the asset
 * is the terrain. Remade when the asset's version moves - a brush stroke - each chunk only where the
 * heights it is built from changed, and the weights only when they did. At Transform, after
 * HierarchySystem, so a terrain under a parent lies where the parent does this frame.
 */
class TerrainSystem : public System {
    public:
        TerrainSystem() = default;
        ~TerrainSystem() override = default;

        TerrainSystem(const TerrainSystem& other) = delete;
        TerrainSystem& operator=(const TerrainSystem& other) = delete;

        TerrainSystem(TerrainSystem && other) = delete;
        TerrainSystem& operator=(TerrainSystem && other) = delete;

    public:
        void update(FrameContext& ctx) override;

    private:
        /// What one terrain entity's asset was last made into.
        struct Made {
            TerrainHandle terrain;
            uint64_t      version    = 0;
            uint32_t      resolution = 0;
            float         size       = 0.0f;
            /// Per chunk, its levels: finest first.
            std::vector<std::array<MeshHandle, TerrainChunks::LEVELS>> chunks;
            std::vector<Math::AABB> bounds;   ///< Per chunk, local.
            TextureHandle           weightMap;
            MaterialHandle          material;
            std::vector<float>      heights;  ///< What the chunks were built from.
            std::vector<uint8_t>    weights;  ///< What the weight map was made from.
            std::vector<TerrainFoliage> foliage;  ///< What the plants were grown from.
            std::vector<TerrainPlants::Plant> plants;  ///< Where its plants stand, local, chunk by chunk.
            std::vector<uint32_t>   plantRuns;  ///< Per chunk, where its plants begin; then their end.
            glm::mat4               model{1.0f};  ///< Where it lies this frame.
            EntityId                entity;       ///< The terrain's.
            bool                    castShadows = true;  ///< Its Terrain's: no plant casts without it.
            bool                    seen = false;
        };

    private:
        /**
         * @brief Make @p made's meshes, weights and material from @p terrain as it is now.
         *
         * @param resources Where they are kept.
         * @param made      What was made before.
         * @param terrain   The asset.
         * @param slot      The terrain entity's slot, naming what is made.
         */
        void remake(ResourceManager& resources, Made& made, TerrainHandle terrain, uint32_t slot);

        /// Remove what @p made made.
        static void release(ResourceManager& resources, Made& made);

        /**
         * @brief List every terrain's plants in the world, a group a chunk.
         *
         * @param resources Holds the plants' meshes, whose boxes place them.
         * @param scene     Holds the terrains.
         */
        void listPlants(const ResourceManager& resources, const Scene& scene);

        /**
         * @brief What the plants listed are made from: the terrains, their versions and places, and
         *        which of their meshes have loaded.
         *
         * @param resources Holds the plants' meshes.
         * @param scene     Holds the terrains.
         * @return Equal while a list of the plants would be the same.
         */
        uint64_t plantsKey(const ResourceManager& resources, const Scene& scene) const;

    private:
        std::unordered_map<uint32_t, Made> m_made;  ///< By terrain entity slot.
        uint64_t                           m_epoch = 0;  ///< The asset graph m_made's handles are in.
        TerrainFrame                       m_frame;
        uint64_t                           m_plantsKey = 0;  ///< plantsKey() of the plants listed.
};

} // namespace Vkm::Engine
