#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "resource/resource.h"
#include "resource/handle.h"

#include "resource/asset/material_asset.h"
#include "resource/asset/mesh_asset.h"

namespace Vkm::Engine {

inline constexpr size_t PLANT_LEVELS = 2;  ///< How many coarser levels a plant takes of each part's mesh:
                                           ///< `LOD::levelName(mesh, 1..)`, those made.

/**
 * @brief One part of a plant a terrain scatters: a mesh drawn with a material, placed in the plant.
 *
 * A plant is its prefab's meshes, taken whole when the layer is authored, so the scattering reads no
 * prefab.
 */
struct TerrainPlantPart {
    MeshHandle     mesh;
    MaterialHandle material;
    glm::mat4      local{1.0f};  ///< Where the part lies in the plant.

    std::string unresolvedMesh;  ///< What its file named for a mesh or material that resolved to none
                                 ///< (TerrainFile::resolve): written back in its place until the slot is set,
                                 ///< so a save does not erase it.
    std::string unresolvedMaterial;
};

/**
 * @brief Plants a terrain scatters where their density is painted: grass, flowers, shrubs.
 *
 * Each grid cell gets plants from a hash of its place and the layer's seed, as many as the density
 * at the cell asks, so the same terrain always grows the same plants, on every end.
 */
struct TerrainFoliage {
    std::vector<TerrainPlantPart> parts;                   ///< One plant.
    std::vector<uint8_t>          density;                 ///< A grid vertex each: 255 is perSquareMeter, 0
                                                           ///< none.
    float                         perSquareMeter = 1.0f;   ///< Plants a square meter where the density is
                                                           ///< full.
    float                         minScale       = 0.8f;   ///< The smallest a plant is drawn, a share of its
                                                           ///< size.
    float                         maxScale       = 1.2f;   ///< The largest.
    float                         maxDistance    = 60.0f;  ///< Meters from the eye past which none is drawn.
    uint32_t                      seed           = 0;      ///< Picks where each plant stands and how it
                                                           ///< turns.
    bool                          castShadows    = false;  ///< Grass need not; a shrub looks afloat without.
};

/**
 * @brief A terrain: a square grid of heights, the share of each of four materials painted at each
 *        grid vertex, and the plants scattered over it.
 *
 * Centered on its entity: the grid spans size meters along local X and Z, vertex (i, j) at
 * x = (i / (resolution - 1) - 0.5) * size, z alike, its height the local Y. Authored by the editor's
 * brushes and kept in a .terrain file of the project (TerrainFile), which every host reads as it is:
 * a terrain has no cooked file.
 */
struct TerrainAsset : public Resource {
    static constexpr uint32_t MIN_RESOLUTION  = 33;    ///< The fewest grid vertices a side: one chunk's.
    static constexpr uint32_t MAX_RESOLUTION  = 1025;  ///< The most: what a terrain's meshes, collider and
                                                       ///< maps are sized for.
    static constexpr uint32_t MAX_FOLIAGE     = 8;     ///< The most foliage layers.
    static constexpr uint32_t MAX_PLANT_PARTS = 8;     ///< The most parts a plant has.

    static constexpr uint32_t WEIGHTS_PER_VERTEX = MaterialAsset::MAX_LAYERS;  ///< A vertex's shares in
                                                                               ///< weights, one a layer.

    uint32_t resolution = 129;    ///< Grid vertices a side, 2^n + 1 from MIN_ to MAX_RESOLUTION.
    float    size       = 64.0f;  ///< Meters a side.

    std::vector<float>   heights;  ///< resolution^2, row by row along X: meters up from the entity.
    std::vector<uint8_t> weights;  ///< resolution^2 * WEIGHTS_PER_VERTEX: each vertex's share of each
                                   ///< layer, R to A.

    std::array<MaterialHandle, MaterialAsset::MAX_LAYERS> layers{};
        ///< What each share is drawn with; an empty layer draws black where it is painted.
    std::array<std::string, MaterialAsset::MAX_LAYERS> unresolvedLayers{};
        ///< What its file named for each layer that resolved to none, kept as TerrainPlantPart's are.
    std::array<float, MaterialAsset::MAX_LAYERS> layerTiling{4.0f, 4.0f, 4.0f, 4.0f};
        ///< Meters one repeat of each layer's maps spans: sand finer than a field, whatever the layer's
        ///< material says for a mesh.

    std::vector<TerrainFoliage> foliage;  ///< Up to MAX_FOLIAGE.

    /// Meters between neighboring grid vertices.
    float spacing() const { return size / static_cast<float>(resolution - 1); }

    /**
     * @brief Whether @p resolution is one a terrain may have.
     *
     * @param resolution Grid vertices a side.
     * @return True for 2^n + 1 from MIN_RESOLUTION to MAX_RESOLUTION.
     */
    static bool validResolution(uint32_t resolution);

    /**
     * @brief Whether heights and weights hold the grid resolution names, so they may be read and written
     *        as one.
     *
     * @return True for a valid resolution with both arrays its size.
     */
    bool holdsGrid() const;

    /**
     * @brief The height at local @p x, @p z, the surface its triangles draw there.
     *
     * Each grid square is two triangles split along its diagonal from (i, j) to (i + 1, j + 1), as
     * the meshes and the collider make it.
     *
     * @param x Local meters along X; clamped to the grid.
     * @param z Local meters along Z; clamped to the grid.
     * @return Local meters up.
     */
    float heightAt(float x, float z) const;

    /**
     * @brief Regrid to @p to vertices a side over the same size, keeping the shape, the paint and
     *        the plants.
     *
     * Heights are read off the surface as drawn (heightAt), so a finer grid keeps every old vertex's
     * height and a coarser one keeps those it still has. Layer shares and plant densities blend
     * between the neighboring vertices, the shares summing to full again.
     *
     * @param to Grid vertices a side.
     * @return False, changing nothing, when @p to is no valid resolution or the grid is unsound.
     */
    bool resample(uint32_t to);

    /**
     * @brief The surface's normal at grid vertex @p i, @p j, from its neighbors' heights.
     *
     * @param i Along X, clamped to the grid.
     * @param j Along Z, clamped to the grid.
     * @return Unit normal, local.
     */
    glm::vec3 normalAt(int i, int j) const;

    /**
     * @brief Each asset it names, in its file's order: its layers' materials, then each plant part's
     *        mesh and material.
     *
     * @param material Called with each material, a repeat included.
     * @param mesh     Called with each mesh, a repeat included.
     */
    void forEachReference(
        const std::function<void(MaterialHandle)>& material,
        const std::function<void(MeshHandle)>& mesh
    ) const;
};

using TerrainHandle = Handle<TerrainAsset>;

} // namespace Vkm::Engine
