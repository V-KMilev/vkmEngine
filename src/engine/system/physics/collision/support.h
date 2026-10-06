#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "system/physics/collision/narrowphase.h"

namespace Vkm::Engine {

/**
 * @brief A convex shape reduced to the one question GJK asks: its furthest point along a direction.
 *
 * The primitives keep their hand-written pair routines, which return up to four contact points (a box
 * resting on a box needs them); GJK answers overlap alone. See docs/guides/engine.md, section 4.
 */
struct SupportShape {
    enum class Kind : uint8_t {
        Box,      ///< Reads centre + axes + halfExtents
        Capsule,  ///< Reads a + b + radius; a == b is a sphere
        Points    ///< Reads points + count, already in world space
    };

    Kind kind = Kind::Box;

    glm::vec3 center      = {0.0f, 0.0f, 0.0f};
    glm::vec3 axes[3]     = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    glm::vec3 halfExtents = {0.5f, 0.5f, 0.5f};

    glm::vec3 a = {0.0f, 0.0f, 0.0f};
    glm::vec3 b = {0.0f, 0.0f, 0.0f};
    float radius = 0.0f;

    const glm::vec3* points = nullptr;
    uint32_t count = 0;
};

/**
 * @brief Build a support view of an oriented box.
 *
 * @param box Box in world space.
 * @return The box, as GJK reads it.
 */
SupportShape supportOf(const BoxShape& box);

/**
 * @brief Build a support view of a capsule.
 *
 * @param capsule Capsule in world space.
 * @return The capsule, as GJK reads it.
 */
SupportShape supportOf(const CapsuleShape& capsule);

/**
 * @brief Build a support view of a point cloud, which is its convex hull.
 *
 * No hull need be computed: every extreme point of a set is a vertex of its hull.
 *
 * @param points World-space points; must outlive the shape.
 * @param count  How many.
 * @return The point set, as GJK reads it.
 */
SupportShape supportOfPoints(const glm::vec3* points, uint32_t count);

/**
 * @brief The furthest point of @p shape along @p direction.
 *
 * @param shape Shape to query.
 * @param direction Need not be normalized.
 * @return The extreme point, world space.
 */
glm::vec3 support(const SupportShape& shape, const glm::vec3& direction);

} // namespace Vkm::Engine
