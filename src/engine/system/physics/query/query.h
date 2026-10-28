#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/math/bounds.h"
#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;
struct Collider;
struct ColliderPart;

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
 * `ignore` is for the common query a body casts from inside itself. The bones of its Ragdoll are
 * ignored with it, so a character's eye inside its own head capsule sees past it.
 */
struct QueryFilter {
    EntityId ignore      = {};     ///< Never hit, nor its Ragdoll's bones; the caster, usually
    bool     hitTriggers = false;  ///< Include colliders marked isTrigger
    bool     hitStatic   = true;   ///< Include Static and Kinematic bodies, by their authored motion
    bool     hitDynamic  = true;   ///< Include Dynamic bodies, a ragdoll's posed bones among them

    /**
     * @brief Which layers may be hit, as a mask of the bits collision uses; everything by default.
     */
    int layerMask = ~0;

    /**
     * @brief The layer of the body the query stands for: a body is seen only if its collidesWith takes
     *        it, as collision asks both ways (layersCollide).
     *
     * Zero, the default, asks nothing of it.
     */
    int casterLayer = 0;
};

/**
 * @brief Cast a ray through the scene and return the nearest body it strikes.
 *
 * Sees entities with a Rigidbody and an enabled Collider, plus animation-driven ragdoll bones; a
 * Collider without a Rigidbody is in no query. Reads the scene directly, so the answer is where bodies
 * are now, at the cost of walking every body per call; a mesh's own tree is walked along the ray. Many
 * rays in one moment go through QueryParts instead.
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

/**
 * @brief Cast a ray or a swept sphere at one collider part placed in the world.
 *
 * What raycast and spherecast ask of every part they see, for a caller holding its parts already.
 *
 * @param collider The part's collider, for the points a mesh reads.
 * @param part     Part to test.
 * @param center   Its world-space centre.
 * @param basis    The body's world rotation.
 * @param origin   Sweep start, world space.
 * @param dir      Unit sweep direction.
 * @param radius   Sphere radius; zero for a ray.
 * @param limit    Furthest distance of interest.
 * @param[out] t      Distance travelled to first contact.
 * @param[out] normal Outward surface normal.
 * @return True when the part is met within @p limit.
 */
bool sweepPart(
    const Collider& collider,
    const ColliderPart& part,
    const glm::vec3& center,
    const glm::mat3& basis,
    const glm::vec3& origin,
    const glm::vec3& dir,
    float radius,
    float limit,
    float& t,
    glm::vec3& normal
);

/**
 * @brief The parts a QueryFilter lets queries see, placed once for many rays in one moment.
 *
 * raycast walks every body per call, which suits gameplay's few casts. A caller casting many at
 * once gathers the parts around where its rays run, once, and casts against those. It answers as
 * raycast would at gather(): a body moved since is struck where it was, and no Collider may be
 * added, removed or edited before the last cast, for it holds them.
 */
class QueryParts {
    public:
        QueryParts() = default;
        ~QueryParts() = default;

        QueryParts(const QueryParts& other) = default;
        QueryParts& operator=(const QueryParts& other) = default;

        QueryParts(QueryParts && other) noexcept = default;
        QueryParts& operator=(QueryParts && other) noexcept = default;

        /**
         * @brief Place every part @p filter lets through whose world bound meets @p around.
         *
         * @param scene  Scene whose bodies are asked.
         * @param filter Which bodies may be hit.
         * @param around Where the rays will run, in world space.
         */
        void gather(Scene& scene, const QueryFilter& filter, const Math::AABB& around);

        /**
         * @brief Cast a ray against the gathered parts, as raycast casts it against the scene.
         *
         * @param origin      World-space start of the ray.
         * @param direction   Any non-zero vector; normalized internally.
         * @param maxDistance How far to look, in metres; non-positive finds nothing.
         * @param[out] out    The nearest hit, ties to the lower entity slot; untouched on false.
         * @param ignore      A body the ray passes through; none by default.
         * @return True when something was struck within @p maxDistance.
         */
        bool raycast(
            const glm::vec3& origin,
            const glm::vec3& direction,
            float maxDistance,
            RayHit& out,
            EntityId ignore = {}
        ) const;

        bool empty() const { return m_parts.empty(); }

    private:
        struct Placed {
            EntityId            entity;
            const Collider*     collider = nullptr;
            const ColliderPart* part     = nullptr;
            glm::vec3           center{0.0f};
            glm::mat3           basis{1.0f};
            glm::vec3           mid{0.0f};   ///< Centre of its world bound.
            glm::vec3           half{0.0f};  ///< Half its world bound's size, contact tolerance included.
        };

    private:
        std::vector<Placed> m_parts;
};

} // namespace Vkm::Engine
