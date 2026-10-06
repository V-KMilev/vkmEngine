#include "system/physics/collision/narrowphase.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "system/physics/collision/gjk.h"
#include "system/physics/collision/support.h"
#include "system/physics/tolerance.h"

namespace Vkm::Engine {

namespace {

float projectRadius(const BoxShape& box, const glm::vec3& axis) {
    return box.halfExtents.x * std::fabs(glm::dot(box.axes[0], axis))
        + box.halfExtents.y * std::fabs(glm::dot(box.axes[1], axis))
        + box.halfExtents.z * std::fabs(glm::dot(box.axes[2], axis));
}

/**
 * @brief Overlap of the two boxes projected onto a candidate separating axis.
 *
 * @param a First box.
 * @param b Second box.
 * @param axis Unit axis to project onto.
 * @param toCentre From a's centre to b's.
 * @return Signed overlap; positive means overlapping.
 */
float overlapOnAxis(const BoxShape& a, const BoxShape& b, const glm::vec3& axis, const glm::vec3& toCentre) {
    return projectRadius(a, axis) + projectRadius(b, axis) - std::fabs(glm::dot(toCentre, axis));
}

/**
 * @brief Compute the four world-space vertices of one face of a box.
 *
 * @param box Box whose face is wanted.
 * @param axis Local axis index (0..2) normal to the face.
 * @param sign Outward direction along that axis, +1 or -1.
 * @return The four corners, wound consistently.
 */
std::array<glm::vec3, 4> faceVertices(const BoxShape& box, int axis, float sign) {
    const int a = (axis + 1) % 3;
    const int b = (axis + 2) % 3;
    const glm::vec3 center = box.center + box.axes[axis] * (box.halfExtents[axis] * sign);
    const glm::vec3 ua = box.axes[a] * box.halfExtents[a];
    const glm::vec3 ub = box.axes[b] * box.halfExtents[b];
    return {center + ua + ub, center + ua - ub, center - ua - ub, center - ua + ub};
}

// A convex contact polygon, inline so the narrowphase stays off the heap. A face (4) clipped by 4 planes,
// each adding at most one vertex, never exceeds 8; 16 is headroom.
constexpr int MAX_CLIP_VERTS = 16;

struct ClipPoly {
    glm::vec3 v[MAX_CLIP_VERTS];
    int n = 0;
    void push(const glm::vec3& p) { if (n < MAX_CLIP_VERTS) v[n++] = p; }
};

/**
 * @brief Sutherland-Hodgman clip of a polygon, keeping where dot(v - planePoint, planeNormal) <= 0.
 *
 * Only a strict crossing emits an intersection: a vertex exactly on the plane is already kept, and
 * boxes resting square put all four incident vertices there.
 *
 * @param poly Polygon to clip, in place.
 * @param planePoint Any point on the plane.
 * @param planeNormal The plane's outward normal.
 */
void clipToPlane(ClipPoly& poly, const glm::vec3& planePoint, const glm::vec3& planeNormal) {
    ClipPoly result;
    for (int i = 0; i < poly.n; ++i) {
        const glm::vec3& cur = poly.v[i];
        const glm::vec3& nxt = poly.v[(i + 1) % poly.n];
        const float dc = glm::dot(cur - planePoint, planeNormal);
        const float dn = glm::dot(nxt - planePoint, planeNormal);
        if (dc <= 0.0f) result.push(cur);

        if ((dc < 0.0f && dn > 0.0f) || (dc > 0.0f && dn < 0.0f)) {
            const float t = dc / (dc - dn);
            result.push(cur + t * (nxt - cur));
        }
    }
    poly = result;
}

/**
 * @brief Choose at most MAX_CONTACTS_PER_MANIFOLD points that still span the contact.
 *
 * Four consecutive vertices of a clipped octagon (equal boxes at 45 degrees) would put the centroid near
 * a corner, so: the deepest point, the one furthest from it, and the furthest to either side of that
 * line. Degenerate polygons fill the rest in order: always min(n, 4) points, never a repeat.
 *
 * @param pts    Candidate points, world space.
 * @param depth  Penetration depth of each, parallel to @p pts.
 * @param n      Candidate count.
 * @param normal Reference face normal, for which side of a line a point is on.
 * @param keep   Out: indices into @p pts.
 * @return Indices written to @p keep.
 */
int reduceToManifold(const glm::vec3* pts, const float* depth, int n, const glm::vec3& normal, int* keep) {
    int k = 0;
    const auto add = [&](int idx) {
        if (idx < 0) return;
        for (int j = 0; j < k; ++j) {
            if (keep[j] == idx) return;
        }
        if (k < MAX_CONTACTS_PER_MANIFOLD) keep[k++] = idx;
    };

    if (n <= MAX_CONTACTS_PER_MANIFOLD) {
        for (int i = 0; i < n; ++i) add(i);
        return k;
    }

    int deepest = 0;
    for (int i = 1; i < n; ++i) {
        if (depth[i] > depth[deepest]) deepest = i;
    }

    int   furthest = -1;
    float bestDistanceSq = -1.0f;
    for (int i = 0; i < n; ++i) {
        const glm::vec3 delta = pts[i] - pts[deepest];
        const float distanceSq = glm::dot(delta, delta);
        if (distanceSq > bestDistanceSq) {
            bestDistanceSq = distanceSq;
            furthest = i;
        }
    }

    // Signed area against their line, so the extremes are one to each side.
    const glm::vec3 spine = pts[furthest] - pts[deepest];
    int   leftMost  = -1;
    int   rightMost = -1;
    float bestLeft  = 0.0f;
    float bestRight = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float area = glm::dot(normal, glm::cross(spine, pts[i] - pts[deepest]));
        if (area > bestLeft) {
            bestLeft  = area;
            leftMost  = i;
        }
        if (area < bestRight) {
            bestRight = area;
            rightMost = i;
        }
    }

