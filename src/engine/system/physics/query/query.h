#pragma once

#include <glm/glm.hpp>

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief What a query struck: which entity, where, and the surface it hit.
 *
 * `normal` points out of the surface, back along the ray. A ray starting inside a body hits it at
 * distance zero; where the shape gives no outward direction (inside a box) the normal points at the caster.
 */
struct RayHit {
    EntityId  entity   = {};                  ///< The body that was struck
    float     distance = 0.0f;                ///< Along the ray, in metres
    glm::vec3 point    = {0.0f, 0.0f, 0.0f};  ///< World-space contact point
    glm::vec3 normal   = {0.0f, 1.0f, 0.0f};  ///< Outward surface normal
};

/**
 * @brief Which bodies a query is allowed to see.
 *
 * `ignore` is for the common query a body casts from inside itself.
 */
struct QueryFilter {
    EntityId ignore      = {};     ///< Never hit; the caster, usually
    bool     hitTriggers = false;  ///< Include colliders marked isTrigger
    bool     hitStatic   = true;   ///< Include Static and Kinematic bodies, by their authored motion
    bool     hitDynamic  = true;   ///< Include Dynamic bodies, a ragdoll's posed bones among them

    /**
     * @brief Which layers may be hit, as a mask of the bits collision uses; everything by default.
     */
    int layerMask = ~0;
};

/**
 * @brief Cast a ray through the scene and return the nearest body it strikes.
 *
 * Sees entities with a Rigidbody and an enabled Collider, plus animation-driven ragdoll bones; a
 * Collider without a Rigidbody is in no query. Reads the scene directly, so the answer is where bodies
 * are now, at the cost of walking every body per call; a mesh's own tree is walked along the ray.
 *
 * @param scene       Scene whose bodies are asked.
 * @param origin      World-space start of the ray.
 * @param direction   Any non-zero vector; normalized internally.
 * @param maxDistance How far to look, in metres; non-positive finds nothing.
 * @param[out] out    The nearest hit, ties to the lower entity slot so every end agrees; untouched on false.
 * @param filter      Which bodies may be hit.
 * @return True when something was struck within @p maxDistance.
 */
bool raycast(
    Scene& scene,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance,
    RayHit& out,
    const QueryFilter& filter = {}
);

/**
 * @brief Sweep a sphere through the scene and stop it at the first body it meets.
 *
 * The stopped centre is `origin + normalize(direction) * distance`. Sees what raycast sees, at the same
 * cost.
 *
 * @param scene       Scene whose bodies are asked.
 * @param origin      World-space centre the sphere starts at.
 * @param radius      Sphere radius; zero or less defers to raycast.
 * @param direction   Normalized internally.
 * @param maxDistance How far the centre may travel, in metres.
 * @param[out] out    The nearest hit, ties broken as raycast does; untouched on false.
 * @param filter      Which bodies may be hit.
 * @return True when the sphere meets something within @p maxDistance.
 */
bool spherecast(
    Scene& scene,
    const glm::vec3& origin,
    float radius,
    const glm::vec3& direction,
    float maxDistance,
    RayHit& out,
    const QueryFilter& filter = {}
);

} // namespace Vkm::Engine
