#include "lab_range.h"

#include <optional>
#include <string_view>

#include "core/math/axes.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/net_local.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/core/world_transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/mesh.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "io/scene/prefab.h"
#include "net/net_session.h"
#include "net/wire/protocol.h"
#include "resource/resource_manager.h"

#include "lab_switch.h"
#include "lab_walker.h"

namespace Lab {

namespace {

// Tumble a thrown body gets, rad/s about the axis across its throw, per m/s of its kick.
constexpr float SPIN_PER_KICK = 0.6f;

// Of the radius, the share within which a player is knocked down.
constexpr float KNOCK_DOWN_SHARE = 0.6f;

// The blow that knocks a player down, kg m/s.
constexpr float KNOCK_DOWN_MOMENTUM = 500.0f;

// A body nearer the charge than this, metres, has no way out from it and is thrown straight up.
constexpr float AT_THE_CHARGE = 1e-3f;

// Show or hide every mesh in @p root's subtree, and switch its collider with it.
void stand(Scene& scene, EntityId root, bool standing) {
    for (const EntityId e : HierarchyOperations::collectSubtree(scene, root)) {
        if (Mesh* mesh = scene.tryGet<Mesh>(e)) mesh->visible = standing;
    }
    if (Collider* collider = scene.tryGet<Collider>(root)) collider->enabled = standing;
}

} // namespace

void LabRange::onStart() {
    HierarchyOperations::forEachChild(scene(), entity(), [&](EntityId child) {
        const Name* name = scene().tryGet<Name>(child);
        const Transform* at = scene().tryGet<Transform>(child);
        if (name && at && std::string_view(name->value).rfind("Charge", 0) == 0) {
            m_charges.push_back({child, at->position, at->rotation});
        }
    });
}

bool LabRange::detonate() {
    bool blew = false;
    for (const Charge& charge : m_charges) {
        const Transform* local = scene().tryGet<Transform>(charge.entity);
        const EntityId   meshed = HierarchyOperations::findInSelfOrDescendants<Mesh>(scene(), charge.entity);
        const Mesh*      shown  = scene().tryGet<Mesh>(meshed);
        if (!local || (shown && !shown->visible)) continue;

        Transform where;
        where.position = resolvedWorldPosition(scene(), charge.entity, *local);
        if (window() && !blast.empty()) Prefab::instantiate(scene(), resources(), blast, where);
        stand(scene(), charge.entity, false);
        blew = true;
    }
    return blew;
}

void LabRange::throwFromCharges() {
    const bool authority = isAuthority(net().role());
    for (const Charge& charge : m_charges) {
        const Transform* local = scene().tryGet<Transform>(charge.entity);
        if (!local) continue;
        const glm::vec3 at = resolvedWorldPosition(scene(), charge.entity, *local);

        // Out from the charge, harder the nearer, with a share of it upward. A client throws only its
        // own copies, under a NetLocal: what the server describes is the server's to throw, and a
        // predicted body thrown here too would be thrown twice.
        scene().forEach<Rigidbody, Transform>([&](EntityId id, Rigidbody& body, const Transform& t) {
            if (body.motion != RigidbodyMotion::Dynamic || id == charge.entity) return;
            if (!authority && !HierarchyOperations::findInSelfOrAncestors<NetLocal>(scene(), id)) return;
            const glm::vec3 offset = resolvedWorldPosition(scene(), id, t) - at;
            const float     d      = glm::length(offset);
            if (d >= radius) return;
            const glm::vec3 out  = d > AT_THE_CHARGE ? offset / d : Math::WORLD_UP;
            const float     kick = push * (1.0f - d / radius);
            body.linearVelocity += glm::normalize(out * (1.0f - lift) + Math::WORLD_UP * lift) * kick;
            body.angularVelocity += glm::vec3(out.z, 0.0f, -out.x) * kick * SPIN_PER_KICK;
        });
        if (!authority) continue;
        scene().forEach<Transform>([&](EntityId id, const Transform& t) {
            LabWalker* walker = findBehavior<LabWalker>(id);
            if (!walker) return;
            const glm::vec3 offset = resolvedWorldPosition(scene(), id, t) - at;
            if (glm::length(offset) >= radius * KNOCK_DOWN_SHARE) return;
            walker->knockDown(glm::normalize(offset + Math::WORLD_UP) * KNOCK_DOWN_MOMENTUM, at);
        });
    }
}

void LabRange::reset() {
    for (const Charge& charge : m_charges) {
        if (!net().simulates(scene(), charge.entity)) continue;
        if (Transform* t = scene().tryGet<Transform>(charge.entity)) {
            t->position = charge.position;
            t->rotation = charge.rotation;
        }
        if (Rigidbody* body = scene().tryGet<Rigidbody>(charge.entity)) {
            body->linearVelocity  = glm::vec3(0.0f);
            body->angularVelocity = glm::vec3(0.0f);
        }
    }
}

void LabRange::onFixedUpdate(float dt) {
    if (m_throw) {
        m_throw = false;
        throwFromCharges();
    }
    if (m_resetIn > 0.0f) {
        m_resetIn -= dt;
        if (m_resetIn > 0.0f) return;
        reset();
        m_raise = true;
    }
}

void LabRange::onUpdate(float) {
    // Before a press: charges reset on this frame's ticks stand to be blown again.
    if (m_raise) {
        m_raise = false;
        for (const Charge& charge : m_charges) stand(scene(), charge.entity, true);
    }
    const LabSwitch* button = findBehavior<LabSwitch>();
    const std::optional<uint32_t> presses = button ? button->presses() : std::nullopt;
    if (!presses || !m_presses.rose(*presses)) return;
    m_resetIn = resetSeconds;
    m_throw = detonate();
}

} // namespace Lab
