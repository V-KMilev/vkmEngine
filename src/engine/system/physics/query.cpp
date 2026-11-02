#include "system/physics/query.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vkm_physics/body.h"
#include "vkm_physics/sweep.h"
#include "vkm_physics/tolerance.h"

#include "base/math/aabb.h"
#include "base/profile/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "core/frame/body_pose.h"

namespace Vkm::Engine {

namespace {

bool passesMobility(const Rigidbody& rb, const QueryFilter& filter) {
    return rb.motion == RigidbodyMotion::Dynamic ? filter.hitDynamic : filter.hitStatic;
}

// filter.ignore, or a bone of its Ragdoll (QueryFilter).
bool isIgnored(EntityId id, const QueryFilter& filter, const Ragdoll* ignoredRagdoll) {
    if (id == filter.ignore) return true;
    if (!ignoredRagdoll) return false;
    for (const RagdollBone& bone : ignoredRagdoll->bones) {
        if (bone.body == id) return true;
    }
    return false;
}

// How far from its origin any part of @p collider reaches, however the body is turned.
float reachOf(const Collider& collider) {
    float reach = 0.0f;
    for (const ColliderPart& part : collider.parts) {
        const Physics::AABB local = Physics::partBounds(physicsPartOf(part), &collider.triangles);
        const glm::vec3 low  = glm::abs(part.center + local.min);
        const glm::vec3 high = glm::abs(part.center + local.max);
        reach = std::max(reach, glm::length(glm::max(low, high)));
    }
    return reach;
}

/**
 * @brief Walk every part @p filter lets through, placed in the world, of each body @p within keeps.
 *
 * @tparam Within Any callable of the shape @p within names.
 * @tparam Fn     Any callable of the shape @p fn names.
 * @param scene  Scene whose bodies are walked.
 * @param filter Which bodies may be hit.
 * @param within Called as within(origin, reach) with a sphere holding every part of a body; false
 *               passes the body over before its parts are placed.
 * @param fn     Called as fn(entity, part, triangles, center, basis), the part as the physics reads it.
 */
template <typename Within, typename Fn>
void forEachPart(Scene& scene, const QueryFilter& filter, Within&& within, Fn&& fn) {
    const SparseSet<Rigidbody>* rbStorage  = scene.storage<Rigidbody>();
    const SparseSet<Transform>* transforms = scene.storage<Transform>();
    const SparseSet<Collider>*  colliders  = scene.storage<Collider>();
    if (!rbStorage || !transforms || !colliders) return;
    const SparseSet<Hierarchy>* links = scene.storage<Hierarchy>();

    const Ragdoll* ignoredRagdoll = filter.ignore ? scene.tryGet<Ragdoll>(filter.ignore) : nullptr;
    const uint32_t count = static_cast<uint32_t>(rbStorage->size());
    for (uint32_t i = 0; i < count; ++i) {
        // The body's own fields first: they reject most bodies without a lookup.
        const Rigidbody& rb = rbStorage->dataAt(i);
        // As collision asks it, the query standing for a body on casterLayer that takes layerMask.
        const bool seen = filter.casterLayer == 0
            ? (rb.layer & filter.layerMask) != 0
            : Physics::layersCollide(filter.casterLayer, filter.layerMask, rb.layer, rb.collidesWith);
        if (!seen) continue;
        if (!passesMobility(rb, filter)) continue;

        const uint32_t key = rbStorage->keyAt(i);
        if (!transforms->contains(key) || !colliders->contains(key)) continue;
        const Collider& collider = colliders->get(key);
        if (!collider.enabled) continue;
        if (collider.isTrigger && !filter.hitTriggers) continue;

        const EntityId id = scene.entityAt(key);
        if (isIgnored(id, filter, ignoredRagdoll)) continue;

        // A body with no parent is where its Transform says: the walk skips building its pose.
        const Transform& transform = transforms->get(key);
        glm::vec3 position = transform.position;
        glm::quat rotation = transform.rotation;
        if (links && links->contains(key) && links->get(key).parent) {
            const BodyPose pose = worldPoseOf(scene, id, transform);
            position = pose.position;
            rotation = pose.rotation;
        }
        if (!within(position, reachOf(collider))) continue;
        const glm::mat3 basis = glm::mat3_cast(rotation);
        for (const ColliderPart& part : collider.parts) {
            fn(id, physicsPartOf(part), &collider.triangles, position + basis * part.center, basis);
        }
    }
}

// The nearest hit offered along one ray. A tie goes to the lower slot, so every end names the same body.
struct Nearest {
    glm::vec3 origin;
    glm::vec3 dir;
    float     limit;
    RayHit    best{};
    bool      found = false;

