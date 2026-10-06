#include "system/physics/collision/support.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

namespace Vkm::Engine {

SupportShape supportOf(const BoxShape& box) {
    SupportShape shape;
    shape.kind = SupportShape::Kind::Box;
    shape.center = box.center;
    shape.axes[0] = box.axes[0];
    shape.axes[1] = box.axes[1];
    shape.axes[2] = box.axes[2];
    shape.halfExtents = box.halfExtents;
    return shape;
}

SupportShape supportOf(const CapsuleShape& capsule) {
    SupportShape shape;
    shape.kind = SupportShape::Kind::Capsule;
    shape.a = capsule.a;
    shape.b = capsule.b;
    shape.radius = capsule.radius;
    return shape;
}

SupportShape supportOfPoints(const glm::vec3* points, uint32_t count) {
    SupportShape shape;
    shape.kind = SupportShape::Kind::Points;
    shape.points = points;
    shape.count = count;
    return shape;
}

glm::vec3 support(const SupportShape& shape, const glm::vec3& direction) {
    switch (shape.kind) {
        case SupportShape::Kind::Box: {
            // The extreme corner agrees in sign with the direction on every axis.
            glm::vec3 point = shape.center;
            for (int i = 0; i < 3; ++i) {
                const float side = glm::dot(shape.axes[i], direction);
                const float sign = side >= 0.0f ? 1.0f : -1.0f;
                point += shape.axes[i] * (shape.halfExtents[i] * sign);
            }
            return point;
        }

        case SupportShape::Kind::Capsule: {
            // The further end, pushed out by the radius: exact, as a capsule is a swept sphere.
            const bool towardB = glm::dot(direction, shape.b - shape.a) >= 0.0f;
            const glm::vec3 end = towardB ? shape.b : shape.a;
            const float lengthSq = glm::dot(direction, direction);
            if (lengthSq <= glm::epsilon<float>()) return end;
            return end + direction * (shape.radius / std::sqrt(lengthSq));
        }

        case SupportShape::Kind::Points: {
            if (!shape.points || shape.count == 0) return glm::vec3(0.0f);

            uint32_t best = 0;
            float bestDot = glm::dot(shape.points[0], direction);
            for (uint32_t i = 1; i < shape.count; ++i) {
                const float d = glm::dot(shape.points[i], direction);
                if (d > bestDot) {
                    bestDot = d;
                    best = i;
                }
            }

            return shape.points[best];
        }
    }
    return glm::vec3(0.0f);
}

} // namespace Vkm::Engine
