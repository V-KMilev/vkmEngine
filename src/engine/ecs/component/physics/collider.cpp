#include "ecs/component/physics/collider.h"

#include <vector>

#include "vkm_log/assert.h"

namespace Vkm::Engine {

namespace {

// Reused so a call does not allocate; per thread, as a query may run on any.
thread_local std::vector<Physics::Part> t_parts;

/// @p collider's parts as the physics reads them, into t_parts.
void gatherParts(const Collider& collider) {
    t_parts.clear();
    for (const ColliderPart& part : collider.parts) t_parts.push_back(physicsPartOf(part));
}

} // namespace

Physics::Part physicsPartOf(const ColliderPart& part) {
    Physics::Part out;
    out.center      = part.center;
    out.halfExtents = part.halfExtents;
    out.radius      = part.radius;
    out.halfHeight  = part.halfHeight;
    switch (part.shape) {
        case ColliderShape::Box:     out.shape = Physics::Shape::Box;       break;
        case ColliderShape::Capsule: out.shape = Physics::Shape::Capsule;   break;
        case ColliderShape::Mesh:
        case ColliderShape::Terrain: out.shape = Physics::Shape::Triangles; break;
        case ColliderShape::Count:   VKM_ASSERT(false, "Count is no shape"); break;
    }
    return out;
}

Physics::AABB colliderLocalBounds(const Collider& collider) {
    gatherParts(collider);
    return Physics::bodyBounds(t_parts.data(), static_cast<uint32_t>(t_parts.size()), &collider.triangles);
}

Physics::MassModel massModelOf(const Collider* collider) {
    if (!collider) return {};
    gatherParts(*collider);
    const uint32_t count = static_cast<uint32_t>(t_parts.size());
    const Physics::AABB bounds = Physics::bodyBounds(t_parts.data(), count, &collider->triangles);
    return Physics::MassModel::of(t_parts.data(), count, bounds);
}

} // namespace Vkm::Engine
