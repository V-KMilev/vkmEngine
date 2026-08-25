#define VKM_LOG_CATEGORY "PHYSICS"

#include "system/physics/authoring/mesh_collider.h"

#include "logger.h"

#include "resource/asset/mesh_asset.h"
#include "system/physics/collision/mesh_bvh.h"
#include "system/physics/tolerance.h"

namespace Vkm::Engine {

uint32_t addMeshCollider(Collider& collider, const MeshAsset& mesh,
                         const glm::vec3& scale) {
    if (mesh.indices.size() < 3) return 0;

    // One mesh part per collider, enforced rather than described. The hierarchy
    // below spans a single triangle range, so a second part would be collided
    // through the first one's tree - silently, and only where the two happen to
    // overlap. Refused rather than quietly replacing what is already there: a
    // caller that asks twice meant to have both, and is owed the news that it
    // cannot.
    for (const ColliderPart& part : collider.parts) {
        if (part.shape != ColliderShape::Mesh) continue;
        LOG_WARNING("addMeshCollider: this collider already has a mesh part, "
                    "and one hierarchy spans one part; the second is refused");
        return 0;
    }

    const uint32_t first = static_cast<uint32_t>(collider.meshPoints.size());
    const size_t triangles = mesh.indices.size() / 3;

    collider.meshPoints.reserve(collider.meshPoints.size() + triangles * 3);
    for (size_t t = 0; t < triangles; ++t) {
        bool whole = true;
        for (size_t c = 0; c < 3; ++c) {
            const uint32_t index = mesh.indices[t * 3 + c];
            if (index >= mesh.vertices.size()) { whole = false; break; }
        }
        // A triangle naming a vertex the mesh does not have would put a corner
        // at the origin and a face across the level; dropped, with the two
        // corners that were fine, since a partial triangle is not one.
        if (!whole) continue;

        for (size_t c = 0; c < 3; ++c) {
            const uint32_t index = mesh.indices[t * 3 + c];
            collider.meshPoints.push_back(mesh.vertices[index].position * scale);
        }
    }

    const uint32_t added =
        static_cast<uint32_t>(collider.meshPoints.size()) - first;
    if (added < 3) {
        collider.meshPoints.resize(first);
        return 0;
    }

    ColliderPart part;
    part.shape = ColliderShape::Mesh;
    part.meshFirst = first;
    part.meshCount = added;
    collider.parts.push_back(part);

    rebuildMeshBvh(collider);
    return added / 3;
}

void rebuildMeshBvh(Collider& collider) {
    // The hierarchy spans one mesh part, so a collider is allowed one. Two
    // would need a tree apiece and a part-to-tree mapping, for a shape whose
    // whole point is being the single piece of static geometry a body sits on.
    ColliderPart* mesh = nullptr;
    for (ColliderPart& part : collider.parts) {
        if (part.shape != ColliderShape::Mesh) continue;
        mesh = &part;
        break;
    }
    if (!mesh || mesh->meshCount < 3) {
        collider.meshNodes.clear();
        return;
    }

    // Bounds-checked like every other reader of this span: a part whose range
    // runs past the buffer describes triangles that are not there, and slicing
    // to it walks off the end rather than answering nothing.
    const size_t last = static_cast<size_t>(mesh->meshFirst) + mesh->meshCount;
    if (last > collider.meshPoints.size()) {
        collider.meshNodes.clear();
        return;
    }

    // The build reorders, so the triangles are lifted out, sorted and put back
    // rather than sorted in place across a buffer other parts also index into.
    std::vector<glm::vec3> triangles(
        collider.meshPoints.begin() + mesh->meshFirst,
        collider.meshPoints.begin() + mesh->meshFirst + mesh->meshCount);

    collider.meshNodes = buildMeshBvh(triangles);

    for (uint32_t i = 0; i < mesh->meshCount; ++i) {
        collider.meshPoints[mesh->meshFirst + i] = triangles[i];
    }
}

} // namespace Vkm::Engine
