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

} // namespace Vkm::Engine
