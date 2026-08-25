#include "system/physics/query/query.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "system/physics/body_pose.h"
#include "system/physics/collision/mesh_bvh.h"
#include "system/physics/collision/gjk.h"
#include "system/physics/collision/support.h"
#include "system/physics/tolerance.h"

namespace Vkm::Engine {

namespace {

// Reused across parts so a query does not allocate per shape it looks at. A
// query is one call on one thread, so these are its own rather than shared.
thread_local std::vector<glm::vec3> t_scratchPoints;
thread_local std::vector<uint32_t>  t_scratchTriangles;

/**
 * @brief Nearest intersection of a ray with a sphere, ahead of the origin.
 *
 * @param origin Ray origin, sphere-relative already applied by the caller.
 * @param dir    Unit ray direction.
 * @param center Sphere centre.
 * @param radius Sphere radius.
 * @param[out] tHit Distance to the intersection.
 * @return True when the ray meets the sphere at or ahead of its origin.
 */
bool raySphere(
    const glm::vec3& origin,
    const glm::vec3& dir,
    const glm::vec3& center,
    float radius,
    float& tHit
) {
    const glm::vec3 m = origin - center;
    const float b = glm::dot(m, dir);
    const float c = glm::dot(m, m) - radius * radius;

    // Origin inside the sphere: the body is already touching the caster.
    if (c <= 0.0f) { tHit = 0.0f; return true; }

    // Pointing away from a sphere it is outside of.
    if (b > 0.0f) return false;

    const float disc = b * b - c;
    if (disc < 0.0f) return false;

    tHit = -b - std::sqrt(disc);
    return tHit >= 0.0f;
}

/**
 * @brief Slab test against an axis-aligned box, in the box's own frame.
 *
 * An oriented box is an AABB once the ray is rotated into it, so the caller does
 * that and this stays the simple form. The entry axis is tracked as the test
 * runs, which is what makes the surface normal free rather than a second pass.
 *
 * @param origin Ray origin in the box's local frame.
 * @param dir    Unit ray direction in the box's local frame.
 * @param halfExtents Box half-sizes.
 * @param maxDistance Furthest distance of interest.
 * @param[out] tHit   Distance to the entry point, 0 when the origin is inside.
 * @param[out] localNormal Outward face normal, in the box's local frame.
 * @return True when the ray meets the box within @p maxDistance.
 */
bool rayBox(
    const glm::vec3& origin,
    const glm::vec3& dir,
    const glm::vec3& halfExtents,
    float maxDistance,
    float& tHit,
    glm::vec3& localNormal
) {
    float tMin = 0.0f;
    float tMax = maxDistance;
    int   axis = -1;
    float sign = -1.0f;

    for (int i = 0; i < 3; ++i) {
        if (std::abs(dir[i]) < glm::epsilon<float>()) {
            // Parallel to this pair of planes: a miss unless the origin already
            // lies between them, in which case the axis constrains nothing.
            if (origin[i] < -halfExtents[i]) return false;
            if (origin[i] >  halfExtents[i]) return false;
            continue;
        }

        const float inv = 1.0f / dir[i];
        float enter = (-halfExtents[i] - origin[i]) * inv;
        float exit  = ( halfExtents[i] - origin[i]) * inv;

        // Entering through the far face means the ray runs down this axis, so
        // the face it enters by is the positive one.
        float entrySign = -1.0f;
        if (enter > exit) { std::swap(enter, exit); entrySign = 1.0f; }

        if (enter > tMin) { tMin = enter; axis = i; sign = entrySign; }
        tMax = std::min(tMax, exit);
        if (tMin > tMax) return false;
    }

    tHit = tMin;
    localNormal = glm::vec3(0.0f);

    // No axis claimed the entry, so the origin is inside the box and there is no
    // face to name. The caller turns this into a normal facing the ray.
    if (axis < 0) return true;

    localNormal[axis] = sign;
    return true;
}

/**
 * @brief Nearest intersection of a ray with a capsule, in world space.
 *
 * Solves the infinite cylinder around the segment, then falls back to the cap
 * sphere at whichever end the hit ran past. A ray that misses the infinite
 * cylinder misses the capsule too, because both caps lie within radius of the
 * axis - so that test is a complete rejection, not just a first pass.
 *
 * @param origin Ray origin, world space.
 * @param dir    Unit ray direction, world space.
 * @param a      Segment start (the capsule's axis, caps excluded).
 * @param b      Segment end.
 * @param radius Sweep radius.
 * @param[out] tHit   Distance to the intersection.
 * @param[out] normal Outward surface normal, world space.
 * @return True when the ray meets the capsule at or ahead of its origin.
 */
bool rayCapsule(
    const glm::vec3& origin,
    const glm::vec3& dir,
    const glm::vec3& a,
    const glm::vec3& b,
    float radius,
    float& tHit,
    glm::vec3& normal
) {
    const glm::vec3 axis = b - a;
    const float axisLenSq = glm::dot(axis, axis);

    // A zero-length segment is a sphere, which the shape explicitly allows.
    if (axisLenSq <= glm::epsilon<float>()) {
        if (!raySphere(origin, dir, a, radius, tHit)) return false;
        const glm::vec3 hit = origin + dir * tHit;
        normal = glm::normalize(hit - a);
        return true;
    }

    const glm::vec3 m = origin - a;
    const float md = glm::dot(m, axis);
    const float nd = glm::dot(dir, axis);

    const float A = axisLenSq - nd * nd;
    const float B = axisLenSq * glm::dot(m, dir) - nd * md;
    const float radiusSq = radius * radius;
    const float C = axisLenSq * (glm::dot(m, m) - radiusSq) - md * md;

    float t = 0.0f;
    if (std::abs(A) < glm::epsilon<float>()) {
        // Running parallel to the axis: the side wall is unreachable, so the
        // answer is whichever cap the ray runs into, and only if the origin is
        // already within the radius of the axis.
        if (C > 0.0f) return false;
        t = 0.0f;
    } else {
        const float disc = B * B - A * C;
        if (disc < 0.0f) return false;
        t = (-B - std::sqrt(disc)) / A;

        // The wall was met behind the origin. That is a miss, unless the origin
        // is inside the cylinder - C is its distance to the axis, squared and
        // less the radius - in which case the caps still decide, from t = 0.
        if (t < 0.0f) {
            if (C > 0.0f) return false;
            t = 0.0f;
        }
    }

    const float projection = md + t * nd;
    if (projection < 0.0f) {
        if (!raySphere(origin, dir, a, radius, tHit)) return false;
        const glm::vec3 hit = origin + dir * tHit;
        normal = glm::normalize(hit - a);
        return true;
    }
    if (projection > axisLenSq) {
        if (!raySphere(origin, dir, b, radius, tHit)) return false;
        const glm::vec3 hit = origin + dir * tHit;
        normal = glm::normalize(hit - b);
        return true;
    }

    tHit = t;
    const glm::vec3 hit = origin + dir * t;
    const glm::vec3 onAxis = a + axis * (projection / axisLenSq);
    const glm::vec3 offAxis = hit - onAxis;
    const float offLenSq = glm::dot(offAxis, offAxis);

    // Dead on the axis, which only happens from inside: no direction is more
    // outward than any other, so the caller's fallback is the honest answer.
    if (offLenSq <= glm::epsilon<float>()) return true;

    normal = offAxis / std::sqrt(offLenSq);
    return true;
}

/**
 * @brief Sweep a sphere against an axis-aligned box, in the box's own frame.
 *
 * Solved by conservative advancement rather than by intersecting the rounded
 * box the sweep really describes. Each step measures the gap from the sphere to
 * the box and advances by exactly that: the sphere travels at unit speed, so it
 * cannot reach anything nearer than the gap, and the march therefore closes on
 * first contact from outside without ever passing through it. One routine
 * covers faces, edges and corners, where solving the shape directly needs three.
 *
 * @param origin Sweep start, box-local.
 * @param dir    Unit sweep direction, box-local.
 * @param radius Sphere radius.
 * @param halfExtents Box half-sizes.
 * @param maxDistance Furthest distance of interest.
 * @param[out] tHit   Distance travelled to first contact.
 * @param[out] localNormal Outward surface normal, box-local; zero when the
 *             sphere starts already overlapping and no direction is outward.
 * @return True when the sphere meets the box within @p maxDistance.
 */
bool sphereBox(
    const glm::vec3& origin,
    const glm::vec3& dir,
    float radius,
    const glm::vec3& halfExtents,
    float maxDistance,
    float& tHit,
    glm::vec3& localNormal
) {

    // Enough for the march to converge from any sane start. Grazing sweeps are
    // the slow case, and they run out of distance rather than of steps.
    constexpr int MAX_STEPS = 32;

    float t = 0.0f;
    for (int step = 0; step < MAX_STEPS; ++step) {
        const glm::vec3 center  = origin + dir * t;
        const glm::vec3 closest = glm::clamp(center, -halfExtents, halfExtents);
        const glm::vec3 away    = center - closest;
        const float awayLenSq   = glm::dot(away, away);

        // Centre inside the box: no direction points out of it, so the caller's
        // fallback names the normal.
        if (awayLenSq <= glm::epsilon<float>()) {
            tHit = t;
            localNormal = glm::vec3(0.0f);
            return true;
        }

        const float distance = std::sqrt(awayLenSq);
        const float gap = distance - radius;
        if (gap <= Physics::CONTACT_TOLERANCE) {
            tHit = t;
            localNormal = away / distance;
            return true;
        }

        // Nothing ahead: the sphere is outside and the sweep does not close on
        // the box at all.
        if (glm::dot(dir, -away) <= 0.0f) return false;

        t += gap;
        if (t > maxDistance) return false;
    }

    return false;
}

/**
 * @brief The point of a triangle nearest @p p, by Voronoi region.
 *
 * @param p Point to measure from.
 * @param a First corner.
 * @param b Second corner.
 * @param c Third corner.
 * @return The nearest point on the triangle, on a face, an edge or a corner.
 */
glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a,
                                 const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = p - a;

    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        return a + ab * (d1 / (d1 - d3));
    }

    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        return a + ac * (d2 / (d2 - d6));
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    }

    const float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

