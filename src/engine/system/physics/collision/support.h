#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "system/physics/collision/narrowphase.h"

namespace Vkm::Engine {

/**
 * @brief A convex shape reduced to the one question GJK asks of it.
 *
 * "Which of your points lies furthest along this direction." Every convex shape
 * answers it, and answering it is all GJK ever needs - so one intersection
 * routine serves every pair of shapes that can, rather than a routine per pair.
 * That is what stops the narrowphase growing as the square of the shape count
 * as shapes are added - mesh triangles being the one that arrived.
 *
 * The primitives keep their hand-written pair routines regardless. Those return
 * up to four contact points and this returns one, and a box resting on a box
 * needs the manifold to stay still. See docs/guides/engine.md, section 4.
 */
struct SupportShape {
    enum class Kind : uint8_t {
        Box,      ///< Reads centre + axes + halfExtents
        Capsule,  ///< Reads a + b + radius; a == b is a sphere
        Points    ///< Reads points + count, already in world space
    };

    Kind kind = Kind::Box;

    glm::vec3 center  = {0.0f, 0.0f, 0.0f};
    glm::vec3 axes[3] = {{1.0f, 0.0f, 0.0f},
                         {0.0f, 1.0f, 0.0f},
                         {0.0f, 0.0f, 1.0f}};
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
 * @return The same box, as the only thing GJK reads.
 */
SupportShape supportOf(const BoxShape& box);

/**
 * @brief Build a support view of a capsule.
 *
 * @param capsule Capsule in world space.
 * @return The same capsule, as the only thing GJK reads.
 */
SupportShape supportOf(const CapsuleShape& capsule);

/**
 * @brief Build a support view of a point cloud, which is its convex hull.
 *
 * The hull of the points is what GJK sees whether or not the caller computed
 * one: a support query returns an extreme point, and every extreme point of a
 * set is a vertex of its hull. Its one caller is the mesh narrowphase, handing
 * over a triangle.
 *
 * @param points World-space points; the span must outlive the shape.
 * @param count  How many.
 * @return The point set, as the only thing GJK reads.
 */
SupportShape supportOfPoints(const glm::vec3* points, uint32_t count);

/**
 * @brief The furthest point of @p shape along @p direction.
 *
 * @param shape Shape to query.
 * @param direction Search direction; need not be normalized.
 * @return The extreme point, in world space.
 */
glm::vec3 support(const SupportShape& shape, const glm::vec3& direction);

} // namespace Vkm::Engine