    add(deepest);
    add(furthest);
    add(leftMost);
    add(rightMost);
    for (int i = 0; i < n && k < MAX_CONTACTS_PER_MANIFOLD; ++i) add(i);
    return k;
}

/**
 * @brief Find the closest pair of points between two line segments.
 *
 * Ericson's clamped parametric method (Real-Time Collision Detection), including point segments.
 *
 * @param p1 Start of the first segment.
 * @param q1 End of the first segment.
 * @param p2 Start of the second segment.
 * @param q2 End of the second segment.
 * @param c1 Out: closest point on the first segment.
 * @param c2 Out: closest point on the second segment.
 */
void closestSegmentSegment(
    const glm::vec3& p1,
    const glm::vec3& q1,
    const glm::vec3& p2,
    const glm::vec3& q2,
    glm::vec3& c1,
    glm::vec3& c2
) {
    const glm::vec3 d1 = q1 - p1;
    const glm::vec3 d2 = q2 - p2;
    const glm::vec3 r = p1 - p2;
    const float a = glm::dot(d1, d1);
    const float e = glm::dot(d2, d2);
    const float f = glm::dot(d2, r);

    float s = 0.0f;
    float t = 0.0f;
    if (a <= Physics::DEGENERATE_SQ && e <= Physics::DEGENERATE_SQ) {
        c1 = p1;
        c2 = p2;
        return;
    }
    if (a <= Physics::DEGENERATE_SQ) {
        t = glm::clamp(f / e, 0.0f, 1.0f);
    } else {
        const float c = glm::dot(d1, r);
        if (e <= Physics::DEGENERATE_SQ) {
            s = glm::clamp(-c / a, 0.0f, 1.0f);
        } else {
            const float b = glm::dot(d1, d2);
            // denom is a*e*sin^2(angle): against a*e it measures the angle alone, not metres^4.
            const float denom = a * e - b * b;
            if (denom > Physics::DEGENERATE_SQ * a * e) {
                s = glm::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
            }
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = glm::clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = glm::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

/**
 * @brief Edge-edge contact: one point midway between the closest points of the edges @p caseIndex picks.
 *
 * @param a       First box.
 * @param b       Second box.
 * @param caseIndex 6..14; (caseIndex - 6) / 3 is a's edge axis, % 3 is b's.
 * @param axis    Contact normal, oriented A -> B.
 * @param overlap Penetration depth along @p axis.
 * @param out     Contact array; one entry is written.
 * @return Always 1.
 */
int edgeEdgeContact(
    const BoxShape& a,
    const BoxShape& b,
    int caseIndex,
    const glm::vec3& axis,
    float overlap,
    Contact* out
) {
    const int ia = (caseIndex - 6) / 3;
    const int jb = (caseIndex - 6) % 3;

    // The two non-edge axes pick the extreme corner along the normal; the edge axis spans the edge.
    glm::vec3 pA = a.center;
    for (int k = 0; k < 3; ++k)
        if (k != ia)
            pA += a.axes[k] * (glm::dot(a.axes[k], axis) > 0.0f ? a.halfExtents[k] : -a.halfExtents[k]);
    glm::vec3 pB = b.center;
    for (int k = 0; k < 3; ++k)
        if (k != jb)
            pB += b.axes[k] * (glm::dot(b.axes[k], axis) > 0.0f ? -b.halfExtents[k] : b.halfExtents[k]);

    glm::vec3 c1;
    glm::vec3 c2;
    closestSegmentSegment(
        pA - a.axes[ia] * a.halfExtents[ia],
        pA + a.axes[ia] * a.halfExtents[ia],
        pB - b.axes[jb] * b.halfExtents[jb],
        pB + b.axes[jb] * b.halfExtents[jb],
        c1,
        c2
    );

    out[0].point = (c1 + c2) * 0.5f;
    out[0].normal = axis;
    out[0].penetration = overlap;
    return 1;
}

/**
 * @brief Face contact: the incident face clipped to the reference face's sides, kept where below it.
 *
 * @param a         First box.
 * @param b         Second box.
 * @param caseIndex 0..5; selects which box owns the reference face (0..2 -> A).
 * @param axis      Contact normal, oriented A -> B.
 * @param out       Room for MAX_CONTACTS_PER_MANIFOLD entries.
 * @return Contacts written.
 */
int faceContact(const BoxShape& a, const BoxShape& b, int caseIndex, const glm::vec3& axis, Contact* out) {
    const bool refIsA = caseIndex < 3;
    const BoxShape& ref = refIsA ? a : b;
    const BoxShape& inc = refIsA ? b : a;
    const int refAxis = refIsA ? caseIndex : caseIndex - 3;
    const glm::vec3 refNormal = refIsA ? axis : -axis;  // outward from ref toward inc
    const float refSign = glm::dot(ref.axes[refAxis], refNormal) >= 0.0f ? 1.0f : -1.0f;

    // Incident face: the face of inc whose outward normal most opposes refNormal.
    int incAxis = 0;
    float maxAbsDot = glm::dot(inc.axes[0], refNormal);
    for (int i = 1; i < 3; ++i) {
        const float d = glm::dot(inc.axes[i], refNormal);
        if (std::fabs(d) > std::fabs(maxAbsDot)) {
            maxAbsDot = d;
            incAxis = i;
        }
    }
    const float incSign = maxAbsDot > 0.0f ? -1.0f : 1.0f;

    const auto incFace = faceVertices(inc, incAxis, incSign);
    ClipPoly poly;
    for (const glm::vec3& vtx : incFace) poly.push(vtx);

    const glm::vec3 refCenter = ref.center + ref.axes[refAxis] * (ref.halfExtents[refAxis] * refSign);
    const int tangents[2] = {(refAxis + 1) % 3, (refAxis + 2) % 3};
    for (int t : tangents) {
        clipToPlane(poly, refCenter + ref.axes[t] * ref.halfExtents[t],  ref.axes[t]);
        clipToPlane(poly, refCenter - ref.axes[t] * ref.halfExtents[t], -ref.axes[t]);
    }

    glm::vec3 pts[MAX_CLIP_VERTS];
    float     depth[MAX_CLIP_VERTS];
    int       n = 0;
    for (int i = 0; i < poly.n; ++i) {
        const glm::vec3& v = poly.v[i];
        const float dist = glm::dot(v - refCenter, refNormal);
        if (dist > 0.0f) continue;  // not penetrating the reference face
        pts[n]   = v;
        depth[n] = -dist;
        ++n;
    }

    int       keep[MAX_CONTACTS_PER_MANIFOLD];
    const int count = reduceToManifold(pts, depth, n, refNormal, keep);
    for (int i = 0; i < count; ++i) {
        out[i].point       = pts[keep[i]];
        out[i].normal      = axis;  // A -> B regardless of which box is reference
        out[i].penetration = depth[keep[i]];
    }
    return count;
}

/**
 * @brief Closest point on segment [pa, pb] to an axis-aligned box of half-extents @p h at the origin.
 *
 * Exact: the squared distance is one parabola per stretch between slab crossings (at most six), each
 * solved in closed form.
 *
 * @param pa Segment start, in the box's local frame.
 * @param pb Segment end, in the box's local frame.
 * @param h Box half-extents.
 * @return The closest point, on the segment, in the same local frame.
 */
glm::vec3 closestOnSegmentToBox(const glm::vec3& pa, const glm::vec3& pb, const glm::vec3& h) {
    const glm::vec3 dir = pb - pa;

    float cuts[8] = {0.0f, 1.0f};
    int cutCount = 2;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dir[i]) <= glm::epsilon<float>()) continue;
        for (const float bound : {-h[i], h[i]}) {
            const float t = (bound - pa[i]) / dir[i];
            if (t > 0.0f && t < 1.0f && cutCount < 8) cuts[cutCount++] = t;
        }
    }
    // Not std::sort: at most eight values, and the optimiser cannot prove a bound on the library's path.
    for (int i = 1; i < cutCount; ++i) {
        const float key = cuts[i];
        int j = i - 1;
        while (j >= 0 && cuts[j] > key) {
            cuts[j + 1] = cuts[j];
            --j;
        }
        cuts[j + 1] = key;
    }

    float bestT = 0.0f;
    float bestDist2 = std::numeric_limits<float>::max();
    for (int c = 0; c + 1 < cutCount; ++c) {
        const float t0 = cuts[c];
        const float t1 = cuts[c + 1];
        if (t1 - t0 <= glm::epsilon<float>()) continue;

        // Fixed across the stretch, so read once in the middle, away from any boundary.
        const glm::vec3 middle = pa + dir * ((t0 + t1) * 0.5f);
        float a = 0.0f;
        float b = 0.0f;
        float cc = 0.0f;
        for (int i = 0; i < 3; ++i) {
            float offset = 0.0f;
            if (middle[i] >  h[i]) offset =  h[i];
            else if (middle[i] < -h[i]) offset = -h[i];
            else continue;  // inside the slab: contributes nothing
            const float k = pa[i] - offset;
            a  += dir[i] * dir[i];
            b  += 2.0f * dir[i] * k;
            cc += k * k;
        }

        // The parabola's vertex, held inside the stretch it was derived for.
        float t = t0;
        if (a > glm::epsilon<float>()) t = glm::clamp(-b / (2.0f * a), t0, t1);
        const float dist2 = a * t * t + b * t + cc;
        if (dist2 < bestDist2) {
            bestDist2 = dist2;
            bestT = t;
        }
    }
    return pa + dir * bestT;
}

/**
 * @brief Any unit vector perpendicular to @p v.
 *
 * Crosses the world axis @p v leans on least, so it never degenerates.
 *
 * @param v Must not be zero.
 * @return A unit vector perpendicular to @p v.
 */
glm::vec3 perpendicularTo(const glm::vec3& v) {
    const glm::vec3 ref = std::fabs(v.x) < std::fabs(v.y) && std::fabs(v.x) < std::fabs(v.z)
        ? glm::vec3(1.0f, 0.0f, 0.0f)
        : (std::fabs(v.y) < std::fabs(v.z) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::normalize(glm::cross(v, ref));
}

// |dot(capsule axis, face normal)| below this (~3 degrees) lies flat, getting two points; past it the
// deeper end carries the contact alone.
constexpr float CAPSULE_FLAT_DOT = 0.05f;

/**
 * @brief Two-point manifold for a capsule segment lying flat on one box face.
 *
 * The segment clipped to the face, contacted at both ends. 0 when the normal names no face, the segment
 * is too tilted to lie flat, or the clip leaves nothing; the caller's single point stands in then.
 *
 * @param box The box; its frame is the one the local inputs are in.
 * @param pa Segment start, box-local.
 * @param dir Segment start-to-end vector, box-local.
 * @param normal Contact normal (capsule -> box), box-local.
 * @param radius Capsule sweep radius.
 * @param out Contacts, world space.
 * @return Contacts written, 0..2.
 */
int capsuleFaceContact(
    const BoxShape& box,
    const glm::vec3& pa,
    const glm::vec3& dir,
    const glm::vec3& normal,
    float radius,
    Contact* out
) {
    int face = -1;
    for (int i = 0; i < 3; ++i) {
        const float offAxis = std::fabs(std::fabs(normal[i]) - 1.0f);
        if (offAxis < Physics::AXIS_TOLERANCE) face = i;
    }

    const float dirLen2 = glm::dot(dir, dir);
    if (face < 0 || dirLen2 <= Physics::DEGENERATE_SQ) return 0;
    if (std::fabs(glm::dot(dir / std::sqrt(dirLen2), normal)) >= CAPSULE_FLAT_DOT) return 0;

    const glm::vec3& h = box.halfExtents;
    float t0 = 0.0f;
    float t1 = 1.0f;
    bool inside = true;
    for (int k = 1; k <= 2 && inside; ++k) {
        const int u = (face + k) % 3;
        if (std::fabs(dir[u]) <= glm::epsilon<float>()) {
            inside = std::fabs(pa[u]) <= h[u];
            continue;
        }
        float lo = (-h[u] - pa[u]) / dir[u];
        float hi = ( h[u] - pa[u]) / dir[u];
        if (lo > hi) std::swap(lo, hi);
        t0 = std::max(t0, lo);
        t1 = std::min(t1, hi);
        inside = t0 <= t1;
    }
    if (!inside || t1 - t0 <= Physics::CONTACT_TOLERANCE) return 0;

    const glm::mat3 rot(box.axes[0], box.axes[1], box.axes[2]);
    const float outward   = -normal[face];
    const float facePlane = outward * h[face];
    int count = 0;
    const float ends[2] = {t0, t1};
    for (float t : ends) {
        const glm::vec3 s = pa + dir * t;
        const float above = outward * s[face] - h[face];
        const float depth = radius - above;
        if (depth <= 0.0f) continue;
        glm::vec3 cp = s;
        cp[face] = facePlane;
        out[count].point = box.center + rot * cp;
        out[count].normal = rot * normal;
        out[count].penetration = depth;
        ++count;
    }
    return count;
}

// How far an edge-edge axis must beat the best face axis (relative factor, absolute metres): a real edge
// crossing wins by millimetres, a degenerate cross product shadowing a face normal never does.
constexpr float EDGE_PREFERENCE_REL = 0.98f;
constexpr float EDGE_PREFERENCE_ABS = 0.001f;

/**
 * @brief The feature of @p shape that reaches furthest along @p direction.
 *
 * The box face nearest @p direction, a flat capsule's two deepest points, else the support point. A
 * support point alone would balance a flat box on one corner.
 *
 * @param shape Shape to ask.
 * @param direction Unit direction to reach along.
 * @param[out] poly The feature's points, world space.
 */
void deepestFeature(const SupportShape& shape, const glm::vec3& direction, ClipPoly& poly) {
    poly.n = 0;
    switch (shape.kind) {
        case SupportShape::Kind::Box: {
            int axis = 0;
            float facing = glm::dot(shape.axes[0], direction);
            for (int i = 1; i < 3; ++i) {
                const float d = glm::dot(shape.axes[i], direction);
                if (std::fabs(d) > std::fabs(facing)) {
                    facing = d;
                    axis = i;
                }
            }
            BoxShape box;
            box.center = shape.center;
            box.axes[0] = shape.axes[0];
            box.axes[1] = shape.axes[1];
            box.axes[2] = shape.axes[2];
            box.halfExtents = shape.halfExtents;
            for (const glm::vec3& corner : faceVertices(box, axis, facing >= 0.0f ? 1.0f : -1.0f)) {
                poly.push(corner);
            }
            return;
        }

        case SupportShape::Kind::Capsule: {
            const glm::vec3 segment = shape.b - shape.a;
            const float lengthSq = glm::dot(segment, segment);
            const bool flat = lengthSq > Physics::DEGENERATE_SQ
                && std::fabs(glm::dot(segment, direction)) < CAPSULE_FLAT_DOT * std::sqrt(lengthSq);
            if (!flat) break;
            poly.push(shape.a + direction * shape.radius);
            poly.push(shape.b + direction * shape.radius);
            return;
        }

        case SupportShape::Kind::Points:
            break;
    }
    poly.push(support(shape, direction));
}

/**
 * @brief Clip a feature, keeping where dot(v - planePoint, planeNormal) <= 0.
 *
 * A polygon clips as clipToPlane does. A segment's outside end moves to the crossing, since walked as a
 * loop it would emit it twice; a point is kept or dropped.
 *
 * @param poly Feature to clip, in place.
 * @param planePoint Any point on the plane.
 * @param planeNormal The plane's outward normal.
 */
void clipFeature(ClipPoly& poly, const glm::vec3& planePoint, const glm::vec3& planeNormal) {
    if (poly.n > 2) {
        clipToPlane(poly, planePoint, planeNormal);
        return;
    }

    ClipPoly kept;
    for (int i = 0; i < poly.n; ++i) {
        const float d = glm::dot(poly.v[i] - planePoint, planeNormal);
        if (d <= 0.0f) {
            kept.push(poly.v[i]);
            continue;
        }
        if (poly.n < 2) continue;

        // The other end on or past the plane: kept already, or outside too.
        const glm::vec3& other = poly.v[1 - i];
        const float otherD = glm::dot(other - planePoint, planeNormal);
        if (otherD >= 0.0f) continue;
        kept.push(poly.v[i] + (other - poly.v[i]) * (d / (d - otherD)));
    }
    poly = kept;
}

} // namespace

int contactBoxes(const BoxShape& a, const BoxShape& b, Contact* out) {
    // SAT over 15 axes (3 face normals per box, 9 edge crosses); the least overlap picks the builder.
    const glm::vec3 toCentre = b.center - a.center;

    float bestOverlap = std::numeric_limits<float>::max();
    int bestCase = -1;  // 0..2 face A, 3..5 face B, 6..14 edge-edge
    glm::vec3 bestAxis(0.0f);

    auto tryAxis = [&](glm::vec3 axis, int caseIndex) {
        const float len2 = glm::dot(axis, axis);
        // Parallel edges: no axis.
        if (len2 < Physics::DEGENERATE_SQ) return true;
        axis /= std::sqrt(len2);
        const float overlap = overlapOnAxis(a, b, axis, toCentre);
        if (overlap < 0.0f) return false;  // separating axis found
        if (overlap < bestOverlap) {
            bestOverlap = overlap;
            bestCase = caseIndex;
            bestAxis = glm::dot(axis, toCentre) < 0.0f ? -axis : axis;  // orient A -> B
        }
        return true;
    };

    for (int i = 0; i < 3; ++i) if (!tryAxis(a.axes[i], i)) return 0;
    for (int i = 0; i < 3; ++i) if (!tryAxis(b.axes[i], 3 + i)) return 0;
    const float     faceOverlap = bestOverlap;
    const int       faceCase    = bestCase;
    const glm::vec3 faceAxis    = bestAxis;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!tryAxis(glm::cross(a.axes[i], b.axes[j]), 6 + i * 3 + j)) return 0;

