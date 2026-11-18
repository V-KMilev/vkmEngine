#pragma once

// The worlds the physics tests build: bodies of one part, placed as a scene author places them.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/scene/transform.h"
#include "ecs/scene.h"

namespace Vkm::Test {

using namespace Vkm::Engine;

inline ColliderPart box(const glm::vec3& halfExtents, const glm::vec3& center = glm::vec3(0.0f)) {
    ColliderPart part;
    part.shape       = ColliderShape::Box;
    part.halfExtents = halfExtents;
    part.center      = center;
    return part;
}

/// A capsule standing along +Y; a half height of 0 is a sphere.
inline ColliderPart capsule(float radius, float halfHeight) {
    ColliderPart part;
    part.shape      = ColliderShape::Capsule;
    part.radius     = radius;
    part.halfHeight = halfHeight;
    return part;
}

/**
 * @brief A body of one collider part.
 *
 * @param scene    Where.
 * @param position Its Transform's position.
 * @param part     Its one part.
 * @param motion   How the solver moves it.
 * @param rotation Its Transform's rotation.
 * @return The entity.
 */
inline EntityId addBody(
    Scene& scene,
    const glm::vec3& position,
    const ColliderPart& part,
    RigidbodyMotion motion = RigidbodyMotion::Dynamic,
    const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)
) {
    const EntityId id = scene.createEntity();
    Transform transform;
    transform.position = position;
    transform.rotation = rotation;
    scene.add(id, transform);
    Rigidbody body;
    body.motion = motion;
    scene.add(id, body);
    Collider collider;
    collider.parts = {part};
    scene.add(id, collider);
    return id;
}

/// A static slab 40 m across whose top is at @p top.
inline EntityId addFloor(Scene& scene, float top = 0.0f) {
    return addBody(scene, {0.0f, top - 0.5f, 0.0f}, box({20.0f, 0.5f, 20.0f}), RigidbodyMotion::Static);
}

/// A dynamic unit cube centered at @p position.
inline EntityId addCrate(Scene& scene, const glm::vec3& position) {
    return addBody(scene, position, box(glm::vec3(0.5f)));
}

inline glm::vec3 positionOf(Scene& scene, EntityId id) { return scene.get<Transform>(id).position; }
inline Rigidbody& bodyOf(Scene& scene, EntityId id)    { return scene.get<Rigidbody>(id); }

/// How far @p rotation tips its up axis from world up, in degrees.
inline float tiltDegrees(const glm::quat& rotation) {
    const float upness = glm::clamp((rotation * glm::vec3(0.0f, 1.0f, 0.0f)).y, -1.0f, 1.0f);
    return glm::degrees(std::acos(upness));
}

} // namespace Vkm::Test
