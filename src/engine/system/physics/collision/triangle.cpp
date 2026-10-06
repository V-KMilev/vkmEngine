#include "system/physics/collision/triangle.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

namespace Vkm::Engine {

bool rayTriangle(
    const glm::vec3& origin,
    const glm::vec3& dir,
    const glm::vec3& v0,
    const glm::vec3& v1,
    const glm::vec3& v2,
    float& t
) {
    const glm::vec3 e1 = v1 - v0;
    const glm::vec3 e2 = v2 - v0;
    const glm::vec3 p  = glm::cross(dir, e2);
    const float det = glm::dot(e1, p);

    // Parallel. det is |e1||e2| sin for a unit dir, so it is held against the edge lengths: a fixed
    // epsilon would call every small triangle parallel.
    const float scale = std::sqrt(glm::dot(e1, e1) * glm::dot(e2, e2) * glm::dot(dir, dir));
    if (std::fabs(det) <= glm::epsilon<float>() * scale) return false;

    const float inv = 1.0f / det;
    const glm::vec3 tv = origin - v0;
    const float u = glm::dot(tv, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;

    const glm::vec3 q = glm::cross(tv, e1);
    const float v = glm::dot(dir, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;

    t = glm::dot(e2, q) * inv;
    return true;
}

} // namespace Vkm::Engine
