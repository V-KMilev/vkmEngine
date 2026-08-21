#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>

#include "resource/resource.h"
#include "resource/resource_handle.h"

namespace Vkm::Engine {

struct SkeletonAsset;

/**
 * @brief Represents a single vertex in a mesh.
 */
struct Vertex {
    glm::vec3 position;    ///< Model-space position.
    glm::vec3 normal;      ///< Surface normal, model space.
    glm::vec2 uv;          ///< Texture coordinates.
    glm::vec4 tangent;     ///< Tangent vector (x, y, z, w), used for normal mapping; w is handedness.
};

/**
 * @brief One vertex's binding to the rig, in a stream parallel to `vertices`.
 *
 * Kept out of Vertex on purpose: folding four indices and four weights into it
 * would cost every vertex of every mesh in the engine 25% more bandwidth, paid
 * hardest by the shadow pass, which reads only `aPos` and replays the geometry
 * per cascade tile and per cube face. A rock does not pay for skinning.
 *
 * Indices are 16-bit because the cooked format has no migration path and an
 * 8-bit index would weld a 255-bone ceiling into it permanently. Weights are
 * quantised so the four bytes sum to exactly 255, which makes `w / 255.0` sum
 * to exactly 1.0 and spares every vertex stage a renormalise.
 */
struct SkinVertex {
    uint16_t bones[4];    ///< Indices into the skeleton named by MeshAsset::skeleton.
    uint8_t  weights[4];  ///< unorm8 influences, summing to exactly 255.
};
static_assert(sizeof(SkinVertex) == 12, "SkinVertex layout changed - bump MESH_FORMAT_VERSION");
static_assert(std::is_trivially_copyable_v<SkinVertex>, "SkinVertex must be trivially copyable to bulk-write");

/**
 * @brief CPU-side geometry: indexed triangle mesh plus a cached local-space AABB.
 *
 * The backend uploads `vertices`/`indices` to GPU buffers on sync; the bounds
 * feed frustum/size culling and are (re)computed via computeAndSetBounds().
 */
struct MeshAsset : public Resource {
    std::vector<Vertex>   vertices = {};  ///< Vertex buffer (geometry)
    std::vector<uint32_t> indices  = {};  ///< Index buffer (triangle indices)

    /**
     * @brief Per-vertex rig binding: empty, or exactly `vertices.size()` long.
     *
     * A mesh is skinned iff this is non-empty - the asset already knows, so no
     * component has to say so.
     */
    std::vector<SkinVertex> skin = {};

    /**
     * @brief Name of the rig `skin`'s indices address; empty when unskinned.
     *
     * A compatibility tag rather than a dependency, which is why it is a name
     * and not a handle: the mesh uploads its skin stream either way, and the
     * pose it is drawn with comes from whatever rig is driving it. The runtime
     * warns when the two names disagree, which reports the failure that
     * actually happens - a rig assigned to the wrong character - instead of
     * exploding the geometry and leaving the cause to be guessed at.
     */
    std::string skeleton = "";

    /**
     * @brief Largest distance from a vertex to any bone that influences it.
     *
     * How far a posed vertex can travel from its bone's origin, which is what a
     * bone-origin bounding box has to be inflated by before it contains the
     * skin. Zero when unskinned.
     */
    float skinRadius = 0.0f;

    glm::vec3 boundsMin{0};           ///< Minimum AABB point in local space
    glm::vec3 boundsMax{0};           ///< Maximum AABB point in local space

    /**
     * @brief True while an async decode is in flight.
     *
     * Flipped false once AsyncLoaderSystem finalises the upload.
     * VisibilitySystem already skips meshes with zero-extent bounds, so
     * a loading mesh stays invisible until its data arrives - no
     * fallback needed.
     */
    bool loading = false;

    /**
     * @brief Compute the local-space AABB from vertex positions and store it in
     * boundsMin / boundsMax (used for frustum/size culling).
     *
     * Call after creating or loading the mesh; an empty mesh yields a
     * zero-extent box at the origin.
     */
    void computeAndSetBounds();

    /**
     * @brief Compute how far a vertex sits from the bones that drive it and
     *        store it in skinRadius.
     *
     * Call after filling `vertices` and `skin`, exactly as computeAndSetBounds()
     * is called: whoever authors a skinned mesh owes it this number. Leaving it
     * at zero is not a smaller box but a wrong one - the posed bounds are the
     * box of the posed bone origins inflated by this radius, and the occlusion
     * cull keeps conservatively, so it does not over-draw an under-sized box, it
     * deletes the character.
     *
     * An unskinned mesh - or one whose skin stream is not parallel to its
     * vertices, which the cooked reader rejects outright - yields zero.
     *
     * @param skeleton The rig `skin`'s bone indices address, supplying the
     *        bind-pose origins the distances are measured from.
     */
    void computeAndSetSkinRadius(const SkeletonAsset& skeleton);
};

using MeshHandle = Handle<MeshAsset>;

} // namespace Vkm::Engine