/**
 * @brief Sweep a sphere of @p radius against one triangle.
 *
 * Conservative advancement again, but stepping by the true distance rather than
 * by a supporting plane's. A triangle is flat, and the plane bound collapses on
 * one approached square-on: the direction from its middle turns sideways as the
 * sphere arrives, the steps shrink toward nothing, and the march runs out of
 * iterations short of a surface it was about to touch. A triangle's nearest
 * point is a closed form, so it need not be approximated at all.
 *
 * @param a First corner, world space.
 * @param b Second corner.
 * @param c Third corner.
 * @param origin Sweep start.
 * @param dir Unit sweep direction.
 * @param radius Sphere radius; zero for a ray.
 * @param maxDistance Furthest distance of interest.
 * @param[out] tHit Distance travelled to first contact.
 * @param[out] normal Outward surface normal.
 * @return True when the sphere meets the triangle within @p maxDistance.
 */
bool sweepTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                   const glm::vec3& origin, const glm::vec3& dir,
                   float radius, float maxDistance,
                   float& tHit, glm::vec3& normal) {
    constexpr int MAX_STEPS = 32;

    float t = 0.0f;
    for (int step = 0; step < MAX_STEPS; ++step) {
        const glm::vec3 center = origin + dir * t;
        const glm::vec3 nearest = closestPointOnTriangle(center, a, b, c);
        const glm::vec3 away = center - nearest;
        const float awayLenSq = glm::dot(away, away);

        if (awayLenSq <= glm::epsilon<float>()) {
            tHit = t;
            normal = glm::vec3(0.0f);
            return true;
        }

        const float distance = std::sqrt(awayLenSq);
        const float gap = distance - radius;
        if (gap <= Physics::CONTACT_TOLERANCE) {
            tHit = t;
            normal = away / distance;
            return true;
        }

        if (glm::dot(dir, -away) <= 0.0f) return false;
        t += gap;
        if (t > maxDistance) return false;
    }
    return false;
}