    void offer(EntityId id, float distance, glm::vec3 normal) {
        if (distance > limit) return;
        if (found && distance == limit && id.slot() > best.entity.slot()) return;
        // No surface named (started inside, or on an axis): facing the caster lets it push back out.
        if (glm::dot(normal, normal) <= glm::epsilon<float>()) normal = -dir;
        limit = distance;
        best  = {id, distance, origin + dir * distance, normal};
        found = true;
    }
};

template <typename PartTest>
bool sweepBodies(
    Scene& scene,
    const glm::vec3& origin,
    const glm::vec3& dir,
    float maxDistance,
    float radius,
    const QueryFilter& filter,
    RayHit& out,
    PartTest testPart
) {
    Nearest nearest{origin, dir, maxDistance};
    // Whether a body's sphere comes within the sweep's radius of the ray, as far as the nearest hit.
    const auto within = [&](const glm::vec3& at, float reach) {
        const float along   = std::clamp(glm::dot(at - origin, dir), 0.0f, nearest.limit);
        const glm::vec3 off = at - (origin + dir * along);
        const float outside = reach + radius + Physics::CONTACT_TOLERANCE;
        return glm::dot(off, off) <= outside * outside;
    };
    const auto test = [&](
        EntityId id,
        const Physics::Part& part,
        const Physics::TriangleMesh* triangles,
        const glm::vec3& center,
        const glm::mat3& basis
    ) {
        float t = 0.0f;
        glm::vec3 normal(0.0f, 1.0f, 0.0f);
        if (testPart(part, triangles, center, basis, nearest.limit, t, normal)) nearest.offer(id, t, normal);
    };
    forEachPart(scene, filter, within, test);
    if (nearest.found) out = nearest.best;
    return nearest.found;
}

bool prepareSweep(const glm::vec3& direction, float maxDistance, glm::vec3& dir) {
    if (maxDistance <= 0.0f) return false;
    const float lengthSq = glm::dot(direction, direction);
    if (lengthSq <= glm::epsilon<float>()) return false;
    dir = direction / std::sqrt(lengthSq);
    return true;
}

} // namespace

bool raycast(
    Scene& scene,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance,
    RayHit& out,
    const QueryFilter& filter
) {
    PROFILE_SCOPE("Physics/Raycast");

    glm::vec3 dir(0.0f);
    if (!prepareSweep(direction, maxDistance, dir)) return false;

    const auto castRay = [&](
        const Physics::Part& part,
        const Physics::TriangleMesh* triangles,
        const glm::vec3& center,
        const glm::mat3& basis,
        float limit,
        float& t,
        glm::vec3& normal
    ) {
        return Physics::sweepPart(part, triangles, center, basis, origin, dir, 0.0f, limit, t, normal);
    };
    const float ray = 0.0f;
    return sweepBodies(scene, origin, dir, maxDistance, ray, filter, out, castRay);
}

bool spherecast(
    Scene& scene,
    const glm::vec3& origin,
    float radius,
    const glm::vec3& direction,
    float maxDistance,
    RayHit& out,
    const QueryFilter& filter
) {
    PROFILE_SCOPE("Physics/Spherecast");

    // A ray; a second implementation here would disagree with it at the edges.
    if (radius <= 0.0f) {
        return raycast(scene, origin, direction, maxDistance, out, filter);
    }

    glm::vec3 dir(0.0f);
    if (!prepareSweep(direction, maxDistance, dir)) return false;

    const auto castSphere = [&](
        const Physics::Part& part,
        const Physics::TriangleMesh* triangles,
        const glm::vec3& center,
        const glm::mat3& basis,
        float limit,
        float& t,
        glm::vec3& normal
    ) {
        return Physics::sweepPart(part, triangles, center, basis, origin, dir, radius, limit, t, normal);
    };
    const bool found = sweepBodies(scene, origin, dir, maxDistance, radius, filter, out, castSphere);

    // sweepBodies reports the center; the surface is one radius further.
    if (found) out.point -= out.normal * radius;
    return found;
}

void overlapSphere(
    Scene& scene,
    const glm::vec3& center,
    float radius,
    std::vector<EntityId>& found,
    const QueryFilter& filter
) {
    PROFILE_SCOPE("Physics/OverlapSphere");
    found.clear();
    if (!(radius > 0.0f)) return;

    const auto within = [&](const glm::vec3& at, float reach) {
        const float outside = reach + radius + Physics::CONTACT_TOLERANCE;
        return glm::dot(at - center, at - center) <= outside * outside;
    };
    // A sweep of no length: whether the sphere where it stands meets the part. A body's parts come
    // together, so it is listed once.
    const glm::vec3 still(0.0f, 1.0f, 0.0f);
    const auto test = [&](
        EntityId id,
        const Physics::Part& part,
        const Physics::TriangleMesh* triangles,
        const glm::vec3& at,
        const glm::mat3& basis
    ) {
        if (!found.empty() && found.back() == id) return;
        float t = 0.0f;
        glm::vec3 normal(0.0f);
        if (Physics::sweepPart(part, triangles, at, basis, center, still, radius, 0.0f, t, normal)) {
            found.push_back(id);
        }
    };
    forEachPart(scene, filter, within, test);
    std::sort(found.begin(), found.end(), [](EntityId a, EntityId b) { return a.slot() < b.slot(); });
}

void QueryParts::gather(Scene& scene, const QueryFilter& filter, const Math::AABB& around) {
    m_parts.clear();
    const auto place = [&](
        EntityId id,
        const Physics::Part& part,
        const Physics::TriangleMesh* triangles,
        const glm::vec3& center,
        const glm::mat3& basis
    ) {
        // The part's own bound turned into the world, grown by what a mesh's walk tolerates.
        const Physics::AABB placed = Physics::transform(basis, center, Physics::partBounds(part, triangles));
        const glm::vec3 mid  = placed.center();
        const glm::vec3 half = placed.halfExtent() + glm::vec3(Physics::CONTACT_TOLERANCE);
        const bool meets = glm::all(glm::lessThanEqual(mid - half, around.max))
            && glm::all(glm::greaterThanEqual(mid + half, around.min));
        if (meets) m_parts.push_back({id, part, triangles, center, basis, mid, half});
    };
    // Whether a body's sphere meets the box, grown by what a mesh's walk tolerates.
    const auto within = [&](const glm::vec3& at, float reach) {
        const glm::vec3 closest = glm::clamp(at, around.min, around.max);
        const float     outside = reach + Physics::CONTACT_TOLERANCE;
        return glm::dot(at - closest, at - closest) <= outside * outside;
    };
    forEachPart(scene, filter, within, place);
}

bool QueryParts::raycast(
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance,
    RayHit& out,
    EntityId ignore
) const {
    glm::vec3 dir(0.0f);
    if (!prepareSweep(direction, maxDistance, dir)) return false;

    Nearest         nearest{origin, dir, maxDistance};
    const glm::vec3 inverse = 1.0f / dir;
    for (const Placed& placed : m_parts) {
        if (ignore && placed.entity == ignore) continue;
        // Its bound first: most gathered parts lie off any one ray.
        const glm::vec3 low  = placed.mid - placed.half;
        const glm::vec3 high = placed.mid + placed.half;
        if (!Physics::rayMeetsBox(origin, inverse, low, high, nearest.limit)) continue;

        float t = 0.0f;
        glm::vec3 normal(0.0f);
        const bool hit = Physics::sweepPart(
            placed.part,
            placed.triangles,
            placed.center,
            placed.basis,
            origin,
            dir,
            0.0f,
            nearest.limit,
            t,
            normal
        );
        if (hit) nearest.offer(placed.entity, t, normal);
    }
    if (nearest.found) out = nearest.best;
    return nearest.found;
}

} // namespace Vkm::Engine
