#include "system/physics/collision/narrowphase.h"

#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine {

namespace {

constexpr float EPS = 1e-6f;

/**
 * @brief Oriented bounding box: centre, three unit world axes, half extents.
 */
struct OBB {
    glm::vec3 c;
    glm::vec3 u[3];
    glm::vec3 e;
};

OBB makeOBB(const glm::vec3& center, const glm::quat& rotation, const glm::vec3& halfExtents) {
    const glm::mat3 r = glm::mat3_cast(rotation);
    return OBB{center, {r[0], r[1], r[2]}, halfExtents};
}

float projectRadius(const OBB& box, const glm::vec3& axis) {
    return box.e.x * std::fabs(glm::dot(box.u[0], axis))
         + box.e.y * std::fabs(glm::dot(box.u[1], axis))
         + box.e.z * std::fabs(glm::dot(box.u[2], axis));
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
float overlapOnAxis(const OBB& a, const OBB& b, const glm::vec3& axis, const glm::vec3& toCentre) {
    return projectRadius(a, axis) + projectRadius(b, axis) - std::fabs(glm::dot(toCentre, axis));
}

/**
 * @brief Compute the four world-space vertices of one face of an OBB.
 *
 * The face is selected by the local axis it is perpendicular to and the outward
 * direction along that axis.
 *
 * @param box The oriented box whose face is wanted.
 * @param axis Local axis index (0..2) normal to the face.
 * @param sign Outward direction along that axis (+1 or -1).
 * @return The four face corners in world space, wound consistently.
 */
std::array<glm::vec3, 4> faceVertices(const OBB& box, int axis, float sign) {
    const int a = (axis + 1) % 3;
    const int b = (axis + 2) % 3;
    const glm::vec3 center = box.c + box.u[axis] * (box.e[axis] * sign);
    const glm::vec3 ua = box.u[a] * box.e[a];
    const glm::vec3 ub = box.u[b] * box.e[b];
    return {
        center + ua + ub,
        center + ua - ub,
        center - ua - ub,
        center - ua + ub
    };
}

/**
 * @brief Sutherland-Hodgman clip of a polygon against a half-space: keep the side
 * where dot(v - planePoint, planeNormal) <= 0.
 */
void clipToPlane(
    std::vector<glm::vec3>& poly,
    const glm::vec3& planePoint,
    const glm::vec3& planeNormal
) {
    std::vector<glm::vec3> result;
    result.reserve(poly.size() + 1);
    for (size_t i = 0; i < poly.size(); ++i) {
        const glm::vec3& cur = poly[i];
        const glm::vec3& nxt = poly[(i + 1) % poly.size()];
        const float dc = glm::dot(cur - planePoint, planeNormal);
        const float dn = glm::dot(nxt - planePoint, planeNormal);
        if (dc <= 0.0f) result.push_back(cur);
        if ((dc < 0.0f) != (dn < 0.0f)) {
            const float t = dc / (dc - dn);
            result.push_back(cur + t * (nxt - cur));
        }
    }
    poly.swap(result);
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

int contactBoxBox(const OBB& a, const OBB& b, Contact* out) {
    const glm::vec3 toCentre = b.c - a.c;

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

    for (int i = 0; i < 3; ++i) if (!tryAxis(a.u[i], i)) return 0;
    for (int i = 0; i < 3; ++i) if (!tryAxis(b.u[i], 3 + i)) return 0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!tryAxis(glm::cross(a.u[i], b.u[j]), 6 + i * 3 + j)) return 0;

    if (bestCase < 0) return 0;

    // Edge-edge: one contact at the midpoint of the closest edge pair.
    if (bestCase >= 6) {
        const int ia = (bestCase - 6) / 3;
        const int jb = (bestCase - 6) % 3;

        glm::vec3 pA = a.c;
        for (int k = 0; k < 3; ++k)
            if (k != ia) pA += a.u[k] * (glm::dot(a.u[k], bestAxis) > 0.0f ? a.e[k] : -a.e[k]);
        glm::vec3 pB = b.c;
        for (int k = 0; k < 3; ++k)
            if (k != jb) pB += b.u[k] * (glm::dot(b.u[k], bestAxis) > 0.0f ? -b.e[k] : b.e[k]);

        glm::vec3 c1;
        glm::vec3 c2;
        closestSegmentSegment(
            pA - a.u[ia] * a.e[ia], pA + a.u[ia] * a.e[ia],
            pB - b.u[jb] * b.e[jb], pB + b.u[jb] * b.e[jb],
            c1, c2);

        out[0].point = (c1 + c2) * 0.5f;
        out[0].normal = bestAxis;
        out[0].penetration = bestOverlap;
        return 1;
    }

    // Face contact: clip the incident face against the reference face side planes.
    const bool refIsA = bestCase < 3;
    const OBB& ref = refIsA ? a : b;
    const OBB& inc = refIsA ? b : a;
    const int refAxis = refIsA ? bestCase : bestCase - 3;
    const glm::vec3 refNormal = refIsA ? bestAxis : -bestAxis;  // outward from ref toward inc
    const float refSign = glm::dot(ref.u[refAxis], refNormal) >= 0.0f ? 1.0f : -1.0f;

    // Incident face: the face of inc whose outward normal most opposes refNormal.
    int incAxis = 0;
    float minDot = glm::dot(inc.u[0], refNormal);
    for (int i = 1; i < 3; ++i) {
        const float d = glm::dot(inc.u[i], refNormal);
        if (std::fabs(d) > std::fabs(minDot)) { minDot = d; incAxis = i; }
    }
    const float incSign = minDot > 0.0f ? -1.0f : 1.0f;

    const auto incFace = faceVertices(inc, incAxis, incSign);
    std::vector<glm::vec3> poly(incFace.begin(), incFace.end());

    const glm::vec3 refCenter = ref.c + ref.u[refAxis] * (ref.e[refAxis] * refSign);
    const int t0 = (refAxis + 1) % 3;
    const int t1 = (refAxis + 2) % 3;
    const int tangents[2] = {t0, t1};
    for (int t : tangents) {
        const glm::vec3 planePt1 = refCenter + ref.u[t] * ref.e[t];
        clipToPlane(poly, planePt1, ref.u[t]);
        const glm::vec3 planePt2 = refCenter - ref.u[t] * ref.e[t];
        clipToPlane(poly, planePt2, -ref.u[t]);
    }

    int count = 0;
    for (const glm::vec3& v : poly) {
        const float dist = glm::dot(v - refCenter, refNormal);
        if (dist > 0.0f) continue;  // not penetrating the reference face
        if (count >= MAX_CONTACTS_PER_MANIFOLD) break;
        out[count].point = v;
        out[count].normal = bestAxis;  // A -> B regardless of which box is reference
        out[count].penetration = -dist;
        ++count;
    }
    return count;
}

} // namespace

int contactBoxes(
    const glm::vec3& centerA, const glm::quat& rotA, const glm::vec3& halfA,
    const glm::vec3& centerB, const glm::quat& rotB, const glm::vec3& halfB,
    Contact* out
) {
    return contactBoxBox(makeOBB(centerA, rotA, halfA),
                         makeOBB(centerB, rotB, halfB), out);
}

} // namespace Engine
