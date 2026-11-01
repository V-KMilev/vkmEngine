#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "base/math/aabb.h"

#include "resource/resource.h"
#include "resource/handle.h"

namespace Vkm::Engine {

struct SkeletonAsset;

/**
 * @brief One vertex: position, normal, texture coordinates and tangent frame.
 *
 * The tangent runs the way U grows and cross(normal, tangent) * w the way V grows.
 */
struct Vertex {
    glm::vec3 position;  ///< Model-space position.
    glm::vec3 normal;    ///< Surface normal, model space.
    glm::vec2 uv;        ///< Texture coordinates.
    glm::vec4 tangent;   ///< Along +U; w is the handedness, +1 or -1.
};

/**
 * @brief One vertex's binding to the rig, in a stream parallel to `vertices`.
 *
 * Kept out of Vertex, which would grow 25% for every mesh. 16-bit indices
 * avoid a 255-bone ceiling in the cooked format. Weights sum to exactly 255, so
 * `w / 255.0` sums to 1.0 with no renormalize. Its limits on the GPU: docs/reference/animation.md,
 * "The GPU path".
 */
struct SkinVertex {
    uint16_t bones[4];    ///< Indices into the skeleton named by MeshAsset::skeleton.
    uint8_t  weights[4];  ///< unorm8 influences, summing to exactly 255.
};

/**
 * @brief CPU-side geometry: indexed triangle mesh plus a cached local-space AABB.
 */
struct MeshAsset : public Resource {
    static constexpr uint32_t LIGHTMAP_PACK_TEXELS  = 256;  ///< The side of the lightmap lightmapUVs' charts
                                                            ///< are packed for, in texels.
    static constexpr uint32_t LIGHTMAP_PACK_PADDING = 4;    ///< The texels the packing holds between charts,
                                                            ///< at that side.

    std::vector<Vertex>   vertices = {};
    std::vector<uint32_t> indices  = {};

    std::vector<SkinVertex> skin = {};  ///< Per-vertex rig binding: empty, or exactly `vertices.size()` long.
                                        ///< A mesh is skinned iff this is non-empty.

    std::vector<glm::vec2> lightmapUVs = {};  ///< Per-vertex lightmap coordinates: empty, or exactly
                                              ///< `vertices.size()` long. A second unwrap of the surface into
                                              ///< [0, 1], no two triangles sharing a point and its charts
                                              ///< held apart, so a baked lightmap keeps each point's own
                                              ///< light. Filled by the cook (unwrapLightmap), which says
                                              ///< which meshes it leaves out.

    std::string skeleton = "";  ///< Name of the rig `skin`'s indices address; empty when unskinned. A
                                ///< compatibility tag, not a dependency: the pose comes from whatever rig
                                ///< drives it. SkeletalAnimationSystem warns when the two names disagree.

    float skinRadius = 0.0f;  ///< Largest distance from a vertex to any bone that influences it. What a
                              ///< bone-origin box is inflated by to contain the skin. Zero when unskinned.

    glm::vec3 boundsMin{0};  ///< Local space
    glm::vec3 boundsMax{0};  ///< Local space

    /**
     * @brief The local-space bounds as one box.
     *
     * @return The box spanning boundsMin to boundsMax.
     */
    Math::AABB bounds() const noexcept { return {boundsMin, boundsMax}; }

    bool loading = false;  ///< True while an async decode is in flight. Cleared by AsyncLoaderSystem; until
                           ///< then the mesh is empty and draws nothing.

    /**
     * @brief Compute the local-space AABB from vertex positions and store it in
     *        boundsMin / boundsMax.
     *
     * An empty mesh yields a zero-extent box at the origin.
     */
    void computeAndSetBounds();

    /**
     * @brief Compute how far a vertex sits from the bones that drive it and
     *        store it in skinRadius.
     *
     * Required after filling a skinned mesh: zero is a wrong box, not a small one
     * (see VisibilitySystem). Zero when unskinned or the skin is not parallel.
     *
     * @param rig The rig `skin`'s bone indices address, for the bind-pose origins.
     */
    void computeAndSetSkinRadius(const SkeletonAsset& rig);
};

/**
 * @brief Why @p mesh cannot be drawn, or an empty string when it can.
 *
 * What the vertex stage, meshoptimizer and a remap rely on unchecked: whole triangles,
 * streams as long as the vertices or absent, indices that name a vertex, bone indices
 * within a rig (SkeletonAsset::MAX_BONES), skin weights summing to 255 and a skin radius
 * that is a length.
 *
 * @param mesh Mesh to judge.
 * @return The first fault found, worded to follow "cannot be drawn - ".
 */
std::string findMeshFault(const MeshAsset& mesh);

using MeshHandle = Handle<MeshAsset>;

} // namespace Vkm::Engine
