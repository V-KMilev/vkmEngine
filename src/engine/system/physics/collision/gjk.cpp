#include "system/physics/collision/gjk.h"

#include <array>
#include <utility>

#include <glm/gtc/constants.hpp>

namespace Vkm::Engine {

namespace {

// The point of the Minkowski difference furthest along dir.
glm::vec3 minkowski(const SupportShape& a, const SupportShape& b, const glm::vec3& dir) {
    return support(a, dir) - support(b, -dir);
}

// At most a tetrahedron: four points enclose the origin in 3D.
struct Simplex {
    std::array<glm::vec3, 4> points{};
    int count = 0;

    void push(const glm::vec3& v) {
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

// Each case: does the simplex enclose the origin, and if not, where to search next. Regions the search
// came from are never tested, which is what makes GJK terminate.
bool lineCase(Simplex& simplex, glm::vec3& dir) {
    const glm::vec3 a = simplex.points[0];
    const glm::vec3 b = simplex.points[1];
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
    const glm::vec3 a = simplex.points[0];
    const glm::vec3 b = simplex.points[1];
    const glm::vec3 c = simplex.points[2];

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

    // Above or below the face: wound so the next point closes the tetrahedron the right way round.
    if (sameDirection(face, -a)) {
        dir = face;
    } else {
        std::swap(simplex.points[1], simplex.points[2]);
        dir = -face;
    }
    return false;
}

bool tetrahedronCase(Simplex& simplex, glm::vec3& dir) {
    const glm::vec3 a = simplex.points[0];
    const glm::vec3 b = simplex.points[1];
    const glm::vec3 c = simplex.points[2];
    const glm::vec3 d = simplex.points[3];

    const glm::vec3 abc = glm::cross(b - a, c - a);
    const glm::vec3 acd = glm::cross(c - a, d - a);
    const glm::vec3 adb = glm::cross(d - a, b - a);

    // The fourth point was added toward the origin, so only the faces touching it can exclude it.
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

// A bound, not a promise: a search that has not closed by here will not.
constexpr int MAX_GJK_ITERATIONS = 32;

bool runGjk(const SupportShape& a, const SupportShape& b, Simplex& simplex) {
    glm::vec3 dir = {1.0f, 0.0f, 0.0f};
    simplex.push(minkowski(a, b, dir));

    // Degenerate: the support along +X is the origin itself (touching), which gives no direction.
    if (glm::dot(simplex.points[0], simplex.points[0]) <= glm::epsilon<float>()) {
        dir = {0.0f, 1.0f, 0.0f};
        simplex.count = 0;
        simplex.push(minkowski(a, b, dir));
    }
    dir = -simplex.points[0];

    for (int i = 0; i < MAX_GJK_ITERATIONS; ++i) {
        if (glm::dot(dir, dir) <= glm::epsilon<float>()) return true;

        const glm::vec3 next = minkowski(a, b, dir);
        // The furthest point fell short of the origin: the shapes are apart.
        if (glm::dot(next, dir) < 0.0f) return false;

        simplex.push(next);
        if (enclosesOrigin(simplex, dir)) return true;
    }
    return false;
}

} // namespace

bool gjkOverlap(const SupportShape& a, const SupportShape& b) {
    Simplex simplex;
    return runGjk(a, b, simplex);
}

} // namespace Vkm::Engine
