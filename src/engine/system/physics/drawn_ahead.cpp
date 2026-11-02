#include "system/physics/drawn_ahead.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vkm_physics/mass_model.h"

#include "base/profile/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/animation/bone_socket.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "net/net_session.h"

namespace Vkm::Engine {

namespace {

/// What @p entity hangs from; null for a root.
EntityId parentOf(const Scene& scene, EntityId entity) {
    const Hierarchy* link = scene.tryGet<Hierarchy>(entity);
    return link ? link->parent : EntityId{};
}

} // namespace

DrawnAhead::DrawnAhead()  = default;
DrawnAhead::~DrawnAhead() = default;

void DrawnAhead::carry(Scene& scene, const NetSession& net, float ahead) {
    m_carried.clear();
    m_sockets.clear();
    m_epoch = scene.epoch();
    if (ahead <= 0.0f) return;

    PROFILE_SCOPE("DrawnAhead");
    const glm::vec3 still(0.0f);
    scene.forEach<Rigidbody, Transform>([&](EntityId id, const Rigidbody& body, Transform& at) {
        if (body.motion == RigidbodyMotion::Static || body.sleeping) return;
        if (body.linearVelocity == still && body.angularVelocity == still) return;
        const Hierarchy* link = scene.tryGet<Hierarchy>(id);
        if ((link && link->parent) || !net.simulates(scene, id)) return;

        const Transform tick = at;

        // About its solver center, which its linear velocity is of.
        const float     speed = glm::length(body.angularVelocity);
        const glm::quat spin  = speed > 0.0f
            ? glm::angleAxis(speed * ahead, body.angularVelocity / speed)
            : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const Physics::MassModel model = massModelOf(scene.tryGet<Collider>(id));
        const glm::vec3 center = Physics::solverCenter(physicsMotionOf(body.motion), body.mass, model);
        const glm::vec3 lever  = at.rotation * center;
        at.position += body.linearVelocity * ahead + lever - spin * lever;
        at.rotation = glm::normalize(spin * at.rotation);

        m_carried.push_back({id, tick, at});
    });

    scene.forEach<BoneSocket, Transform>([&](EntityId id, const BoneSocket&, const Transform& at) {
        m_sockets.push_back({id, parentOf(scene, id), at});
    });
}

void DrawnAhead::putBack(Scene& scene) {
    // A world replaced in the frame was never carried: its entities only share the old ids.
    if (scene.epoch() == m_epoch) {
        for (const Carried& carried : m_carried) {
            Transform* at = scene.tryGet<Transform>(carried.entity);
            if (!at) continue;
            if (at->position == carried.drawn.position) at->position = carried.tick.position;
            if (at->rotation == carried.drawn.rotation) at->rotation = carried.tick.rotation;
        }
        // One the frame took off its bone, or hung from another parent, is the frame's to place now.
        for (const Placed& socket : m_sockets) {
            const bool held = scene.has<BoneSocket>(socket.entity)
                && parentOf(scene, socket.entity) == socket.parent;
            if (!held) continue;
            if (Transform* at = scene.tryGet<Transform>(socket.entity)) *at = socket.tick;
        }
    }
    m_carried.clear();
    m_sockets.clear();
}

} // namespace Vkm::Engine
