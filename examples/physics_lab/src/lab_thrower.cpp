#include "lab_thrower.h"

#include <glm/glm.hpp>

#include "core/math/axes.h"
#include "core/math/rotation.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/mesh.h"
#include "ecs/scene.h"
#include "platform/input/input_map.h"
#include "platform/window/glfw_include.h"
#include "resource/generate/mesh_generators.h"
#include "resource/resource_manager.h"
#include "system/physics/physics_events.h"

#include "lab_walker.h"

namespace Lab {

namespace {

constexpr const char* ACTION_THROW = "Throw";

constexpr float MUZZLE_FORWARD = 1.0f;
constexpr float MUZZLE_HEIGHT  = 1.2f;
constexpr float CRATE_MASS     = 2.0f;

// How far from the hit, in metres, the push falls to half: the struck limbs take most of it.
constexpr float PUSH_REACH = 0.35f;

} // namespace

void LabThrower::onStart() {
    input().define(ACTION_THROW, { InputBinding{InputSource::Key, GLFW_KEY_G, 1.0f} });

    subscribe([this](const CollisionEvent& hit) {
        if (hit.phase != ContactPhase::Began || !net().isOffline()) return;
        for (const Thrown& crate : m_live) {
            if (crate.entity == hit.a) knockDown(hit.b, crate, hit.point);
            if (crate.entity == hit.b) knockDown(hit.a, crate, hit.point);
        }
    });

    m_mesh = resources().add(generateCube(), "lab:crate");

    MaterialAsset crate;
    crate.albedo    = {0.62f, 0.42f, 0.18f, 1.0f};
    crate.roughness = 0.85f;
    m_material = resources().add(std::move(crate), "lab:crate_material");
}

void LabThrower::onUpdate(float dt) {
    // Offline only: a client-made entity takes a slot the server may give away,
    // and a server reads no key. Only the character the player sits in throws.
    const Transform* from = tryGet<Transform>();
    if (from && net().isOffline() && input().pressed(ACTION_THROW)
        && isOfflineSeat(scene(), entity())) {
        const glm::vec3 aim = Math::computeForward(from->rotation);

        const EntityId crate = spawn("Thrown Crate");
        Transform at;
        at.position = from->position + aim * MUZZLE_FORWARD + Math::WORLD_UP * MUZZLE_HEIGHT;
        at.scale    = glm::vec3(size * 2.0f);
        scene().add(crate, std::move(at));
        scene().add(crate, Mesh{m_mesh, m_material});

        // Assigned, not appended: a Collider arrives holding one unit box.
        Collider collider;
        ColliderPart box;
        box.halfExtents = glm::vec3(size);
        collider.parts = { box };
        scene().add(crate, std::move(collider));

        Rigidbody body;
        body.mass           = CRATE_MASS;
        body.linearVelocity = aim * speed + Math::WORLD_UP * (speed * 0.25f);
        const glm::vec3 momentum = body.linearVelocity * body.mass;
        scene().add(crate, std::move(body));

        m_live.push_back({crate, momentum, lifetime});
    }

    for (size_t i = m_felled.size(); i-- > 0; ) {
        m_felled[i].remaining -= dt;
        if (m_felled[i].remaining > 0.0f) continue;
        if (Ragdoll* ragdoll = scene().tryGet<Ragdoll>(m_felled[i].character)) ragdoll->active = false;
        m_felled[i] = m_felled.back();
        m_felled.pop_back();
    }

    // Back to front, so swap-removal only moves visited elements. destroy() is deferred.
    for (size_t i = m_live.size(); i-- > 0; ) {
        m_live[i].remaining -= dt;
        if (m_live[i].remaining > 0.0f) continue;
        if (scene().isAlive(m_live[i].entity)) destroy(m_live[i].entity);
        m_live[i] = m_live.back();
        m_live.pop_back();
    }
}

void LabThrower::knockDown(EntityId character, const Thrown& crate, const glm::vec3& at) {
    Ragdoll* ragdoll = scene().tryGet<Ragdoll>(character);
    if (!ragdoll || ragdoll->active || ragdoll->bones.empty()) return;
    ragdoll->active = true;

    // The crate's momentum, shared out so the bones nearest the hit move most: the rest follow through
    // the joints, and the body tumbles rather than sliding off whole.
    float weighted = 0.0f;
    for (const RagdollBone& bone : ragdoll->bones) {
        const Rigidbody* body = scene().tryGet<Rigidbody>(bone.body);
        const Transform* pose = scene().tryGet<Transform>(bone.body);
        if (!body || !pose) continue;
        const float d = glm::distance(resolvedWorldPosition(scene(), bone.body, *pose), at) / PUSH_REACH;
        weighted += body->mass / (1.0f + d * d);
    }
    if (weighted <= 0.0f) return;
    for (const RagdollBone& bone : ragdoll->bones) {
        Rigidbody* body = scene().tryGet<Rigidbody>(bone.body);
        const Transform* pose = scene().tryGet<Transform>(bone.body);
        if (!body || !pose) continue;
        const float d = glm::distance(resolvedWorldPosition(scene(), bone.body, *pose), at) / PUSH_REACH;
        body->linearVelocity += crate.momentum / (weighted * (1.0f + d * d));
    }

    if (downFor > 0.0f) m_felled.push_back({character, downFor});
}

} // namespace Lab
