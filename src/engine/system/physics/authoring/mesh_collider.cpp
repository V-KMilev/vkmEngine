#define VKM_LOG_CATEGORY "PHYSICS"

#include "system/physics/authoring/mesh_collider.h"

#include <algorithm>
#include <vector>

#include "logger.h"

#include "resource/asset/mesh_asset.h"
#include "resource/asset/terrain_asset.h"
#include "resource/resource_manager.h"
#include "system/physics/collision/mesh_bvh.h"

namespace Vkm::Engine {

namespace {

/// The collider's first part made of triangles, a mesh's or a terrain's.
const ColliderPart* trianglePartOf(const Collider& collider) {
    for (const ColliderPart& part : collider.parts) {
        if (isTriangles(part.shape)) return &part;
    }
    return nullptr;
}

/// Grid squares a terrain tree's leaf holds at most.
constexpr uint32_t TERRAIN_LEAF_SQUARES = 4;

/**
 * @brief A terrain's squares as triangles and the tree over them, built from the grid: halving its
 *        rectangle of squares along the longer side gives the tree with no sort, so a brush stroke
 *        rebuilds it as it goes.
 */
class TerrainTree {
    public:
        explicit TerrainTree(const TerrainAsset& terrain)
            : m_terrain(terrain)
            , m_spacing(terrain.spacing())
            , m_half(0.5f * terrain.size)
        {}
        ~TerrainTree() = default;

        TerrainTree(const TerrainTree& other) = delete;
        TerrainTree& operator=(const TerrainTree& other) = delete;

        TerrainTree(TerrainTree && other) = delete;
        TerrainTree& operator=(TerrainTree && other) = delete;

    public:
        /**
         * @brief Every square of the grid, two triangles each, split as TerrainAsset::heightAt
         *        reads them.
         *
         * @param[out] points Three corners a triangle, in the tree's order.
         * @param[out] nodes  The tree, root first.
         */
        void build(std::vector<glm::vec3>& points, std::vector<MeshNode>& nodes) {
            const uint32_t squares = m_terrain.resolution - 1;
            points.clear();
            nodes.clear();
            points.reserve(static_cast<size_t>(squares) * squares * 6);
            nodes.reserve(static_cast<size_t>(squares) * squares / TERRAIN_LEAF_SQUARES * 2);
            add(points, nodes, 0, squares, 0, squares);
        }

    private:
        glm::vec3 corner(uint32_t i, uint32_t j) const {
            const float x = static_cast<float>(i) * m_spacing - m_half;
            const float z = static_cast<float>(j) * m_spacing - m_half;
            return {x, m_terrain.heights[static_cast<size_t>(j) * m_terrain.resolution + i], z};
        }

        /// The node over squares [i0, i1) by [j0, j1); its index in @p nodes.
        uint32_t add(
            std::vector<glm::vec3>& points,
            std::vector<MeshNode>& nodes,
            uint32_t i0,
            uint32_t i1,
            uint32_t j0,
            uint32_t j1
        ) {
            const auto at = static_cast<uint32_t>(nodes.size());
            nodes.emplace_back();
            if ((i1 - i0) * (j1 - j0) <= TERRAIN_LEAF_SQUARES) {
                const auto first = static_cast<uint32_t>(points.size());
                for (uint32_t j = j0; j < j1; ++j) {
                    for (uint32_t i = i0; i < i1; ++i) {
                        const glm::vec3 v00 = corner(i, j);
                        const glm::vec3 v11 = corner(i + 1, j + 1);
                        points.insert(points.end(), {v00, v11, corner(i + 1, j), v00, corner(i, j + 1), v11});
                    }
                }
                MeshNode& leaf     = nodes[at];
                leaf.firstTriangle = first / 3;
                leaf.triangleCount = (static_cast<uint32_t>(points.size()) - first) / 3;
                leaf.min           = points[first];
                leaf.max           = points[first];
                for (size_t p = first; p < points.size(); ++p) {
                    leaf.min = glm::min(leaf.min, points[p]);
                    leaf.max = glm::max(leaf.max, points[p]);
                }
                return at;
            }
            uint32_t right = 0;
            if (i1 - i0 >= j1 - j0) {
                const uint32_t middle = (i0 + i1) / 2;
                add(points, nodes, i0, middle, j0, j1);
                right = add(points, nodes, middle, i1, j0, j1);
            } else {
                const uint32_t middle = (j0 + j1) / 2;
                add(points, nodes, i0, i1, j0, middle);
                right = add(points, nodes, i0, i1, middle, j1);
            }
            MeshNode& node  = nodes[at];
            node.rightChild = right;
            node.min        = glm::min(nodes[at + 1].min, nodes[right].min);
            node.max        = glm::max(nodes[at + 1].max, nodes[right].max);
            return at;
        }

