#pragma once

#include "core/math/bounds.h"
#include "ecs/component/physics/collider.h"

namespace Vkm::Engine {

/**
 * @brief One part's bound in its own frame, about its centre; a mesh's is its tree's root.
 *
 * @param collider The collider holding the part, whose tree a mesh part reads.
 * @param part     One of @p collider's parts.
 * @return The bound, over the same points the narrowphase and queries place about the centre.
 */
inline Math::AABB partLocalBounds(const Collider& collider, const ColliderPart& part) {
    switch (part.shape) {
        case ColliderShape::Box:
            return {-part.halfExtents, part.halfExtents};

        case ColliderShape::Capsule: {
            const glm::vec3 reach(part.radius, part.halfHeight + part.radius, part.radius);
            return {-reach, reach};
        }

        case ColliderShape::Mesh:
        case ColliderShape::Terrain:
            if (collider.meshNodes.empty()) return {glm::vec3(0.0f), glm::vec3(0.0f)};
            return {collider.meshNodes[0].min, collider.meshNodes[0].max};

        case ColliderShape::Count:
            break;
    }
    return {-part.halfExtents, part.halfExtents};
}

/**
 * @brief Every part's bound in the body's own frame, together.
 *
 * @param collider Collider whose parts are bounded.
 * @return The bound; a point at the origin with no parts.
 */
inline Math::AABB colliderLocalBounds(const Collider& collider) {
    if (collider.parts.empty()) return {glm::vec3(0.0f), glm::vec3(0.0f)};

    Math::AABB out = Math::AABB::empty();
    for (const ColliderPart& part : collider.parts) {
        const Math::AABB local = partLocalBounds(collider, part);
        out.expand(Math::AABB{part.center + local.min, part.center + local.max});
    }
    return out;
}

} // namespace Vkm::Engine
