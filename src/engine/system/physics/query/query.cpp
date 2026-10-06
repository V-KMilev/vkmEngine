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
#include "system/physics/collision/triangle.h"
#include "system/physics/tolerance.h"

namespace Vkm::Engine {

namespace {

// Reused so a query does not allocate per shape; a query runs on one thread.
thread_local std::vector<uint32_t> t_scratchTriangles;

/**
 * @brief Nearest intersection of a ray with a sphere, ahead of the origin.
 *
 * @param origin Ray origin, in the frame of @p center.
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

    // Origin inside: already touching the caster.
    if (c <= 0.0f) {
        tHit = 0.0f;
        return true;
    }

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
 * The caller rotates the ray into an oriented box's frame.
 *
 * @param origin Ray origin, box-local.
 * @param dir    Unit ray direction, box-local.
 * @param halfExtents Box half-sizes.
 * @param maxDistance Furthest distance of interest.
 * @param[out] tHit   Distance to the entry point, 0 when the origin is inside.
 * @param[out] localNormal Outward face normal, box-local.
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
            // Parallel: a miss unless the origin lies between the planes, where the axis constrains nothing.
            if (origin[i] < -halfExtents[i]) return false;
            if (origin[i] >  halfExtents[i]) return false;
            continue;
        }

        const float inv = 1.0f / dir[i];
        float enter = (-halfExtents[i] - origin[i]) * inv;
        float exit  = ( halfExtents[i] - origin[i]) * inv;

        // Swapped means the ray runs down this axis, entering by the positive face.
        float entrySign = -1.0f;
        if (enter > exit) {
            std::swap(enter, exit);
            entrySign = 1.0f;
        }

        if (enter > tMin) {
            tMin = enter;
            axis = i;
            sign = entrySign;
        }
        tMax = std::min(tMax, exit);
        if (tMin > tMax) return false;
    }

    tHit = tMin;
    localNormal = glm::vec3(0.0f);

    // The origin is inside, with no face to name; the caller makes a normal facing the ray.
    if (axis < 0) return true;

    localNormal[axis] = sign;
    return true;
}

/**
 * @brief Nearest intersection of a ray with a capsule, in world space.
 *
 * The infinite cylinder first, then the cap sphere at whichever end the hit ran past. Missing the
 * cylinder misses the capsule, as both caps lie within radius of the axis.
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

    // A zero-length segment is a sphere.
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
        // Parallel to the axis: only a cap can be met, and only from within the radius.
        if (C > 0.0f) return false;
        t = 0.0f;
    } else {
        const float disc = B * B - A * C;
        if (disc < 0.0f) return false;
        t = (-B - std::sqrt(disc)) / A;

        // The wall is behind the origin: a miss, unless the origin is inside the cylinder (C <= 0), where
        // the caps decide from t = 0.
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

    // Dead on the axis, from inside: no direction is outward, so the caller's fallback answers.
    if (offLenSq <= glm::epsilon<float>()) return true;

    normal = offAxis / std::sqrt(offLenSq);
    return true;
}

/**
 * @brief Sweep a sphere against an axis-aligned box, in the box's own frame.
 *
 * Exact: a ray against the box grown by the radius with rounded edges and corners. Where it enters the
 * grown box decides: beyond one face, that face; two, that edge's capsule; three, the nearest of the
 * corner's three edge capsules. (Ericson, Real-Time Collision Detection, 5.5.7.)
 *
 * @param origin Sweep start, box-local.
 * @param dir    Unit sweep direction, box-local.
 * @param radius Sphere radius.
 * @param halfExtents Box half-sizes.
 * @param maxDistance Furthest distance of interest.
 * @param[out] tHit   Distance travelled to first contact.
 * @param[out] localNormal Outward surface normal, box-local; zero when the centre starts inside.
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
    float t = 0.0f;
    glm::vec3 faceNormal(0.0f);
    if (!rayBox(origin, dir, halfExtents + glm::vec3(radius), maxDistance, t, faceNormal)) {
        return false;
    }

    // Which faces of the original box the entry lies beyond, a bit per axis.
    const glm::vec3 entry = origin + dir * t;
    int below = 0;
    int above = 0;
    for (int i = 0; i < 3; ++i) {
        if (entry[i] < -halfExtents[i]) below |= 1 << i;
        if (entry[i] >  halfExtents[i]) above |= 1 << i;
    }
    const int beyond = below | above;

    if ((beyond & (beyond - 1)) == 0) {
        tHit = t;
        localNormal = faceNormal;
        // Starting overlapped the slab test names no face; beyond exactly one, that face is the way out.
        if (beyond != 0 && glm::dot(faceNormal, faceNormal) == 0.0f) {
            const int axis = beyond == 1 ? 0 : (beyond == 2 ? 1 : 2);
            localNormal[axis] = (above & beyond) ? 1.0f : -1.0f;
        }
        return true;
    }

    // A corner of the box, a bit per axis set for its positive side.
    const auto corner = [&](int positive) {
        return glm::vec3(
            (positive & 1) ? halfExtents.x : -halfExtents.x,
            (positive & 2) ? halfExtents.y : -halfExtents.y,
            (positive & 4) ? halfExtents.z : -halfExtents.z
        );
    };

    bool found = false;
    float best = maxDistance;
    const auto tryEdge = [&](int from, int to) {
        float edgeT = 0.0f;
        glm::vec3 edgeNormal(0.0f);
        if (!rayCapsule(origin, dir, corner(from), corner(to), radius, edgeT, edgeNormal)) return;
        if (edgeT > best) return;
        best = edgeT;
        localNormal = edgeNormal;
        found = true;
    };

    if (beyond == 7) {
        for (int i = 0; i < 3; ++i) tryEdge(above, above ^ (1 << i));
    } else {
        // The one axis inside its slab is the edge's own direction.
        tryEdge(above, below ^ 7);
    }
    if (found) tHit = best;
    return found;
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
glm::vec3 closestPointOnTriangle(
    const glm::vec3& p,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c
) {
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
 * @brief Cast a ray, or sweep a sphere of @p radius, against one triangle.
 *
 * Exact. A sphere's centre stops on the face lifted a radius toward the sweep, or a capsule along an
 * edge (whose ends cover the corners): the nearest of the four.
 *
 * @param a First corner, world space.
 * @param b Second corner.
 * @param c Third corner.
 * @param origin Sweep start.
 * @param dir Unit sweep direction.
 * @param radius Sphere radius; zero for a ray.
 * @param maxDistance Furthest distance of interest.
 * @param[out] tHit Distance travelled to first contact.
 * @param[out] normal Outward normal on the side the sweep came from; zero when a sphere starts in-plane.
 * @return True when the sweep meets the triangle within @p maxDistance.
 */
bool sweepTriangle(
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    const glm::vec3& origin,
    const glm::vec3& dir,
    float radius,
    float maxDistance,
    float& tHit,
    glm::vec3& normal
) {
    // The side the sweep starts on is the side it can meet.
    const glm::vec3 face = glm::cross(b - a, c - a);
    const float faceLenSq = glm::dot(face, face);
    const glm::vec3 unit = faceLenSq > 0.0f ? face / std::sqrt(faceLenSq) : glm::vec3(0.0f);
    const glm::vec3 front = glm::dot(origin - a, unit) >= 0.0f ? unit : -unit;

    float t = 0.0f;
    if (radius <= 0.0f) {
        if (!rayTriangle(origin, dir, a, b, c, t) || t < 0.0f || t > maxDistance) return false;
        tHit = t;
        normal = front;
        return true;
    }

    // Already touching: the sweep stops where it starts.
    const glm::vec3 away = origin - closestPointOnTriangle(origin, a, b, c);
    const float awayLenSq = glm::dot(away, away);
    if (awayLenSq <= radius * radius) {
        tHit = 0.0f;
        normal = awayLenSq > glm::epsilon<float>() ? away / std::sqrt(awayLenSq) : glm::vec3(0.0f);
        return true;
    }

    bool found = false;
    float best = maxDistance;
    const glm::vec3 lift = front * radius;
    if (rayTriangle(origin, dir, a + lift, b + lift, c + lift, t) && t >= 0.0f && t <= best) {
        best = t;
        normal = front;
        found = true;
    }

    // An edge wins only strictly nearer: where they meet, the face's normal says which way it faces.
    const glm::vec3 corners[3] = {a, b, c};
    for (int i = 0; i < 3; ++i) {
        glm::vec3 edgeNormal(0.0f);
        if (!rayCapsule(origin, dir, corners[i], corners[(i + 1) % 3], radius, t, edgeNormal)) {
            continue;
        }
        if (t > best || (found && t == best)) continue;
        best = t;
        normal = edgeNormal;
        found = true;
    }
    if (found) tHit = best;
    return found;
}

/**
 * @brief Cast a ray or a swept sphere at one collider part.
 *
 * The one place a query decides what a shape is: a switch with no default, so a new shape warns here.
 *
 * @param collider The part's collider, for the points a mesh reads.
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
bool castPart(
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
) {
    switch (part.shape) {
        case ColliderShape::Box: {
            const glm::mat3 toLocal = glm::transpose(basis);
            const glm::vec3 localOrigin = toLocal * (origin - center);
            const glm::vec3 localDir = toLocal * dir;
            glm::vec3 localNormal(0.0f);

            const bool hit = radius > 0.0f
                ? sphereBox(localOrigin, localDir, radius, part.halfExtents, limit, t, localNormal)
                : rayBox(localOrigin, localDir, part.halfExtents, limit, t, localNormal);
            if (hit) normal = basis * localNormal;
            return hit;
        }

        case ColliderShape::Capsule: {
            // A swept sphere is a ray against the capsule grown by its radius.
            const glm::vec3 up = basis[1] * part.halfHeight;
            return rayCapsule(origin, dir, center - up, center + up, part.radius + radius, t, normal);
        }

        case ColliderShape::Mesh: {
            if (collider.meshNodes.empty()) return false;

            // Into the tree's frame, walked along the segment rather than through its box.
            const glm::mat3 toLocal = glm::transpose(basis);
            const glm::vec3 localOrigin = toLocal * (origin - center);
            const glm::vec3 localDir = toLocal * dir;

            t_scratchTriangles.clear();
            queryMeshBvhSegment(
                collider.meshNodes,
                localOrigin,
                localDir,
                limit,
                radius + Physics::CONTACT_TOLERANCE,
                t_scratchTriangles
            );

            bool found = false;
            float nearest = limit;
            for (uint32_t triangle : t_scratchTriangles) {
                const uint32_t base = triangle * 3;
                if (base + 2 >= collider.meshPoints.size()) continue;

                glm::vec3 face[3];
                for (int c = 0; c < 3; ++c) {
                    face[c] = center + basis * collider.meshPoints[base + c];
                }

                float hitT = 0.0f;
                glm::vec3 hitNormal(0.0f);
                const bool met = sweepTriangle(
                    face[0],
                    face[1],
                    face[2],
                    origin,
                    dir,
                    radius,
                    nearest,
                    hitT,
                    hitNormal
                );
                if (!met) continue;
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

bool passesMobility(const Rigidbody& rb, const QueryFilter& filter) {
    return rb.motion == RigidbodyMotion::Dynamic ? filter.hitDynamic : filter.hitStatic;
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

        const Transform* transform = scene.tryGet<Transform>(id);
        const Collider*  collider  = scene.tryGet<Collider>(id);
        if (!transform || !collider) continue;
        if (!collider->enabled) continue;
        if (collider->isTrigger && !filter.hitTriggers) continue;
        const Rigidbody& rb = rbStorage->dataAt(i);
        if ((rb.layer & filter.layerMask) == 0) continue;
        if (!passesMobility(rb, filter)) continue;

        const BodyPose pose = worldPoseOf(scene, id, *transform);
        const glm::mat3 basis = glm::mat3_cast(pose.rotation);

        for (const ColliderPart& part : collider->parts) {
            const glm::vec3 center = pose.position + basis * part.center;

            float     hitDistance = 0.0f;
            glm::vec3 hitNormal   = {0.0f, 1.0f, 0.0f};
            const bool hit = testPart(*collider, part, center, basis, bestDistance, hitDistance, hitNormal);
            if (!hit) continue;
            if (hitDistance > bestDistance) continue;
            // A tie goes to the lower slot, so every end names the same body.
            if (found && hitDistance == bestDistance && id.slot() > best.entity.slot()) continue;

            // No surface named (started inside, or on an axis): facing the caster lets it push back out.
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
        const Collider& collider,
        const ColliderPart& part,
        const glm::vec3& center,
        const glm::mat3& basis,
        float limit,
        float& t,
        glm::vec3& normal
    ) {
        return castPart(collider, part, center, basis, origin, dir, 0.0f, limit, t, normal);
    };
    return sweepBodies(scene, origin, dir, maxDistance, filter, out, castRay);
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
        const Collider& collider,
        const ColliderPart& part,
        const glm::vec3& center,
        const glm::mat3& basis,
        float limit,
        float& t,
        glm::vec3& normal
    ) {
        return castPart(collider, part, center, basis, origin, dir, radius, limit, t, normal);
    };
    const bool found = sweepBodies(scene, origin, dir, maxDistance, filter, out, castSphere);

    // sweepBodies reports the centre; the surface is one radius further.
    if (found) out.point -= out.normal * radius;
    return found;
}

} // namespace Vkm::Engine