    private:
        const TerrainAsset& m_terrain;
        float               m_spacing;
        float               m_half;
};

/// Every whole triangle of @p mesh, scaled, three corners each, wound to face out.
std::vector<glm::vec3> trianglesOf(const MeshAsset& mesh, const glm::vec3& scale) {
    std::vector<glm::vec3> points;
    const size_t triangles = mesh.indices.size() / 3;
    points.reserve(triangles * 3);

    // A triangle is solid on one side, told by its winding: a mirroring scale turns it inside out.
    const bool mirrored = scale.x * scale.y * scale.z < 0.0f;
    for (size_t t = 0; t < triangles; ++t) {
        bool whole = true;
        for (size_t c = 0; c < 3; ++c) {
            if (mesh.indices[t * 3 + c] >= mesh.vertices.size()) {
                whole = false;
                break;
            }
        }
        // An out-of-range index would put a corner at the origin; the whole triangle is dropped.
        if (!whole) continue;

        for (size_t c = 0; c < 3; ++c) {
            const size_t corner = mirrored && c > 0 ? 3 - c : c;
            points.push_back(mesh.vertices[mesh.indices[t * 3 + corner]].position * scale);
        }
    }
    return points;
}

} // namespace

uint32_t addMeshCollider(
    Collider& collider,
    MeshHandle mesh,
    const ResourceManager& resources,
    const glm::vec3& scale
) {
    if (trianglePartOf(collider)) {
        LOG_WARNING(
            "addMeshCollider: this collider already has a mesh part, and a "
            "collider holds one set of triangles; the second is refused"
        );
        return 0;
    }

    ColliderPart part;
    part.shape     = ColliderShape::Mesh;
    part.mesh      = mesh;
    part.meshScale = scale;
    collider.parts.push_back(part);
    syncMeshCollider(collider, resources);

    if (collider.meshPoints.empty()) {
        collider.parts.pop_back();
        collider.meshBuiltFrom = MeshColliderSource{};
        return 0;
    }
    return static_cast<uint32_t>(collider.meshPoints.size() / 3);
}

bool syncMeshCollider(Collider& collider, const ResourceManager& resources) {
    const ColliderPart* part = trianglePartOf(collider);
    if (part && part->shape == ColliderShape::Terrain) return false;
    const MeshAsset* asset = part ? resources.tryGet(part->mesh) : nullptr;

    MeshColliderSource source;
    if (asset) {
        source.uid     = asset->uid();
        source.version = asset->version();
        source.scale   = part->meshScale;
    }
    const MeshColliderSource& built = collider.meshBuiltFrom;
    if (source.uid == built.uid && source.version == built.version && source.scale == built.scale) {
        return false;
    }

    collider.meshBuiltFrom = source;
    collider.meshPoints.clear();
    collider.meshNodes.clear();
    if (!asset) return true;

    collider.meshPoints = trianglesOf(*asset, part->meshScale);
    collider.meshNodes  = buildMeshBvh(collider.meshPoints);
    // A scene may hold more; each would collide as this one's triangles, placed at its own centre.
    const auto isMesh    = [](const ColliderPart& p) { return isTriangles(p.shape); };
    const auto meshParts = std::count_if(collider.parts.begin(), collider.parts.end(), isMesh);
    if (meshParts > 1) {
        LOG_WARNING(
            "Collider: %d mesh parts, and a collider holds one set of triangles - "
            "the others collide as the first's; merge them into one mesh",
            static_cast<int>(meshParts)
        );
    }
    return true;
}

bool syncTerrainCollider(Collider& collider, const TerrainAsset* terrain) {
    const ColliderPart* part = trianglePartOf(collider);
    if (!part || part->shape != ColliderShape::Terrain) return false;
    // Placed where the terrain is drawn, at the entity, whatever a centre says.
    for (ColliderPart& each : collider.parts) {
        if (each.shape == ColliderShape::Terrain) each.center = glm::vec3(0.0f);
    }

    MeshColliderSource source;
    if (terrain) {
        source.uid     = terrain->uid();
        source.version = terrain->version();
        source.scale   = glm::vec3(1.0f);
    }
    const MeshColliderSource& built = collider.meshBuiltFrom;
    if (source.uid == built.uid && source.version == built.version && source.scale == built.scale) {
        return false;
    }
    collider.meshBuiltFrom = source;
    collider.meshPoints.clear();
    collider.meshNodes.clear();
    const bool sound = terrain && TerrainAsset::validResolution(terrain->resolution)
        && terrain->heights.size() == static_cast<size_t>(terrain->resolution) * terrain->resolution;
    if (sound) TerrainTree(*terrain).build(collider.meshPoints, collider.meshNodes);
    return true;
}

} // namespace Vkm::Engine
