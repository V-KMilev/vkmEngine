#include "system/physics/collision/gjk.h"

#include <algorithm>
#include <array>
#include <utility>
#include <cmath>
#include <vector>

#include <glm/gtc/constants.hpp>

#include "system/physics/tolerance.h"

namespace Vkm::Engine {

namespace {

// A point of the Minkowski difference, kept with the two it came from so EPA
// can put the contact back on the surfaces rather than in the difference.
struct Vertex {
    glm::vec3 point;   ///< support(a, d) - support(b, -d)
    glm::vec3 onA;     ///< support(a, d)
};

Vertex minkowski(const SupportShape& a, const SupportShape& b,
                 const glm::vec3& dir) {
    Vertex v;
    v.onA = support(a, dir);
    v.point = v.onA - support(b, -dir);
    return v;
}

// The simplex GJK carries: never more than a tetrahedron, because four points
// are all it takes to enclose the origin in three dimensions.
struct Simplex {
    std::array<Vertex, 4> points{};
    int count = 0;

    void push(const Vertex& v) {
        points[3] = points[2];
        points[2] = points[1];
        points[1] = points[0];
        points[0] = v;
        if (count < 4) ++count;
    }
};

bool sameDirection(const glm::vec3& a, const glm::vec3& b) {
    return glm::dot(a, b) > 0.0f;
}

// Each case answers the same two questions: does the simplex already enclose
// the origin, and if not, which direction should the next support point be
// sought in. Regions the search came from cannot contain the origin and are
// never tested - that is what makes GJK terminate rather than wander.
bool lineCase(Simplex& simplex, glm::vec3& dir) {
    const glm::vec3 a = simplex.points[0].point;
    const glm::vec3 b = simplex.points[1].point;
    const glm::vec3 ab = b - a;

    if (sameDirection(ab, -a)) {
        dir = glm::cross(glm::cross(ab, -a), ab);
        // The origin is on the segment itself; any perpendicular will do.
        if (glm::dot(dir, dir) <= glm::epsilon<float>()) {
            dir = glm::cross(ab, glm::vec3(1.0f, 0.0f, 0.0f));
            if (glm::dot(dir, dir) <= glm::epsilon<float>()) {
                dir = glm::cross(ab, glm::vec3(0.0f, 1.0f, 0.0f));
            }
        }
    } else {
        simplex.count = 1;
        dir = -a;
    }
    return false;
}

bool triangleCase(Simplex& simplex, glm::vec3& dir) {
    const glm::vec3 a = simplex.points[0].point;
    const glm::vec3 b = simplex.points[1].point;
    const glm::vec3 c = simplex.points[2].point;

    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 face = glm::cross(ab, ac);

    if (sameDirection(glm::cross(face, ac), -a)) {
        if (sameDirection(ac, -a)) {
            simplex.points[1] = simplex.points[2];
            simplex.count = 2;
            dir = glm::cross(glm::cross(ac, -a), ac);
            return false;
        }
        simplex.count = 2;
        return lineCase(simplex, dir);
    }

    if (sameDirection(glm::cross(ab, face), -a)) {
        simplex.count = 2;
        return lineCase(simplex, dir);
    }

    // Above or below the face: keep it and search across it, winding the
    // simplex so the next point closes a tetrahedron the right way round.
    if (sameDirection(face, -a)) {
        dir = face;
    } else {
        const Vertex swap = simplex.points[1];
        simplex.points[1] = simplex.points[2];
        simplex.points[2] = swap;
        dir = -face;
    }
    return false;
}

bool tetrahedronCase(Simplex& simplex, glm::vec3& dir) {
    const glm::vec3 a = simplex.points[0].point;
    const glm::vec3 b = simplex.points[1].point;
    const glm::vec3 c = simplex.points[2].point;
    const glm::vec3 d = simplex.points[3].point;

    const glm::vec3 abc = glm::cross(b - a, c - a);
    const glm::vec3 acd = glm::cross(c - a, d - a);
    const glm::vec3 adb = glm::cross(d - a, b - a);

    // The fourth point was added toward the origin, so only the three faces
    // touching it can have the origin outside them.
    if (sameDirection(abc, -a)) {
        simplex.count = 3;
        return triangleCase(simplex, dir);
    }
    if (sameDirection(acd, -a)) {
        simplex.points[1] = simplex.points[2];
        simplex.points[2] = simplex.points[3];
        simplex.count = 3;
        return triangleCase(simplex, dir);
    }
    if (sameDirection(adb, -a)) {
        simplex.points[2] = simplex.points[1];
        simplex.points[1] = simplex.points[3];
        simplex.count = 3;
        return triangleCase(simplex, dir);
    }
    return true;
}

bool enclosesOrigin(Simplex& simplex, glm::vec3& dir) {
    switch (simplex.count) {
        case 2:  return lineCase(simplex, dir);
        case 3:  return triangleCase(simplex, dir);
        case 4:  return tetrahedronCase(simplex, dir);
        default: return false;
    }
}

// Enough for shapes that are not pathological, and a bound rather than a
// promise: a search that has not closed by here is not going to.
constexpr int MAX_GJK_ITERATIONS = 32;
constexpr int MAX_EPA_ITERATIONS = 48;

bool runGjk(const SupportShape& a, const SupportShape& b, Simplex& simplex) {
    glm::vec3 dir = {1.0f, 0.0f, 0.0f};
    simplex.push(minkowski(a, b, dir));

    // Degenerate first pick: the shapes' centres coincide along the search
    // axis, so anything else will do.
    if (glm::dot(simplex.points[0].point, simplex.points[0].point)
            <= glm::epsilon<float>()) {
        dir = {0.0f, 1.0f, 0.0f};
        simplex.count = 0;
        simplex.push(minkowski(a, b, dir));
    }
    dir = -simplex.points[0].point;

    for (int i = 0; i < MAX_GJK_ITERATIONS; ++i) {
        if (glm::dot(dir, dir) <= glm::epsilon<float>()) return true;

        const Vertex next = minkowski(a, b, dir);
        // The furthest point in this direction fell short of the origin, so
        // nothing of the difference reaches it and the shapes are apart.
        if (glm::dot(next.point, dir) < 0.0f) return false;

        simplex.push(next);
        if (enclosesOrigin(simplex, dir)) return true;
    }
    return false;
}

// GJK stops as soon as the origin is enclosed, and for flat-faced shapes that
// routinely happens on a point, a line or a triangle - the origin lying exactly
// on the simplex is the common case, not the pathological one. EPA needs a
// volume to grow, so the missing dimensions are searched for here rather than
// the contact being refused, which is what made a box land on a box only on the
// ticks where the arithmetic happened to leave four points behind.
bool expandToTetrahedron(const SupportShape& a, const SupportShape& b,
                         Simplex& simplex) {
    const glm::vec3 axes[3] = {
        {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}
    };

    // A point: any direction that reaches a different point will do.
    if (simplex.count == 1) {
        for (const glm::vec3& axis : axes) {
            for (float sign : {1.0f, -1.0f}) {
                const Vertex v = minkowski(a, b, axis * sign);
                const glm::vec3 span = v.point - simplex.points[0].point;
                if (glm::dot(span, span) > Physics::DEGENERATE_SQ) {
                    simplex.push(v);
                    break;
                }
            }
            if (simplex.count == 2) break;
        }
        if (simplex.count < 2) return false;
    }

    // A line: search across it, along whichever axis it leans on least.
    if (simplex.count == 2) {
        const glm::vec3 line = simplex.points[0].point - simplex.points[1].point;
        int leanest = 0;
        for (int i = 1; i < 3; ++i) {
            if (std::fabs(line[i]) < std::fabs(line[leanest])) leanest = i;
        }
        const glm::vec3 across = glm::cross(line, axes[leanest]);
        for (float sign : {1.0f, -1.0f}) {
            const Vertex v = minkowski(a, b, across * sign);
            const glm::vec3 span = v.point - simplex.points[0].point;
            if (glm::dot(span, span) > Physics::DEGENERATE_SQ) {
                simplex.push(v);
                break;
            }
        }
        if (simplex.count < 3) return false;
    }

    // A triangle: off its face, either side.
    if (simplex.count == 3) {
        const glm::vec3 normal = glm::cross(
            simplex.points[1].point - simplex.points[0].point,
            simplex.points[2].point - simplex.points[0].point);
        for (float sign : {1.0f, -1.0f}) {
            const Vertex v = minkowski(a, b, normal * sign);
            const float off = glm::dot(v.point - simplex.points[0].point,
                                       glm::normalize(normal));
            if (std::fabs(off) > Physics::CONTACT_TOLERANCE) {
                simplex.push(v);
                break;
            }
        }
    }

    return simplex.count == 4;
}

// Where the origin sits within a face, as weights on that face's corners.
// Every point of the Minkowski difference is a difference of two real points,
// so the weights that place the origin on the face are the same weights that
// place the contact on each shape.
glm::vec3 barycentric(const glm::vec3& p, const glm::vec3& a,
                      const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 v0 = b - a;
    const glm::vec3 v1 = c - a;
    const glm::vec3 v2 = p - a;

    const float d00 = glm::dot(v0, v0);
    const float d01 = glm::dot(v0, v1);
    const float d11 = glm::dot(v1, v1);
    const float d20 = glm::dot(v2, v0);
    const float d21 = glm::dot(v2, v1);

    const float denom = d00 * d11 - d01 * d01;
    // A sliver with no area: the corners are as good an answer as any, and
    // averaging them beats dividing by nothing.
    if (std::fabs(denom) <= glm::epsilon<float>()) {
        return glm::vec3(1.0f / 3.0f);
    }

    const float v = (d11 * d20 - d01 * d21) / denom;
    const float w = (d00 * d21 - d01 * d20) / denom;
    return {1.0f - v - w, v, w};
}

struct Face {
    int a = 0;
    int b = 0;
    int c = 0;
    glm::vec3 normal{0.0f};
    float distance = 0.0f;
};

Face makeFace(const std::vector<Vertex>& hull, int i, int j, int k) {
    Face face{i, j, k, glm::vec3(0.0f), 0.0f};
    const glm::vec3 n = glm::cross(hull[j].point - hull[i].point,
                                   hull[k].point - hull[i].point);
    const float lengthSq = glm::dot(n, n);
    if (lengthSq <= glm::epsilon<float>()) return face;

    face.normal = n / std::sqrt(lengthSq);
    face.distance = glm::dot(face.normal, hull[i].point);
    // Wind every face outward, so "nearest to the origin" is a distance rather
    // than a distance and a sign to remember.
    if (face.distance < 0.0f) {
        face.normal = -face.normal;
        face.distance = -face.distance;
        face.b = k;
        face.c = j;
    }
    return face;
}

} // namespace

bool gjkOverlap(const SupportShape& a, const SupportShape& b) {
    Simplex simplex;
    return runGjk(a, b, simplex);
}

bool gjkContact(const SupportShape& a, const SupportShape& b, Contact& out) {
    Simplex simplex;
    if (!runGjk(a, b, simplex)) return false;
    if (simplex.count < 4 && !expandToTetrahedron(a, b, simplex)) return false;

    // Scratch reused across calls: EPA runs once per overlapping pair per
    // tick, and four fresh vectors each time was allocator traffic for
    // buffers whose sizes are bounded by the iteration cap anyway.
    thread_local std::vector<Vertex> t_hull;
    thread_local std::vector<Face> t_faces;
    thread_local std::vector<std::pair<int, int>> t_horizon;
    thread_local std::vector<Face> t_kept;

    t_hull.assign(simplex.points.begin(), simplex.points.end());
    t_faces.assign({
        makeFace(t_hull, 0, 1, 2),
        makeFace(t_hull, 0, 2, 3),
        makeFace(t_hull, 0, 3, 1),
        makeFace(t_hull, 1, 3, 2)
    });

    // GJK has already proved the overlap, so from here every exit answers
    // with a contact: an expansion that runs out of iterations, or meets a
    // degenerate face, reports its best face so far rather than dropping an
    // overlap it knows exists - which was a body free to tunnel on exactly
    // the ticks the arithmetic was hardest.
    auto contactFromFace = [&](const Face& face) {
        // Where on the two surfaces, not merely how far apart. A support
        // point is whichever corner won a direction, which on a large flat
        // body is metres from where the two actually meet - and an impulse
        // applied there spins the body instead of stopping it.
        const glm::vec3 onFace = face.normal * face.distance;
        const glm::vec3 weights = barycentric(onFace,
                                              t_hull[face.a].point,
                                              t_hull[face.b].point,
                                              t_hull[face.c].point);
        const glm::vec3 onA = t_hull[face.a].onA * weights.x
                            + t_hull[face.b].onA * weights.y
                            + t_hull[face.c].onA * weights.z;

        out.normal = face.normal;
        out.penetration = face.distance;
        // Midway between the surfaces, which is where the two would touch
        // once the overlap is resolved.
        out.point = onA - face.normal * (face.distance * 0.5f);
    };

    for (int iteration = 0; iteration < MAX_EPA_ITERATIONS; ++iteration) {
        // The face nearest the origin is the shallowest way out, which is the
        // one a solver should push along.
        size_t nearest = 0;
        for (size_t i = 1; i < t_faces.size(); ++i) {
            if (t_faces[i].distance < t_faces[nearest].distance) nearest = i;
        }
        const Face face = t_faces[nearest];
        if (glm::dot(face.normal, face.normal) <= glm::epsilon<float>()) break;

        const Vertex next = minkowski(a, b, face.normal);
        const float reach = glm::dot(next.point, face.normal);

        // The surface is where this face already is: expanding further would
        // add a point the polytope already contains.
        if (reach - face.distance <= Physics::CONTACT_TOLERANCE) {
            contactFromFace(face);
            return true;
        }

        // Every face the new point can see is no longer on the surface. Their
        // shared edges are the t_horizon the new point cones back to; an edge
        // seen twice is interior to that t_horizon and cancels.
        const int added = static_cast<int>(t_hull.size());
        t_hull.push_back(next);

        t_horizon.clear();
        t_kept.clear();
        t_kept.reserve(t_faces.size());
        for (const Face& f : t_faces) {
            if (glm::dot(f.normal, next.point) - f.distance > 0.0f) {
                const std::pair<int, int> edges[3] = {
                    {f.a, f.b}, {f.b, f.c}, {f.c, f.a}
                };
                for (const auto& edge : edges) {
                    const auto twin = std::make_pair(edge.second, edge.first);
                    const auto it =
                        std::find(t_horizon.begin(), t_horizon.end(), twin);
                    if (it != t_horizon.end()) t_horizon.erase(it);
                    else t_horizon.push_back(edge);
                }
            } else {
                t_kept.push_back(f);
            }
        }

        if (t_horizon.empty()) break;
        for (const auto& edge : t_horizon) {
            t_kept.push_back(makeFace(t_hull, edge.first, edge.second, added));
        }
        t_faces.swap(t_kept);
    }

    size_t nearest = 0;
    for (size_t i = 1; i < t_faces.size(); ++i) {
        if (t_faces[i].distance < t_faces[nearest].distance) nearest = i;
    }
    // EPA runs only after GJK has already proved these shapes intersect, so
    // "no overlap" is not an answer available here: a degenerate polytope means
    // the depth is unmeasurable, not that the contact is absent. Reporting one
    // is a body the solver never separates, and it sinks through.
    const auto usable = [](const Face& f) {
        return glm::dot(f.normal, f.normal) > glm::epsilon<float>();
    };
    if (!t_faces.empty() && !usable(t_faces[nearest])) {
        for (size_t i = 0; i < t_faces.size(); ++i) {
            if (usable(t_faces[i]) && (!usable(t_faces[nearest]) ||
                                       t_faces[i].distance < t_faces[nearest].distance)) {
                nearest = i;
            }
        }
    }
    // Only when no face in the whole polytope has a direction is there nothing
    // to report, and then there is genuinely nothing to point the solver along.
    if (t_faces.empty() || !usable(t_faces[nearest])) return false;
    contactFromFace(t_faces[nearest]);
    return true;
}

} // namespace Vkm::Engine