    // Only by a clear margin: an edge cross can duplicate a face normal, turning a face manifold into a
    // corner the solver rocks on.
    if (bestCase >= 6 && faceCase >= 0
        && bestOverlap >= faceOverlap * EDGE_PREFERENCE_REL - EDGE_PREFERENCE_ABS) {
        bestCase    = faceCase;
        bestAxis    = faceAxis;
        bestOverlap = faceOverlap;
    }

    if (bestCase < 0) return 0;

    return bestCase >= 6
        ? edgeEdgeContact(a, b, bestCase, bestAxis, bestOverlap, out)
        : faceContact(a, b, bestCase, bestAxis, out);
}

int contactCapsuleBox(const CapsuleShape& a, const BoxShape& b, Contact* out) {
    // In the box's frame the closest point is a clamp and face clipping is two intervals. Orthonormal
    // axes, so the inverse is a transpose.
    const glm::mat3 rot(b.axes[0], b.axes[1], b.axes[2]);
    const glm::mat3 inv = glm::transpose(rot);
    const glm::vec3& h = b.halfExtents;
    const glm::vec3 pa = inv * (a.a - b.center);
    const glm::vec3 pb = inv * (a.b - b.center);

    const glm::vec3 p = closestOnSegmentToBox(pa, pb, h);
    const glm::vec3 surface = glm::clamp(p, -h, h);
    const glm::vec3 delta = p - surface;
    const float dist2 = glm::dot(delta, delta);

    glm::vec3 normal(0.0f);   // capsule -> box
    glm::vec3 point(0.0f);
    float penetration = 0.0f;
    if (dist2 > Physics::DEGENERATE_SQ) {
        const float dist = std::sqrt(dist2);
        if (dist > a.radius) return 0;
        normal = -delta / dist;
        point = surface;
        penetration = a.radius - dist;
    } else {
        // The segment reaches inside the box: push out through the face that frees the whole segment soonest
        // (the minimum translation), not the face nearest one point, however deep the rest is.
        int axis = 0;
        float sign = 1.0f;
        float depth = std::numeric_limits<float>::max();
        for (int i = 0; i < 3; ++i) {
            const float up   = h[i] - std::min(pa[i], pb[i]);   // out through +i
            const float down = h[i] + std::max(pa[i], pb[i]);   // out through -i
            if (up < depth) {
                depth = up;
                axis = i;
                sign =  1.0f;
            }
            if (down < depth) {
                depth = down;
                axis = i;
                sign = -1.0f;
            }
        }
        // The end that leaves last, held under the face it leaves through.
        const bool aDeeper = sign > 0.0f ? pa[axis] <= pb[axis] : pa[axis] >= pb[axis];
        normal[axis] = -sign;
        point = glm::clamp(aDeeper ? pa : pb, -h, h);
        point[axis] = sign * h[axis];
        penetration = a.radius + depth;
    }

    const int n = capsuleFaceContact(b, pa, pb - pa, normal, a.radius, out);
    if (n > 0) return n;

    out[0].point = b.center + rot * point;
    out[0].normal = rot * normal;
    out[0].penetration = penetration;
    return 1;
}