/**
 * @brief Cast a ray or a swept sphere at one collider part.
 *
 * The single place a query decides what a shape is. Written as a switch with no
 * default so that adding a fifth shape does not compile until this is one of
 * the places that answered for it - the previous form treated everything that
 * was not a box as a capsule, which silently gave a hull and a mesh a phantom
 * capsule of whatever radius the part happened to carry.
 *
 * @param collider The part's collider, for the points a hull or mesh reads.
 * @param part Part to test.
 * @param center Its world-space centre.
 * @param basis The body's world rotation.
 * @param origin Sweep start, world space.
 * @param dir Unit sweep direction.
 * @param radius Sphere radius; zero for a ray.
 * @param limit Furthest distance of interest.
 * @param[out] t Distance travelled to first contact.
 * @param[out] normal Outward surface normal.
 * @return True when the part is met within @p limit.
 */
bool castPart(const Collider& collider, const ColliderPart& part,
              const glm::vec3& center, const glm::mat3& basis,
              const glm::vec3& origin, const glm::vec3& dir,
              float radius, float limit, float& t, glm::vec3& normal) {
    switch (part.shape) {
        case ColliderShape::Box: {
            // The box frame is the body's, so the sweep moves into it rather
            // than the eight corners moving out.
            const glm::mat3 toLocal = glm::transpose(basis);
            const glm::vec3 localOrigin = toLocal * (origin - center);
            const glm::vec3 localDir = toLocal * dir;
            glm::vec3 localNormal(0.0f);

            const bool hit = radius > 0.0f
                ? sphereBox(localOrigin, localDir, radius, part.halfExtents,
                            limit, t, localNormal)
                : rayBox(localOrigin, localDir, part.halfExtents, limit, t,
                         localNormal);
            if (hit) normal = basis * localNormal;
            return hit;
        }

        case ColliderShape::Capsule: {
            // The segment runs along the body's local +Y, which is the basis'
            // second column already in world space. Sweeping a sphere against a
            // capsule is a ray against the same capsule grown by the sphere's
            // radius, so both cases are one call.
            const glm::vec3 up = basis[1] * part.halfHeight;
            return rayCapsule(origin, dir, center - up, center + up,
                              part.radius + radius, t, normal);
        }

        case ColliderShape::Mesh: {
            const uint32_t last = part.meshFirst + part.meshCount;
            if (part.meshCount < 3 || last > collider.meshPoints.size()) {
                return false;
            }

            // The tree is in the body's frame, so the sweep's bound goes into
            // that frame rather than every triangle coming out of it.
            const glm::mat3 toLocal = glm::transpose(basis);
            const glm::vec3 localOrigin = toLocal * (origin - center);
            const glm::vec3 localDir = toLocal * dir;
            const glm::vec3 localEnd = localOrigin + localDir * limit;
            const glm::vec3 grow(radius + Physics::CONTACT_TOLERANCE);

            t_scratchTriangles.clear();
            queryMeshBvh(collider.meshNodes,
                         glm::min(localOrigin, localEnd) - grow,
                         glm::max(localOrigin, localEnd) + grow,
                         t_scratchTriangles);

            bool found = false;
            float nearest = limit;
            for (uint32_t triangle : t_scratchTriangles) {
                const uint32_t base = part.meshFirst + triangle * 3;
                if (base + 2 >= collider.meshPoints.size()) continue;

                glm::vec3 face[3];
                for (int c = 0; c < 3; ++c) {
                    face[c] = center + basis * collider.meshPoints[base + c];
                }

                float hitT = 0.0f;
                glm::vec3 hitNormal(0.0f);
                if (!sweepTriangle(face[0], face[1], face[2], origin, dir,
                                   radius, nearest, hitT, hitNormal)) {
                    continue;
                }
                if (found && hitT >= nearest) continue;
                nearest = hitT;
                t = hitT;
                normal = hitNormal;
                found = true;
            }
            return found;
        }

        case ColliderShape::Count:
            break;
    }
    return false;
}

