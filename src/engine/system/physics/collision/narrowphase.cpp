#include "system/physics/collision/narrowphase.h"

#include <array>
#include <cmath>
#include <limits>

#include <glm/glm.hpp>

namespace Engine {

namespace {

constexpr float EPS = 1e-6f;

float projectRadius(const BoxShape& box, const glm::vec3& axis) {
    return box.halfExtents.x * std::fabs(glm::dot(box.axes[0], axis))
         + box.halfExtents.y * std::fabs(glm::dot(box.axes[1], axis))
         + box.halfExtents.z * std::fabs(glm::dot(box.axes[2], axis));
}

/**
 * @brief Overlap of the two boxes projected onto a candidate separating axis.
 *
 * A positive result means the projections overlap by that much; a negative result
 * means the axis separates the two boxes.
 *
 * @param a First oriented box.
 * @param b Second oriented box.
 * @param axis Unit axis to project both boxes onto.
 * @param toCentre Vector from a's centre to b's centre.
 * @return Signed overlap along the axis; positive == overlapping.
 */
float overlapOnAxis(const BoxShape& a, const BoxShape& b, const glm::vec3& axis, const glm::vec3& toCentre) {
    return projectRadius(a, axis) + projectRadius(b, axis) - std::fabs(glm::dot(toCentre, axis));
}

/**
 * @brief Compute the four world-space vertices of one face of a box.
 *
 * The face is selected by the local axis it is perpendicular to and the outward
 * direction along that axis.
 *
 * @param box The oriented box whose face is wanted.
 * @param axis Local axis index (0..2) normal to the face.
 * @param sign Outward direction along that axis (+1 or -1).
 * @return The four face corners in world space, wound consistently.
 */
std::array<glm::vec3, 4> faceVertices(const BoxShape& box, int axis, float sign) {
    const int a = (axis + 1) % 3;
    const int b = (axis + 2) % 3;
    const glm::vec3 center = box.center + box.axes[axis] * (box.halfExtents[axis] * sign);
    const glm::vec3 ua = box.axes[a] * box.halfExtents[a];
    const glm::vec3 ub = box.axes[b] * box.halfExtents[b];
    return {
        center + ua + ub,
        center + ua - ub,
        center - ua - ub,
        center - ua + ub
    };
}

// A convex contact polygon with inline storage. An incident box face starts at 4
// vertices and is clipped against 4 reference side planes; each clip adds at most
// one vertex, so the count never exceeds 8 - 16 leaves comfortable headroom and
// keeps the narrowphase off the heap.
constexpr int MAX_CLIP_VERTS = 16;

struct ClipPoly {
    glm::vec3 v[MAX_CLIP_VERTS];
    int n = 0;
    void push(const glm::vec3& p) { if (n < MAX_CLIP_VERTS) v[n++] = p; }
};

/**
 * @brief Sutherland-Hodgman clip of a polygon against a half-space: keep the side
 * where dot(v - planePoint, planeNormal) <= 0.
 */
void clipToPlane(ClipPoly& poly, const glm::vec3& planePoint, const glm::vec3& planeNormal) {
    ClipPoly result;
    for (int i = 0; i < poly.n; ++i) {
        const glm::vec3& cur = poly.v[i];
        const glm::vec3& nxt = poly.v[(i + 1) % poly.n];
        const float dc = glm::dot(cur - planePoint, planeNormal);
        const float dn = glm::dot(nxt - planePoint, planeNormal);
        if (dc <= 0.0f) result.push(cur);
        if ((dc < 0.0f) != (dn < 0.0f)) {
            const float t = dc / (dc - dn);
            result.push(cur + t * (nxt - cur));
        }
    }
    poly = result;
}

/**
 * @brief Find the closest pair of points between two line segments.
 *
 * Uses the clamped parametric method from Ericson, Real-Time Collision Detection,
 * handling the degenerate cases where either segment collapses to a point.
 *
 * @param p1 Start of the first segment.
 * @param q1 End of the first segment.
 * @param p2 Start of the second segment.
 * @param q2 End of the second segment.
 * @param c1 Out: closest point on the first segment.
 * @param c2 Out: closest point on the second segment.
 */
void closestSegmentSegment(
    const glm::vec3& p1, const glm::vec3& q1,
    const glm::vec3& p2, const glm::vec3& q2,
    glm::vec3& c1, glm::vec3& c2
) {
    const glm::vec3 d1 = q1 - p1;
    const glm::vec3 d2 = q2 - p2;
    const glm::vec3 r = p1 - p2;
    const float a = glm::dot(d1, d1);
    const float e = glm::dot(d2, d2);
    const float f = glm::dot(d2, r);

    float s = 0.0f;
    float t = 0.0f;
    if (a <= EPS && e <= EPS) {
        c1 = p1; c2 = p2; return;
    }
    if (a <= EPS) {
        t = glm::clamp(f / e, 0.0f, 1.0f);
    } else {
        const float c = glm::dot(d1, r);
        if (e <= EPS) {
            s = glm::clamp(-c / a, 0.0f, 1.0f);
        } else {
            const float b = glm::dot(d1, d2);
            const float denom = a * e - b * b;
            if (denom > EPS) s = glm::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
            t = (b * s + f) / e;
            if (t < 0.0f)      { t = 0.0f; s = glm::clamp(-c / a, 0.0f, 1.0f); }
            else if (t > 1.0f) { t = 1.0f; s = glm::clamp((b - c) / a, 0.0f, 1.0f); }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

/**
 * @brief Edge-edge contact: a single point at the midpoint of the closest points
 * of the two edges selected by @p caseIndex (each box's edge along axis ia / jb).
 *
 * @param axis    Contact normal, oriented A -> B.
 * @param overlap Penetration depth along @p axis.
 */
int edgeEdgeContact(const BoxShape& a, const BoxShape& b, int caseIndex,
                    const glm::vec3& axis, float overlap, Contact* out) {
    const int ia = (caseIndex - 6) / 3;
    const int jb = (caseIndex - 6) % 3;

    // Walk each centre out to the contacting edge: the two non-edge axes pick the
    // extreme corner along the normal, the edge axis itself spans the edge below.
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
        pA - a.axes[ia] * a.halfExtents[ia], pA + a.axes[ia] * a.halfExtents[ia],
        pB - b.axes[jb] * b.halfExtents[jb], pB + b.axes[jb] * b.halfExtents[jb],
        c1, c2);

    out[0].point = (c1 + c2) * 0.5f;
    out[0].normal = axis;
    out[0].penetration = overlap;
    return 1;
}

/**
 * @brief Face contact: clip the incident box's face against the reference face's
 * side planes and keep the points lying below the reference face.
 *
 * @param caseIndex 0..5; selects which box owns the reference face (0..2 -> A).
 * @param axis      Contact normal, oriented A -> B.
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
        if (std::fabs(d) > std::fabs(maxAbsDot)) { maxAbsDot = d; incAxis = i; }
    }
    const float incSign = maxAbsDot > 0.0f ? -1.0f : 1.0f;

    const auto incFace = faceVertices(inc, incAxis, incSign);
    ClipPoly poly;
    for (const glm::vec3& vtx : incFace) poly.push(vtx);

    // Clip against the reference face's four side planes (both directions along
    // each of its two in-face tangents).
    const glm::vec3 refCenter = ref.center + ref.axes[refAxis] * (ref.halfExtents[refAxis] * refSign);
    const int tangents[2] = {(refAxis + 1) % 3, (refAxis + 2) % 3};
    for (int t : tangents) {
        clipToPlane(poly, refCenter + ref.axes[t] * ref.halfExtents[t],  ref.axes[t]);
        clipToPlane(poly, refCenter - ref.axes[t] * ref.halfExtents[t], -ref.axes[t]);
    }

    int count = 0;
    for (int i = 0; i < poly.n; ++i) {
        const glm::vec3& v = poly.v[i];
        const float dist = glm::dot(v - refCenter, refNormal);
        if (dist > 0.0f) continue;  // not penetrating the reference face
        if (count >= MAX_CONTACTS_PER_MANIFOLD) break;
        out[count].point = v;
        out[count].normal = axis;  // A -> B regardless of which box is reference
        out[count].penetration = -dist;
        ++count;
    }
    return count;
}

} // namespace

int contactBoxes(const BoxShape& a, const BoxShape& b, Contact* out) {
    // Separating-axis test (SAT): scan the 15 candidate axes (3 face normals per
    // box + 9 edge-edge cross products); a negative overlap on any axis means no
    // collision. The axis of minimum overlap classifies the contact as edge-edge
    // or face, which is then dispatched to the matching builder.
    const glm::vec3 toCentre = b.center - a.center;

    float bestOverlap = std::numeric_limits<float>::max();
    int bestCase = -1;          // 0..2 face A, 3..5 face B, 6..14 edge-edge
    glm::vec3 bestAxis(0.0f);

    auto tryAxis = [&](glm::vec3 axis, int caseIndex) {
        const float len2 = glm::dot(axis, axis);
        if (len2 < EPS) return true;   // degenerate (parallel edges); skip
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
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!tryAxis(glm::cross(a.axes[i], b.axes[j]), 6 + i * 3 + j)) return 0;

    if (bestCase < 0) return 0;

    return bestCase >= 6
        ? edgeEdgeContact(a, b, bestCase, bestAxis, bestOverlap, out)
        : faceContact(a, b, bestCase, bestAxis, out);
}

} // namespace Engine
