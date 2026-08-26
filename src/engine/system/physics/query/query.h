#pragma once

#include <glm/glm.hpp>

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief What a query struck: which entity, where, and the surface it hit.
 *
 * `normal` points back along the incoming ray, out of the surface, so a caller
 * can slide, reflect or step against it without checking which side it started
 * on. A ray that begins already inside a body reports it at distance zero, with
 * the normal pointing back at the caster.
 */
struct RayHit {
    EntityId  entity   = {};                      ///< The body that was struck
    float     distance = 0.0f;                    ///< Along the ray, in metres
    glm::vec3 point    = {0.0f, 0.0f, 0.0f};      ///< World-space contact point
    glm::vec3 normal   = {0.0f, 1.0f, 0.0f};      ///< Outward surface normal
};

/**
 * @brief Which bodies a query is allowed to see.
 *
 * The defaults are what a gameplay caster wants: solid bodies only, triggers
 * left alone. `ignore` exists because the overwhelmingly common query is one a
 * body casts from inside itself, and a hit on the caster is never the answer.
 */
struct QueryFilter {
    EntityId ignore      = {};      ///< Never hit; the caster, usually
    bool     hitTriggers = false;   ///< Include colliders marked isTrigger
    bool     hitStatic   = true;    ///< Include static and kinematic bodies
    bool     hitDynamic  = true;    ///< Include dynamic bodies

    /**
     * @brief Which layers may be hit, as a mask of their bits.
     *
     * The same layers collision uses, so a query can be asked in the terms the
     * world is already organised by - "the level, not the characters" is a mask
     * rather than a list of entities to ignore. Everything by default.
     */
    int layerMask = ~0;
};

/**
 * @brief Cast a ray through the scene and return the nearest body it strikes.
 *
 * Sees exactly what the simulation sees: an entity with both a Rigidbody and an
 * enabled Collider. A Collider without a Rigidbody is in no broadphase, and is
 * in no query either - one definition of the physics world, so a ray can never
 * report a wall that a character would walk through.
 *
 * Queries read the scene directly rather than the tick's cached proxies, so the
 * answer describes where bodies are at the moment of the call rather than where
 * they were when physics last ran. That costs a walk of the scene's bodies per
 * call: there is no acceleration structure behind this yet, and the first shape
 * that needs one is the triangle mesh.
 *
 * @param scene       Scene whose bodies are asked.
 * @param origin      World-space start of the ray.
 * @param direction   Direction to cast; normalized internally, so callers may
 *                    pass any non-zero vector.
 * @param maxDistance How far to look, in metres. Non-positive finds nothing.
 * @param[out] out    The nearest hit; untouched when the call returns false.
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
 * The question a character asks that a ray cannot answer. A ray reports whether
 * something is in the way; a sweep reports where a body of a given size would
 * come to rest, which is what deciding a step-up, a ledge grab or a spawn point
 * actually needs. `distance` is how far the sphere travelled, `point` is where
 * it touched, and `normal` is the surface it touched - so the stopped centre is
 * `origin + normalize(direction) * distance`.
 *
 * Sees the same bodies as raycast, under the same filter, with the same caveat
 * about there being no acceleration structure yet. A radius of zero or less is
 * answered by raycast itself rather than by a second implementation of it.
 *
 * @param scene       Scene whose bodies are asked.
 * @param origin      World-space centre the sphere starts at.
 * @param radius      Sphere radius. Zero or less defers to raycast.
 * @param direction   Direction to sweep; normalized internally.
 * @param maxDistance How far the centre may travel, in metres.
 * @param[out] out    The nearest hit; untouched when the call returns false.
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
