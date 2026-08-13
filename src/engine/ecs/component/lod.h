#pragma once

#include <vector>

#include "resource/asset/mesh_asset.h"

namespace Engine {

/**
 * @brief One detail level: the mesh to draw while the entity is near enough.
 */
struct LODLevel {
    MeshHandle mesh;                  ///< Geometry for this level.
    float      maxDistance = 0.0f;    ///< Use this level while the camera is within this range.
};

/**
 * @brief Distance-selected geometry for an entity.
 *
 * The Mesh component still decides material, visibility and shadow casting; this
 * only replaces which geometry those apply to. Keeping them separate means an
 * entity gains LOD by adding a component, and everything that reasons about
 * materials or draw buckets is untouched.
 *
 * Levels are ordered near to far and selected on distance to the entity's bounds
 * centre. Past the last level the last level keeps drawing - dropping the entity
 * entirely is DistanceCuller's job, and having two components able to make
 * something vanish would make it ambiguous which one did.
 *
 * Selection happens inside the visibility cull, which already has the distance
 * and already runs in parallel, so LOD costs a comparison per visible entity
 * rather than a pass of its own.
 */
struct LOD {
    std::vector<LODLevel> levels;

    /**
     * @brief Scales every level's range; >1 keeps detail longer.
     *
     * The knob for trading quality against cost globally-per-entity without
     * re-authoring thresholds - a quality setting can drive it.
     */
    float bias = 1.0f;
};

} // namespace Engine
