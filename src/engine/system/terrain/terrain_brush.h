#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "resource/asset/terrain_asset.h"

namespace Vkm::Engine {

/**
 * @brief The edits a brush makes to a terrain, one dab at a time: raise, smooth and flatten its
 *        heights, paint a layer's share, and grow or thin a plant layer.
 *
 * Each changes the asset in place and returns the grid vertices it touched; the caller commits
 * the asset (ResourceManager::commit), which redraws the touched chunks and rebuilds the terrain's
 * collider and plants. A dab's effect fades from its center to its rim, so strokes made of many
 * dabs blend.
 */
namespace TerrainBrush {

/**
 * @brief Grid vertices [i0, i1] by [j0, j1], inclusive: what a dab touched.
 */
struct Region {
    int i0 = 0;
    int j0 = 0;
    int i1 = -1;  ///< Below i0 when nothing was touched.
    int j1 = -1;

    bool empty() const { return i1 < i0 || j1 < j0; }

    /// Grow to hold @p other too.
    void add(const Region& other);
};

/**
 * @brief One application of a brush: where, how wide, how much.
 */
struct Dab {
    glm::vec2 center{0.0f};     ///< Local meters along X and Z.
    float     radius   = 4.0f;  ///< Meters.
    float     strength = 1.0f;  ///< How much of the full effect this dab makes, 0 to 1.
    float     hardness = 0.3f;  ///< How much of the radius takes the full effect before it fades: 0 fades
                                ///< from the center, 1 is a flat disc.
};

/**
 * @brief How much of a dab's effect reaches @p distance from its center.
 *
 * @param dab      The dab.
 * @param distance Meters from its center.
 * @return 1 within its hard core, falling smoothly to 0 at its radius.
 */
float falloff(const Dab& dab, float distance);

/**
 * @brief The grid vertices within @p dab, clamped to @p terrain's grid.
 *
 * @param terrain The terrain.
 * @param dab     The dab.
 * @return Their region; empty when the dab misses the grid.
 */
Region reach(const TerrainAsset& terrain, const Dab& dab);

/**
 * @brief Where a ray first meets @p terrain's surface, over its grid.
 *
 * Marched a half grid step at a time, then narrowed to the crossing, so a ray grazing a ridge
 * thinner than that may pass it.
 *
 * @param terrain   The terrain.
 * @param origin    Local meters.
 * @param direction Unit, local.
 * @param range     Meters along it to look.
 * @param[out] t    Meters along it to the surface; untouched on false.
 * @return False when it meets none within @p range.
 */
bool hit(
    const TerrainAsset& terrain,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float range,
    float& t
);

/**
 * @brief Raise the heights under @p dab, @p meters at its center; negative lowers.
 *
 * @param terrain The terrain.
 * @param dab     Where.
 * @param meters  How far its center moves.
 * @return What it touched.
 */
Region raise(TerrainAsset& terrain, const Dab& dab, float meters);

/**
 * @brief Pull each height under @p dab toward its neighbors' mean.
 *
 * @param terrain The terrain.
 * @param dab     Where; its strength is the share of the way each height moves.
 * @return What it touched.
 */
Region smooth(TerrainAsset& terrain, const Dab& dab);

/**
 * @brief Pull each height under @p dab toward @p height.
 *
 * @param terrain The terrain.
 * @param dab     Where; its strength is the share of the way each height moves.
 * @param height  Local meters: the level a path or a plot is cut to.
 * @return What it touched.
 */
Region flatten(TerrainAsset& terrain, const Dab& dab, float height);

/**
 * @brief Move each vertex's layer shares under @p dab toward all of @p layer.
 *
 * The shares still add up to 255 after.
 *
 * @param terrain The terrain.
 * @param dab     Where; its strength is the share of the way each vertex moves.
 * @param layer   Below MaterialAsset::MAX_LAYERS.
 * @return What it touched.
 */
Region paint(TerrainAsset& terrain, const Dab& dab, uint32_t layer);

/**
 * @brief Move the density of plant layer @p foliage under @p dab toward @p target.
 *
 * Gives the layer a density map first if it has none.
 *
 * @param terrain The terrain.
 * @param dab     Where; its strength is the share of the way each vertex moves.
 * @param foliage Below terrain.foliage.size().
 * @param target  0 thins it out, 1 grows it full.
 * @return What it touched.
 */
Region grow(TerrainAsset& terrain, const Dab& dab, uint32_t foliage, float target);

} // namespace TerrainBrush

} // namespace Vkm::Engine