// Whether this body passes the filter's static / dynamic split. Kinematic
// counts as static: it is what the solver treats as immovable, and a caller
// asking for "the level" means the things that do not fall.
bool passesMobility(const Rigidbody& rb, const QueryFilter& filter) {
    if ((rb.layer & filter.layerMask) == 0) return false;
    const bool immovable = rb.isStatic || rb.isKinematic || rb.mass <= 0.0f;
    return immovable ? filter.hitStatic : filter.hitDynamic;
}

template <typename PartTest>
bool sweepBodies(
    Scene& scene,
    const glm::vec3& origin,
    const glm::vec3& dir,
    float maxDistance,
    const QueryFilter& filter,
    RayHit& out,
    PartTest testPart
) {
    auto* rbStorage = scene.storage<Rigidbody>();
    if (!rbStorage) return false;

    RayHit best;
    float bestDistance = maxDistance;
    bool found = false;

    const uint32_t count = static_cast<uint32_t>(rbStorage->size());
    for (uint32_t i = 0; i < count; ++i) {
        const EntityId id = scene.entityAt(rbStorage->keyAt(i));
        if (id == filter.ignore) continue;
        if (!scene.has<Transform>(id) || !scene.has<Collider>(id)) continue;

        const Collider& collider = scene.get<Collider>(id);
        if (!collider.enabled) continue;
        if (collider.isTrigger && !filter.hitTriggers) continue;
        if (!passesMobility(rbStorage->dataAt(i), filter)) continue;

        const BodyPose pose = worldPoseOf(scene, id, scene.get<Transform>(id));
        const glm::mat3 basis = glm::mat3_cast(pose.rotation);

        for (const ColliderPart& part : collider.parts) {
            const glm::vec3 center = pose.position + basis * part.center;

            float     hitDistance = 0.0f;
            glm::vec3 hitNormal   = {0.0f, 1.0f, 0.0f};
            const bool hit = testPart(collider, part, center, basis,
                                      bestDistance, hitDistance, hitNormal);
            if (!hit) continue;
            if (hitDistance > bestDistance) continue;

            // Nothing named a surface: the query started inside the shape, or
            // ended on its axis. Facing the caster is the one answer that lets
            // it push back out the way it came in.
            if (glm::dot(hitNormal, hitNormal) <= glm::epsilon<float>()) {
                hitNormal = -dir;
            }

            bestDistance  = hitDistance;
            best.entity   = id;
            best.distance = hitDistance;
            best.normal   = hitNormal;
            best.point    = origin + dir * hitDistance;
            found = true;
        }
    }

    if (found) out = best;
    return found;
}