int contactTriangle(const glm::vec3* triangle, const SupportShape& shape, Contact* out) {
    const glm::vec3 e1 = triangle[1] - triangle[0];
    const glm::vec3 e2 = triangle[2] - triangle[0];
    const glm::vec3 face = glm::cross(e1, e2);
    const float faceLenSq = glm::dot(face, face);
    // |face|^2 is |e1|^2 |e2|^2 sin^2(corner): against the edge lengths it asks about the angle alone;
    // against DEGENERATE_SQ alone a two-centimetre triangle would have no face.
    if (faceLenSq <= Physics::DEGENERATE_SQ * glm::dot(e1, e1) * glm::dot(e2, e2)) return 0;

    if (!gjkOverlap(supportOfPoints(triangle, 3), shape)) return 0;

    // Along the face normal, never the shallowest way out, which flips with the shape's centre.
    const glm::vec3 normal = face / std::sqrt(faceLenSq);
    const glm::vec3 deepest = support(shape, -normal);
    const float reach = glm::dot(triangle[0] - deepest, normal);
    if (reach <= 0.0f) return 0;

    // Outward from each edge, in-plane; counter-clockwise winding puts the inside on the left.
    glm::vec3 sides[3];
    for (int i = 0; i < 3; ++i) {
        sides[i] = glm::normalize(glm::cross(triangle[(i + 1) % 3] - triangle[i], normal));

        // Met from the side: its way out is through the edge, where a riser's face holds it or nothing
        // does. Pushed along the normal, a shape brushing a ledge's riser would be lifted the whole step.
        const float intrusion = glm::dot(triangle[i] - support(shape, -sides[i]), sides[i]);
        if (intrusion < reach) return 0;
    }

    ClipPoly poly;
    deepestFeature(shape, -normal, poly);
    for (int i = 0; i < 3; ++i) clipFeature(poly, triangle[i], sides[i]);

    glm::vec3 pts[MAX_CLIP_VERTS];
    float     depth[MAX_CLIP_VERTS];
    int       n = 0;
    for (int i = 0; i < poly.n; ++i) {
        const float below = glm::dot(triangle[0] - poly.v[i], normal);
        if (below <= 0.0f) continue;   // above the face: not touching it
        pts[n]   = poly.v[i];
        depth[n] = below;
        ++n;
    }

    // Nothing of the feature lies over the triangle: it meets it past an edge.
    if (n == 0) {
        pts[0]   = deepest;
        depth[0] = reach;
        n = 1;
    }

    int       keep[MAX_CONTACTS_PER_MANIFOLD];
    const int count = reduceToManifold(pts, depth, n, normal, keep);
    for (int i = 0; i < count; ++i) {
        // Midway, so neither surface biases where the impulse lands.
        out[i].point       = pts[keep[i]] + normal * (depth[keep[i]] * 0.5f);
        out[i].normal      = normal;
        out[i].penetration = depth[keep[i]];
    }
    return count;
}

int contactCapsuleCapsule(const CapsuleShape& a, const CapsuleShape& b, Contact* out) {
    glm::vec3 ca;
    glm::vec3 cb;
    closestSegmentSegment(a.a, a.b, b.a, b.b, ca, cb);

    const glm::vec3 delta = cb - ca;
    const float dist2 = glm::dot(delta, delta);
    const float reach = a.radius + b.radius;
    if (dist2 > reach * reach) return 0;

    glm::vec3 normal;
    float dist = 0.0f;
    if (dist2 > Physics::DEGENERATE_SQ) {
        dist = std::sqrt(dist2);
        normal = delta / dist;
    } else {
        // The axes touch: any perpendicular to A's axis separates them; along it would shove one through.
        const glm::vec3 axis = a.b - a.a;
        normal = glm::dot(axis, axis) > Physics::DEGENERATE_SQ
            ? perpendicularTo(axis)
            : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // Midway, so neither radius biases where the impulse lands.
    out[0].point = (ca + normal * a.radius + cb - normal * b.radius) * 0.5f;
    out[0].normal = normal;
    out[0].penetration = reach - dist;
    return 1;
}

} // namespace Vkm::Engine
