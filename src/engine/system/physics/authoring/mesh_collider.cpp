#define VKM_LOG_CATEGORY "PHYSICS"

#include "system/physics/authoring/mesh_collider.h"

#include <vector>

#include "logger.h"

#include "resource/asset/mesh_asset.h"
#include "resource/resource_manager.h"
#include "system/physics/collision/mesh_bvh.h"

namespace Vkm::Engine {

namespace {

const ColliderPart* meshPartOf(const Collider& collider) {
    for (const ColliderPart& part : collider.parts) {
        if (part.shape == ColliderShape::Mesh) return &part;
    }
    return nullptr;
}

/// Every whole triangle of @p mesh, scaled, three corners each.
std::vector<glm::vec3> trianglesOf(const MeshAsset& mesh, const glm::vec3& scale) {
    std::vector<glm::vec3> points;
    const size_t triangles = mesh.indices.size() / 3;
    points.reserve(triangles * 3);
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
            points.push_back(mesh.vertices[mesh.indices[t * 3 + c]].position * scale);
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
    if (meshPartOf(collider)) {
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
    const ColliderPart* part  = meshPartOf(collider);
    const MeshAsset*    asset = part ? resources.tryGet(part->mesh) : nullptr;

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
    return true;
}

} // namespace Vkm::Engine