// Both entry points take a direction of any length and a distance that may be
// nonsense, so they share the same front door: reject what cannot be cast, and
// hand on a unit direction.
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

    return sweepBodies(scene, origin, dir, maxDistance, filter, out,
        [&](const Collider& collider, const ColliderPart& part,
            const glm::vec3& center, const glm::mat3& basis, float limit,
            float& t, glm::vec3& normal) {
            return castPart(collider, part, center, basis, origin, dir, 0.0f,
                            limit, t, normal);
        });
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

    // A sweep of no thickness is a ray, and answering it here would be a second
    // implementation of one that disagrees with it at the edges.
    if (radius <= 0.0f) {
        return raycast(scene, origin, direction, maxDistance, out, filter);
    }

    glm::vec3 dir(0.0f);
    if (!prepareSweep(direction, maxDistance, dir)) return false;

    const bool found = sweepBodies(scene, origin, dir, maxDistance, filter, out,
        [&](const Collider& collider, const ColliderPart& part,
            const glm::vec3& center, const glm::mat3& basis, float limit,
            float& t, glm::vec3& normal) {
            return castPart(collider, part, center, basis, origin, dir, radius,
                            limit, t, normal);
        });

    // sweepBodies reports where the sphere's centre stopped. The surface it
    // stopped against is one radius further along the normal.
    if (found) out.point -= out.normal * radius;
    return found;
}

} // namespace Vkm::Engine
